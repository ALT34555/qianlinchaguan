/**
 * 连续地势 → 有序气候 → D8 汇水 → 地表。主线程、预览、Worker 共用。
 * 水系在区块中心组成严格下降的有向无环图；局部低洼作为内流湖。
 * 地势与类型解耦，地形/气候标签不会在区块边缘制造高度断层。
 */
import { createNoise2D, type NoiseFunction2D } from 'simplex-noise';
import { CHUNK_SIZE, SEA_LEVEL, WORLD_MAX_Y, WORLD_MIN_Y } from '../../core/config';
import { mulberry32 } from '../../core/math/Random';
import { Block } from './Blocks';
import { mixTerrainProfiles, overlayTerrain, type TerrainWeight, type TerrainProfile } from './TerrainBlend';
import { CLIMATES, DEFAULT_CLIMATE_WEIGHTS, normalizeClimateWeights, type ClimateWeights } from './WorldSettings';

export const PADDED_SIZE = CHUNK_SIZE + 2;
export const FLOW_DIRECTIONS = [
  { dx: 0, dz: -1, arrow: '↑' }, { dx: 1, dz: -1, arrow: '↗' },
  { dx: 1, dz: 0, arrow: '→' }, { dx: 1, dz: 1, arrow: '↘' },
  { dx: 0, dz: 1, arrow: '↓' }, { dx: -1, dz: 1, arrow: '↙' },
  { dx: -1, dz: 0, arrow: '←' }, { dx: -1, dz: -1, arrow: '↖' },
] as const;
const RIVER_THRESHOLD = 9;
const key = (x: number, z: number) => `${x},${z}`;
const clamp = (v: number, a = 0, b = 1) => Math.max(a, Math.min(b, v));
const smooth = (v: number) => { const t = clamp(v); return t * t * (3 - 2 * t); };
const mix = (a: number, b: number, t: number) => a + (b - a) * t;
// Catmull–Rom：相邻区块共享切线，避免双线性插值产生的方形坡折。
const cubic = (a: number, b: number, c: number, d: number, t: number) =>
  b + 0.5 * t * (c - a + t * (2 * a - 5 * b + 4 * c - d + t * (3 * (b - c) + d - a)));
interface TerrainPatch { rows: number[][] }

interface LandNode {
  cx: number; cz: number; height: number; uplift: number; moisture: number;
  plateau: number; valley: number; rift: number;
}
export interface ChunkInfo {
  cx: number; cz: number; type: number; climate: number; temperature: number;
  elevation: number; flow: number; discharge: number; riverWidth: number;
  downstream: { cx: number; cz: number } | null;
}
export interface RiverPoint { x: number; z: number; level: number; width: number }
interface ColumnSample { height: number; water: number; bank: number; channel: number; flowGrade: number }
interface RiverSegment {
  ax: number; az: number; bx: number; bz: number;
  aLevel: number; bLevel: number; aWidth: number; bWidth: number;
  length2: number; minX: number; maxX: number; minZ: number; maxZ: number;
  lake: boolean; grade: number;
}
interface HydraulicNode { x: number; z: number; level: number; tx: number; tz: number; grade: number }
interface RiverReach { segments: RiverSegment[]; start: HydraulicNode; end: HydraulicNode }
export interface ChunkGenResult {
  cx: number; cz: number; type: number;
  heights: Int16Array;
  surfaces: Uint8Array;
  /** 外扩一圈的地表混合 RGB，供相邻网格共享顶点颜色。 */
  surfaceColors: Uint8Array;
  /** 外扩一圈，每列独立水位；无水为 -Infinity。 */
  waterLevels: Float32Array;
}

export class WorldGenerator {
  readonly seed: number;
  readonly climateWeights: ClimateWeights;
  private readonly continent: NoiseFunction2D;
  private readonly mountain: NoiseFunction2D;
  private readonly land: NoiseFunction2D;
  private readonly moisture: NoiseFunction2D;
  private readonly warp: NoiseFunction2D;
  private readonly detail: NoiseFunction2D;
  private readonly mesa: NoiseFunction2D;
  private readonly phase: number;
  private readonly patches = new Map<string, TerrainPatch>();
  private readonly nodes = new Map<string, LandNode>();
  private readonly flows = new Map<string, number>();
  private readonly drainage = new Map<string, number>();
  private readonly infos = new Map<string, ChunkInfo>();
  private readonly hydraulicNodes = new Map<string, HydraulicNode>();
  private readonly paths = new Map<string, readonly RiverPoint[]>();
  private readonly profileCells = new Map<string, number[]>();
  private readonly segments = new Map<string, RiverReach[]>();

  constructor(seed: number, climateWeights: ClimateWeights = DEFAULT_CLIMATE_WEIGHTS) {
    this.seed = seed >>> 0;
    this.climateWeights = normalizeClimateWeights(climateWeights);
    const rng = mulberry32(this.seed);
    const make = () => createNoise2D(mulberry32(Math.floor(rng() * 4294967296)));
    this.continent = make(); this.mountain = make(); this.land = make();
    this.moisture = make(); this.warp = make(); this.detail = make(); this.mesa = make();
    this.phase = rng() * 128;
  }

  /** 连续温度坐标：曲折气候带按照热→冷排列，带宽按归一化权重分配。 */
  getClimate(cx: number, cz: number): { index: number; temperature: number } {
    const latitude = cz + this.phase + 5 * this.warp(cx / 25, cz / 32);
    const cycle = ((latitude % 128) + 128) % 128;
    const warmth = Math.abs(cycle - 64) / 64;
    let cumulative = 0;
    let index = 0;
    const position = Math.min(1 - Number.EPSILON, 1 - warmth);
    for (let i = 0; i < 5; i++) {
      cumulative += this.climateWeights[i];
      if (this.climateWeights[i] > 0 && position < cumulative) { index = i; break; }
    }
    return { index, temperature: 38 - 58 * position };
  }

  private node(cx: number, cz: number): LandNode {
    const k = key(cx, cz);
    const cached = this.nodes.get(k);
    if (cached) return cached;
    const c = this.continent(cx / 22 + 13.1, cz / 22 - 7.3);
    const regional = this.land(cx / 11, cz / 11);
    const warp = this.warp(cx / 16, cz / 16);
    // 拉长的山脊与低频山系包络，使相邻山丘自然连成山脉。
    const ridge = Math.pow(1 - Math.abs(this.mountain(cx / 25 + warp * 0.3, cz / 7)), 3);
    const envelope = smooth((this.mesa(cx / 32, cz / 32) + 0.22) / 0.72);
    const coast = smooth((c + 0.16) / 0.35);
    const uplift = 165 * ridge * envelope * coast;
    const plateau = smooth((this.mesa(cx / 13 + 40, cz / 13) - 0.25) / 0.4) * coast;
    const valley = smooth((-regional - 0.2) / 0.5) * coast;
    const rift = (1 - smooth(Math.abs(this.warp(cx / 19 + 90, cz / 9)) / 0.09)) * envelope * coast;
    const rawHeight = 18 + c * 95 + regional * 15 + uplift + plateau * 48 - valley * 12 - rift * 24;
    const height = rawHeight <= 210 ? rawHeight : 210 + 42 * (1 - Math.exp(-(rawHeight - 210) / 42));
    const result = { cx, cz, height, uplift, plateau, valley, rift, moisture: this.moisture(cx / 9, cz / 9) };
    this.nodes.set(k, result);
    return result;
  }

  /** 8 邻域最陡下降；相等高度不连边，因此没有循环和逆流。 */
  private flow(cx: number, cz: number): number {
    const k = key(cx, cz);
    const cached = this.flows.get(k);
    if (cached !== undefined) return cached;
    const n = this.node(cx, cz);
    let best = 0;
    let direction = -1;
    if (n.height > SEA_LEVEL) FLOW_DIRECTIONS.forEach((d, i) => {
      const drop = (n.height - this.node(cx + d.dx, cz + d.dz).height) / Math.hypot(d.dx, d.dz);
      if (drop > best) { best = drop; direction = i; }
    });
    this.flows.set(k, direction);
    return direction;
  }

  /** 精确累加所有上游降水；显式栈避免深河网递归溢出。 */
  private accumulation(cx: number, cz: number): number {
    const stack: [number, number, boolean][] = [[cx, cz, false]];
    while (stack.length) {
      const [x, z, visited] = stack.pop()!;
      const k = key(x, z);
      if (this.drainage.has(k)) continue;
      const upstream: [number, number][] = [];
      for (const d of FLOW_DIRECTIONS) {
        const nx = x + d.dx, nz = z + d.dz;
        const f = this.flow(nx, nz);
        if (f >= 0 && nx + FLOW_DIRECTIONS[f].dx === x && nz + FLOW_DIRECTIONS[f].dz === z) upstream.push([nx, nz]);
      }
      if (!visited) {
        stack.push([x, z, true]);
        for (const [nx, nz] of upstream) if (!this.drainage.has(key(nx, nz))) stack.push([nx, nz, false]);
      } else {
        let sum = 1 + (this.node(x, z).moisture + 1) * 0.45;
        for (const [nx, nz] of upstream) sum += this.drainage.get(key(nx, nz))!;
        this.drainage.set(k, sum);
      }
    }
    return this.drainage.get(key(cx, cz))!;
  }

  private width(discharge: number): number { return Math.min(17, 2 + Math.sqrt(discharge) * 0.6); }

  getChunkInfo(cx: number, cz: number): ChunkInfo {
    // 在完整查询之间释放缓存；结果不依赖缓存命中或生成顺序。
    if (this.nodes.size > 120000) {
      this.nodes.clear(); this.flows.clear(); this.drainage.clear(); this.infos.clear();
      this.segments.clear(); this.patches.clear(); this.paths.clear(); this.profileCells.clear(); this.hydraulicNodes.clear();
    }
    const k = key(cx, cz);
    const cached = this.infos.get(k);
    if (cached) return cached;
    const n = this.node(cx, cz);
    const climate = this.getClimate(cx, cz);
    const zone = CLIMATES[climate.index].zone;
    const flow = this.flow(cx, cz);
    const discharge = n.height > 0 ? this.accumulation(cx, cz) : 0;
    const river = discharge >= RIVER_THRESHOLD && n.height > 0;
    // 基础地形在所有气候带都可出现；其气候仍由温度场独立决定。
    const basic = this.detail(cx / 5 + 20, cz / 5 - 20) > 0.36 || zone === 0;
    const prefix = basic ? 0 : zone;
    let type: number;
    if (n.height <= 0) type = prefix + 2;
    else if (river) type = flow < 0 ? 2 : 5;
    else if (n.rift > 0.65) type = 9;
    else if (n.plateau > 0.5) type = prefix + 8;
    else if (n.uplift > 35) type = !basic && zone === 400 && n.height > 135 ? 406 : prefix + 3;
    else if (n.valley > 0.5) type = prefix + 7;
    else if (basic) type = n.moisture < -0.28 ? 4 : n.moisture > 0.22 ? 6 : 1;
    else if (zone === 100) type = n.height < 14 && n.moisture > 0.15 ? 105 : n.moisture < -0.35 ? 104 : n.moisture < 0 ? 101 : n.moisture < 0.35 ? 109 : 106;
    else if (zone === 400) type = n.moisture > 0.4 ? 409 : n.moisture < -0.3 ? 404 : n.moisture < 0.1 ? 401 : 405;
    else type = zone + (n.moisture < -0.3 ? 4 : n.moisture < 0.05 ? 1 : n.moisture < 0.36 ? 5 : 6);
    const d = flow < 0 ? null : FLOW_DIRECTIONS[flow];
    const info = {
      cx, cz, type, climate: climate.index, temperature: climate.temperature,
      elevation: n.height, flow: river ? flow : -1, discharge,
      riverWidth: river ? this.width(discharge) * 2 : 0,
      downstream: d ? { cx: cx + d.dx, cz: cz + d.dz } : null,
    };
    this.infos.set(k, info);
    return info;
  }

  getChunkType(cx: number, cz: number): number { return this.getChunkInfo(cx, cz).type; }

  private riverPosition(cx: number, cz: number): { x: number; z: number } {
    return { x: (cx + .5) * CHUNK_SIZE + this.warp(cx * .43 + 71, cz * .43) * 7,
      z: (cz + .5) * CHUNK_SIZE + this.detail(cx * .43, cz * .43 - 53) * 7 };
  }

  /** 主支流共享节点切线；支流以同一方向汇入干流，消除拼接处折角。 */
  private riverTangent(cx: number, cz: number): { x: number; z: number } {
    const a = this.getChunkInfo(cx, cz), p = this.riverPosition(cx, cz);
    let upstream: ChunkInfo | null = null;
    for (const d of FLOW_DIRECTIONS) {
      const candidate = this.getChunkInfo(cx + d.dx, cz + d.dz);
      if (candidate.downstream?.cx === cx && candidate.downstream?.cz === cz &&
          (!upstream || candidate.discharge > upstream.discharge)) upstream = candidate;
    }
    const previous = upstream ? this.riverPosition(upstream.cx, upstream.cz) : p;
    const next = a.downstream ? this.riverPosition(a.downstream.cx, a.downstream.cz) : p;
    const inLength = Math.hypot(p.x - previous.x, p.z - previous.z) || 1;
    const outLength = Math.hypot(next.x - p.x, next.z - p.z) || 1;
    const x = (p.x - previous.x) / inLength + (next.x - p.x) / outLength;
    const z = (p.z - previous.z) / inLength + (next.z - p.z) / outLength;
    const length = Math.hypot(x, z) || 1;
    return { x: x / length, z: z / length };
  }

  /** 真实河道中心线；节点坐标轻微偏移，曲线在平缓地势中增加曲流。 */
  getRiverPath(cx: number, cz: number): readonly RiverPoint[] {
    const k = key(cx, cz), cached = this.paths.get(k);
    if (cached) return cached;
    const a = this.getChunkInfo(cx, cz);
    if (a.elevation <= 0 || a.discharge < RIVER_THRESHOLD) return [];
    const b = a.downstream ? this.getChunkInfo(a.downstream.cx, a.downstream.cz) : a;
    const start = this.riverPosition(cx, cz), end = this.riverPosition(b.cx, b.cz);
    const aLevel = Math.max(0, a.elevation - 1.2), bLevel = Math.max(0, b.elevation - 1.2);
    if (a === b) {
      const result = [{ ...start, level: aLevel, width: this.width(a.discharge) * 1.7 }];
      this.paths.set(k, result); return result;
    }
    const length = Math.hypot(end.x - start.x, end.z - start.z);
    const ta = this.riverTangent(cx, cz), tb = this.riverTangent(b.cx, b.cz);
    const grade = (aLevel - bLevel) / length;
    const meander = 5.5 * (1 - smooth(grade / .35));
    const phase = this.detail(cx / 4, cz / 4) * Math.PI;
    const hasRiverUpstream = FLOW_DIRECTIONS.some(d => {
      const n = this.getChunkInfo(cx + d.dx, cz + d.dz);
      return n.type === 5 && n.downstream?.cx === cx && n.downstream?.cz === cz;
    });
    const startWidth = hasRiverUpstream ? this.width(a.discharge) : .8;
    const points: RiverPoint[] = [];
    for (let i = 0; i <= 16; i++) {
      const t = i / 16, u = 1 - t;
      const wiggle = meander * Math.sin(Math.PI * t) ** 2 * Math.sin(Math.PI * 2 * t + phase);
      const control = length * .32;
      points.push({
        x: u ** 3 * start.x + 3 * u * u * t * (start.x + ta.x * control) +
          3 * u * t * t * (end.x - tb.x * control) + t ** 3 * end.x - (end.z - start.z) / length * wiggle,
        z: u ** 3 * start.z + 3 * u * u * t * (start.z + ta.z * control) +
          3 * u * t * t * (end.z - tb.z * control) + t ** 3 * end.z + (end.x - start.x) / length * wiggle,
        level: i === 0 ? aLevel : i === 16 ? bLevel : mix(aLevel, bLevel, t),
        width: mix(startWidth, this.width(Math.max(a.discharge, b.discharge)), smooth(t)),
      });
    }
    this.paths.set(k, points); return points;
  }

  private hydraulicNode(cx: number, cz: number): HydraulicNode {
    const k = key(cx, cz), cached = this.hydraulicNodes.get(k);
    if (cached) return cached;
    const a = this.getChunkInfo(cx, cz), position = this.riverPosition(cx, cz);
    const tangent = this.riverTangent(cx, cz);
    const level = Math.max(0, a.elevation - 1.2);
    let grade = 0;
    if (a.downstream) {
      const b = this.getChunkInfo(a.downstream.cx, a.downstream.cz);
      const end = this.riverPosition(b.cx, b.cz);
      grade = (level - Math.max(0, b.elevation - 1.2)) / Math.hypot(end.x - position.x, end.z - position.z);
      for (const d of FLOW_DIRECTIONS) {
        const n = this.getChunkInfo(cx + d.dx, cz + d.dz);
        if (n.type !== 5 || n.downstream?.cx !== cx || n.downstream?.cz !== cz) continue;
        const start = this.riverPosition(n.cx, n.cz);
        grade = Math.min(grade, (Math.max(0, n.elevation - 1.2) - level) / Math.hypot(start.x - position.x, start.z - position.z));
      }
    }
    const node = { ...position, level, tx: tangent.x, tz: tangent.z, grade };
    this.hydraulicNodes.set(k, node); return node;
  }

  private riverSegments(cx: number, cz: number): RiverReach[] {
    const k = key(cx, cz), cached = this.segments.get(k);
    if (cached) return cached;
    const result: RiverReach[] = [];
    // 曲线和滩地可能延伸至邻块的邻块；按影响包围盒筛选，不能只查3×3。
    for (let z = cz - 2; z <= cz + 2; z++) for (let x = cx - 2; x <= cx + 2; x++) {
      const points = this.getRiverPath(x, z);
      const segments: RiverSegment[] = [];
      for (let i = 0; i < Math.max(0, points.length - 1) || i === 0 && points.length === 1; i++) {
        const a = points[i], b = points[Math.min(i + 1, points.length - 1)];
        const lake = points.length === 1;
        const radius = Math.max(a.width, b.width) + 38;
        const minX = Math.min(a.x, b.x) - radius, maxX = Math.max(a.x, b.x) + radius;
        const minZ = Math.min(a.z, b.z) - radius, maxZ = Math.max(a.z, b.z) + radius;
        if (maxX < cx * CHUNK_SIZE - 1 || minX > (cx + 1) * CHUNK_SIZE + 1 ||
            maxZ < cz * CHUNK_SIZE - 1 || minZ > (cz + 1) * CHUNK_SIZE + 1) continue;
        const length2 = (b.x - a.x) ** 2 + (b.z - a.z) ** 2;
        segments.push({ ax: a.x, az: a.z, bx: b.x, bz: b.z, aLevel: a.level, bLevel: b.level,
          aWidth: a.width, bWidth: b.width, length2, minX, maxX, minZ, maxZ, lake,
          grade: length2 ? (a.level - b.level) / Math.sqrt(length2) : 0 });
      }
      if (segments.length) {
        const info = this.getChunkInfo(x, z), downstream = info.downstream ?? info;
        result.push({ segments, start: this.hydraulicNode(x, z), end: this.hydraulicNode(downstream.cx, downstream.cz) });
      }
    }
    this.segments.set(k, result); return result;
  }

  /** 所有地形使用相同的噪声扰动边界、平滑权重；无成对硬切换。 */
  getTerrainWeights(wx: number, wz: number): TerrainWeight[] {
    const qx = (wx + .5 + this.warp(wx / 170 + 8, wz / 170) * 13) / CHUNK_SIZE - .5;
    const qz = (wz + .5 + this.detail(wx / 170, wz / 170 - 8) * 13) / CHUNK_SIZE - .5;
    const ix = Math.floor(qx), iz = Math.floor(qz), k = key(ix, iz);
    let types = this.profileCells.get(k);
    if (!types) {
      types = [this.getChunkType(ix, iz), this.getChunkType(ix + 1, iz),
        this.getChunkType(ix, iz + 1), this.getChunkType(ix + 1, iz + 1)];
      this.profileCells.set(k, types);
    }
    const tx = smooth(qx - ix), tz = smooth(qz - iz);
    return [{ type: types[0], weight: (1 - tx) * (1 - tz) }, { type: types[1], weight: tx * (1 - tz) },
      { type: types[2], weight: (1 - tx) * tz }, { type: types[3], weight: tx * tz }];
  }

  getTerrainBlend(wx: number, wz: number): TerrainProfile { return mixTerrainProfiles(this.getTerrainWeights(wx, wz)); }

  private patch(cx: number, cz: number): TerrainPatch {
    const k = key(cx, cz);
    const cached = this.patches.get(k);
    if (cached) return cached;
    const rows: number[][] = [];
    for (let z = -1; z <= 2; z++) {
      const row: number[] = [];
      for (let x = -1; x <= 2; x++) row.push(this.node(cx + x, cz + z).height);
      rows.push(row);
    }
    const patch = { rows }; this.patches.set(k, patch); return patch;
  }

  /** 双三次地势保持坡度连续；河床使用深槽→浅滩→湿岸→滩地的连续断面。 */
  private column(wx: number, wz: number): ColumnSample {
    const x = Math.floor(wx), z = Math.floor(wz);
    const fx = (x + .5) / CHUNK_SIZE - .5, fz = (z + .5) / CHUNK_SIZE - .5;
    const ix = Math.floor(fx), iz = Math.floor(fz), tx = fx - ix, tz = fz - iz;
    const rows = this.patch(ix, iz).rows;
    const row = (i: number) => cubic(rows[i][0], rows[i][1], rows[i][2], rows[i][3], tx);
    const profile = this.getTerrainBlend(x, z);
    const baseHeight = cubic(row(0), row(1), row(2), row(3), tz) +
      this.detail(x / 44, z / 44) * profile.roughness * Math.sin(Math.PI * tx) ** 2 * Math.sin(Math.PI * tz) ** 2;
    let height = baseHeight;
    let water = baseHeight < 0 ? SEA_LEVEL : -Infinity;
    let nearest = Infinity, bank = 0, channel = 0, flowGrade = 0;
    const px = x + .5, pz = z + .5;
    const noise = this.detail(x / 37 + 21, z / 37 - 31);
    for (const reach of this.riverSegments(Math.floor(x / CHUNK_SIZE), Math.floor(z / CHUNK_SIZE))) {
      let chosen: RiverSegment | null = null, distance = Infinity, t = 0;
      // 每条曲线只使用最近投影，避免各小段圆帽叠加出锯齿岸线。
      for (const part of reach.segments) {
        if (px < part.minX || px > part.maxX || pz < part.minZ || pz > part.maxZ) continue;
        const dx = part.bx - part.ax, dz = part.bz - part.az;
        const u = part.length2 ? clamp(((px - part.ax) * dx + (pz - part.az) * dz) / part.length2) : 0;
        const d = Math.hypot(px - mix(part.ax, part.bx, u), pz - mix(part.az, part.bz, u));
        if (d < distance) { distance = d; t = u; chosen = part; }
      }
      if (!chosen) continue;
      const s = chosen;
      const width = mix(s.aWidth, s.bWidth, t) * (1 + noise * (s.lake ? .18 : .07));
      const shore = 5 + width * .45, floodplain = 18 + width * .4;
      if (distance > width + shore + floodplain) continue;
      let level = mix(s.aLevel, s.bLevel, t);
      // 汇流节点周围共享同一缓降水面，像素落在邻支流一侧时也不会突然换水位。
      for (const node of [reach.start, reach.end]) {
        const d = Math.hypot(px - node.x, pz - node.z);
        if (d >= 24) continue;
        const plane = Math.max(0, node.level - ((px - node.x) * node.tx + (pz - node.z) * node.tz) * node.grade);
        level = mix(level, plane, smooth((24 - d) / 8));
      }
      const depth = 1.8 + Math.sqrt(width) * .65;
      const r = distance / width;
      // 圆弧形深槽逐渐抬升至浅滩；岸线位于深度归零处，不再画等深矩形槽。
      let bed: number;
      if (r < .65) bed = level - depth + (depth - .85) * smooth(r / .65);
      else if (r < 1) bed = level - .85 + 1.35 * smooth((r - .65) / .35);
      else {
        const bankT = clamp((distance - width) / shore);
        bed = level + .5 + 1.3 * smooth(bankT);
        const outer = smooth((distance - width - shore) / floodplain);
        bed = mix(bed, Math.max(bed, baseHeight), outer);
      }
      height = Math.min(height, bed);
      bank = Math.max(bank, 1 - smooth((distance - width * .75) / (shore + width * .5)));
      // 最近中心线提供唯一水位，避免较高支流抬高干流；全河网共享同一规则。
      if (distance < width && (distance < nearest - 1e-6 || Math.abs(distance - nearest) < 1e-6 && level < water)) {
        nearest = distance; water = level; channel = clamp(1 - r); flowGrade = s.grade;
      }
    }
    height = clamp(Math.floor(height), WORLD_MIN_Y + 1, WORLD_MAX_Y);
    if (height < SEA_LEVEL) water = Math.max(SEA_LEVEL, water);
    if (height >= water - .12) water = -Infinity;
    return { height, water, bank, channel, flowGrade };
  }

  getHeight(wx: number, wz: number): number { return this.column(wx, wz).height; }
  getWaterLevel(wx: number, wz: number): number { return this.column(wx, wz).water; }

  findSpawnChunk(maxRadius = 64): { cx: number; cz: number } {
    for (let r = 0; r <= maxRadius; r++) for (let z = -r; z <= r; z++) for (let x = -r; x <= r; x++) {
      if (Math.max(Math.abs(x), Math.abs(z)) !== r) continue;
      const type = this.getChunkType(x, z);
      if (![1, 101, 201, 301, 401, 4, 6, 204, 304, 404, 409].includes(type)) continue;
      const wx = (x + 0.5) * CHUNK_SIZE + 0.5, wz = (z + 0.5) * CHUNK_SIZE + 0.5;
      const h = this.getHeight(wx, wz);
      if (h > this.getWaterLevel(wx, wz) && Math.abs(h - this.getHeight(wx + 2, wz)) <= 1 &&
          Math.abs(h - this.getHeight(wx, wz + 2)) <= 1) return { cx: x, cz: z };
    }
    throw new Error('未找到安全出生点，请更换种子。');
  }

  generateChunk(cx: number, cz: number): ChunkGenResult {
    const S = CHUNK_SIZE, P = PADDED_SIZE;
    const heights = new Int16Array(P * P), waterLevels = new Float32Array(P * P);
    const surfaces = new Uint8Array(S * S), surfaceColors = new Uint8Array(P * P * 3);
    const samples: ColumnSample[] = [];
    for (let z = -1; z <= S; z++) for (let x = -1; x <= S; x++) {
      const column = this.column(cx * S + x, cz * S + z), i = (z + 1) * P + x + 1;
      heights[i] = column.height; waterLevels[i] = column.water; samples[i] = column;
    }
    const heightAt = (x: number, z: number) => x >= -1 && x <= S && z >= -1 && z <= S
      ? heights[(z + 1) * P + x + 1] : this.getHeight(cx * S + x, cz * S + z);
    for (let z = -1; z <= S; z++) for (let x = -1; x <= S; x++) {
      const wx = cx * S + x, wz = cz * S + z, i = (z + 1) * P + x + 1;
      const sample = samples[i], h = sample.height;
      const slope = Math.max(Math.abs(h - heightAt(x - 1, z)), Math.abs(h - heightAt(x + 1, z)),
        Math.abs(h - heightAt(x, z - 1)), Math.abs(h - heightAt(x, z + 1)));
      const blend = this.getTerrainBlend(wx, wz);
      const rock = smooth((slope - 1) / 4);
      overlayTerrain(blend, rock, [129, 132, 122], [[Block.STONE, .8], [Block.GRAVEL, .2]]);
      const climate = this.getClimate((wx + .5) / S - .5, (wz + .5) / S - .5);
      const position = (38 - climate.temperature) / 58;
      const coldBoundary = 1 - this.climateWeights[4];
      const cold = this.climateWeights[4] === 0 ? 0 : this.climateWeights[4] === 1 ? 1
        : smooth((position - coldBoundary + .035) / .07);
      const snow = Math.max(cold, smooth((h - 166) / 40)) * (1 - rock * .65);
      overlayTerrain(blend, snow, [226, 234, 226], [[Block.SNOW, 1]]);
      // 河岸、海岸、湖岸也采用同一环境层混合，湿泥/砂砾/植被各有空间。
      const coast = h < 5 ? 1 - smooth(Math.max(0, h) / 5) : 0;
      const shore = Math.max(sample.bank, coast);
      const wet = blend.wetness;
      const sediment: [number, number, number] = [mix(186, 127, wet), mix(174, 134, wet), mix(127, 99, wet)];
      overlayTerrain(blend, shore * .75 * (1 - snow * .65), sediment,
        [[Block.SAND, .45 * (1 - wet)], [Block.GRAVEL, .25], [Block.MUD, .3 + wet * .45]]);
      if (h < sample.water) {
        const coarse = smooth(sample.flowGrade / .25);
        const depth = sample.water - h;
        overlayTerrain(blend, .9, [mix(156, 116, coarse), mix(148, 124, coarse), mix(116, 119, coarse)],
          [[Block.SAND, (1 - coarse) * .6], [Block.MUD, (1 - coarse) * .4], [Block.GRAVEL, coarse]]);
        const darken = 1 - Math.min(.16, depth * .018);
        blend.color = blend.color.map(v => v * darken) as TerrainProfile['color'];
      }
      const variation = 1 + this.detail(wx / 11 + 19, wz / 11) * .035;
      for (let c = 0; c < 3; c++) surfaceColors[i * 3 + c] = clamp(Math.round(blend.color[c] * variation), 0, 255);
      if (x >= 0 && x < S && z >= 0 && z < S) {
        const pick = clamp(.5 + this.detail(wx / 6 - 17, wz / 6 + 33) * .48);
        let cumulative = 0, block = Block.GRASS as number;
        for (let b = 0; b < blend.materials.length; b++) {
          cumulative += blend.materials[b];
          if (pick <= cumulative && blend.materials[b] > 0) { block = b; break; }
        }
        surfaces[z * S + x] = block;
      }
    }
    return { cx, cz, type: this.getChunkType(cx, cz), heights, surfaces, waterLevels, surfaceColors };
  }
}
