/**
 * 区块网格构建（在 Worker 中运行）。
 *
 * 地形为高度图，所以只需生成：
 *  - 每列一个顶面（带体素式顶点 AO，便于在纯色下分辨方块）；
 *  - 当相邻列更低时生成侧面，按"表层 1 格 / 填充 3 格 / 石头"分段着色；
 *  - 水面（海平面以下的列）单独一个半透明网格，颜色随水深变化。
 * 所有面均为纯色，光照以 Minecraft 风格的方向明暗直接烘焙进顶点色。
 */
import { CHUNK_SIZE, SEA_LEVEL } from '../../core/config';
import { hash2 } from '../../core/math/Random';
import { BLOCK_FILLER, BLOCK_RGB, Block } from './Blocks';
import { PADDED_SIZE } from './WorldGenerator';

export interface MeshData {
  positions: Int16Array | Float32Array;
  /** RGB，Uint8 归一化 */
  colors: Uint8Array;
  indices: Uint16Array | Uint32Array;
}

export interface ChunkMeshes {
  terrain: MeshData | null;
  water: MeshData | null;
  /** 64x64 RGBA 小地图图块 */
  minimap: Uint8ClampedArray<ArrayBuffer>;
}

// 方向明暗（Minecraft 风格）
const SHADE_TOP = 1.0;
const SHADE_Z = 0.8;
const SHADE_X = 0.64;
/** 墙脚折角处的暗化 */
const SHADE_CREASE = 0.86;
/** 顶面 AO 等级对应亮度 */
const AO_LEVELS = [1.0, 0.91, 0.84, 0.74];
/** 水面略低于方块顶面 */
const WATER_OFFSET = 0.12;
const WATER_SHALLOW = [100, 164, 163];
const WATER_DEEP = [36, 77, 112];
const WATER_DEPTH_RANGE = 14;

class GeoBuilder {
  private pos: Int16Array | Float32Array;
  private col: Uint8Array;
  private idx: Uint32Array;
  private vc = 0;
  private ic = 0;

  constructor(private readonly useFloat: boolean, capVerts: number) {
    this.pos = useFloat ? new Float32Array(capVerts * 3) : new Int16Array(capVerts * 3);
    this.col = new Uint8Array(capVerts * 3);
    this.idx = new Uint32Array(Math.ceil(capVerts * 1.5));
  }

  get vertexCount(): number {
    return this.vc;
  }

  private grow(): void {
    const cap = (this.pos.length / 3) * 2;
    const pos = this.useFloat ? new Float32Array(cap * 3) : new Int16Array(cap * 3);
    pos.set(this.pos);
    const col = new Uint8Array(cap * 3);
    col.set(this.col);
    const idx = new Uint32Array(Math.ceil(cap * 1.5));
    idx.set(this.idx);
    this.pos = pos;
    this.col = col;
    this.idx = idx;
  }

  vertex(x: number, y: number, z: number, r: number, g: number, b: number): void {
    if (this.vc * 3 + 3 > this.pos.length) this.grow();
    const o = this.vc * 3;
    this.pos[o] = x;
    this.pos[o + 1] = y;
    this.pos[o + 2] = z;
    this.col[o] = r > 255 ? 255 : r;
    this.col[o + 1] = g > 255 ? 255 : g;
    this.col[o + 2] = b > 255 ? 255 : b;
    this.vc++;
  }

  /** 以最近 4 个顶点（逆时针，外侧视角）组成一个四边形；flip 时改用 1-3 对角线。 */
  quad(flip = false): void {
    if (this.ic + 6 > this.idx.length) this.grow();
    const b = this.vc - 4;
    const i = this.idx;
    let o = this.ic;
    if (flip) {
      i[o++] = b; i[o++] = b + 1; i[o++] = b + 3;
      i[o++] = b + 1; i[o++] = b + 2; i[o++] = b + 3;
    } else {
      i[o++] = b; i[o++] = b + 1; i[o++] = b + 2;
      i[o++] = b; i[o++] = b + 2; i[o++] = b + 3;
    }
    this.ic = o;
  }

  finish(): MeshData | null {
    if (this.vc === 0) return null;
    const positions = this.pos.slice(0, this.vc * 3);
    const colors = this.col.slice(0, this.vc * 3);
    const indices = this.vc <= 65535 ? Uint16Array.from(this.idx.subarray(0, this.ic)) : this.idx.slice(0, this.ic);
    return { positions, colors, indices };
  }
}

export function buildChunkMeshes(
  cx: number,
  cz: number,
  heights: Int16Array,
  surfaces: Uint8Array,
  seed: number,
  waterLevels?: Float32Array,
  surfaceColors?: Uint8Array,
): ChunkMeshes {
  const S = CHUNK_SIZE;
  const P = PADDED_SIZE;
  const H = (lx: number, lz: number) => heights[(lz + 1) * P + (lx + 1)];
  const colorAt = (x: number, z: number, component: number, fallback: number) =>
    surfaceColors?.[((z + 1) * P + x + 1) * 3 + component] ?? fallback;
  const vertexColor = (x: number, z: number, component: number, fallback: number) =>
    (colorAt(x - 1, z - 1, component, fallback) + colorAt(x, z - 1, component, fallback) +
      colorAt(x - 1, z, component, fallback) + colorAt(x, z, component, fallback)) / 4;
  const ox = cx * S;
  const oz = cz * S;

  const terrain = new GeoBuilder(false, 40000);
  const water = new GeoBuilder(true, 4096);
  const minimap = new Uint8ClampedArray(S * S * 4);

  // 侧面：方向、邻居偏移、明暗
  const emitSide = (
    dir: 0 | 1 | 2 | 3,
    x: number,
    z: number,
    h: number,
    nh: number,
    surface: number,
    tint: number,
  ) => {
    const shade = dir < 2 ? SHADE_X : SHADE_Z;
    const filler = BLOCK_FILLER[surface];
    // 分段：[h-1,h] 表层；[h-4,h-1] 填充；[nh,h-4] 石头
    const segs: [number, number, number][] = [
      [h - 1, h, surface],
      [h - 4, h - 1, filler],
      [nh, h - 4, Block.STONE],
    ];
    for (const [s0, s1, block] of segs) {
      const y0 = Math.max(s0, nh);
      const y1 = Math.min(s1, h);
      if (y1 <= y0) continue;
      const k = shade * tint;
      const component = (c: number) => {
        const base = BLOCK_RGB[block * 3 + c];
        const surfaceColor = colorAt(x, z, c, BLOCK_RGB[surface * 3 + c]);
        return (block === surface ? surfaceColor : block === filler ? base * .8 + surfaceColor * .2 : base) * k;
      };
      const r = component(0), g = component(1), b = component(2);
      const kb = y0 === nh ? SHADE_CREASE : 1; // 墙脚
      const rb = r * kb, gb = g * kb, bb = b * kb;
      switch (dir) {
        case 0: // +X
          terrain.vertex(x + 1, y0, z, rb, gb, bb);
          terrain.vertex(x + 1, y1, z, r, g, b);
          terrain.vertex(x + 1, y1, z + 1, r, g, b);
          terrain.vertex(x + 1, y0, z + 1, rb, gb, bb);
          break;
        case 1: // -X
          terrain.vertex(x, y0, z + 1, rb, gb, bb);
          terrain.vertex(x, y1, z + 1, r, g, b);
          terrain.vertex(x, y1, z, r, g, b);
          terrain.vertex(x, y0, z, rb, gb, bb);
          break;
        case 2: // +Z
          terrain.vertex(x + 1, y0, z + 1, rb, gb, bb);
          terrain.vertex(x + 1, y1, z + 1, r, g, b);
          terrain.vertex(x, y1, z + 1, r, g, b);
          terrain.vertex(x, y0, z + 1, rb, gb, bb);
          break;
        case 3: // -Z
          terrain.vertex(x, y0, z, rb, gb, bb);
          terrain.vertex(x, y1, z, r, g, b);
          terrain.vertex(x + 1, y1, z, r, g, b);
          terrain.vertex(x + 1, y0, z, rb, gb, bb);
          break;
      }
      terrain.quad();
    }
  };

  const aoAt = (x: number, z: number, h: number, sx: number, sz: number): number => {
    const s1 = H(x + sx, z) > h ? 1 : 0;
    const s2 = H(x, z + sz) > h ? 1 : 0;
    const c = H(x + sx, z + sz) > h ? 1 : 0;
    return AO_LEVELS[s1 && s2 ? 3 : s1 + s2 + c];
  };

  const W = (x: number, z: number) => waterLevels?.[(z + 1) * P + x + 1] ?? SEA_LEVEL;
  const waterDepthAt = (x: number, z: number) => Math.max(0, W(x, z) - H(x, z));
  // 相邻面共享顶点水位，斜坡河面与跨区块边缘保持相接。
  const vertexWater = (x: number, z: number) => {
    let total = 0, count = 0;
    for (const [dx, dz] of [[-1, -1], [0, -1], [-1, 0], [0, 0]]) {
      const level = W(x + dx, z + dz);
      if (Number.isFinite(level) && H(x + dx, z + dz) < level) { total += level; count++; }
    }
    return (count ? total / count : SEA_LEVEL) - WATER_OFFSET;
  };

  for (let z = 0; z < S; z++) {
    for (let x = 0; x < S; x++) {
      const surface = surfaces[z * S + x];
      if (surface === Block.AIR) continue;
      const h = H(x, z);
      const wx = ox + x;
      const wz = oz + z;
      const tint = surfaceColors ? 1 : 0.97 + 0.06 * hash2(wx, wz, seed);
      const r = colorAt(x, z, 0, BLOCK_RGB[surface * 3]) * tint;
      const g = colorAt(x, z, 1, BLOCK_RGB[surface * 3 + 1]) * tint;
      const b = colorAt(x, z, 2, BLOCK_RGB[surface * 3 + 2]) * tint;

      // ---- 顶面（顶点顺序：(x,z) (x,z+1) (x+1,z+1) (x+1,z)，从上方看为逆时针）
      const a0 = aoAt(x, z, h, -1, -1) * SHADE_TOP;
      const a1 = aoAt(x, z, h, -1, 1) * SHADE_TOP;
      const a2 = aoAt(x, z, h, 1, 1) * SHADE_TOP;
      const a3 = aoAt(x, z, h, 1, -1) * SHADE_TOP;
      for (const [vx, vz, shade] of [[x, z, a0], [x, z + 1, a1], [x + 1, z + 1, a2], [x + 1, z, a3]]) {
        terrain.vertex(vx, h, vz, vertexColor(vx, vz, 0, r) * shade,
          vertexColor(vx, vz, 1, g) * shade, vertexColor(vx, vz, 2, b) * shade);
      }
      terrain.quad(a0 + a2 < a1 + a3);

      // ---- 侧面
      let nh = H(x + 1, z);
      if (nh < h) emitSide(0, x, z, h, nh, surface, tint);
      nh = H(x - 1, z);
      if (nh < h) emitSide(1, x, z, h, nh, surface, tint);
      nh = H(x, z + 1);
      if (nh < h) emitSide(2, x, z, h, nh, surface, tint);
      nh = H(x, z - 1);
      if (nh < h) emitSide(3, x, z, h, nh, surface, tint);

      // ---- 水面
      if (h < W(x, z)) {
        for (const [dx, dz] of [[0, 0], [0, 1], [1, 1], [1, 0]] as const) {
          const cxv = x + dx;
          const czv = z + dz;
          const d =
            (waterDepthAt(cxv - 1, czv - 1) + waterDepthAt(cxv, czv - 1) + waterDepthAt(cxv - 1, czv) + waterDepthAt(cxv, czv)) / 4;
          const t = Math.min(1, d / WATER_DEPTH_RANGE);
          water.vertex(
            cxv,
            vertexWater(cxv, czv),
            czv,
            WATER_SHALLOW[0] + (WATER_DEEP[0] - WATER_SHALLOW[0]) * t,
            WATER_SHALLOW[1] + (WATER_DEEP[1] - WATER_SHALLOW[1]) * t,
            WATER_SHALLOW[2] + (WATER_DEEP[2] - WATER_SHALLOW[2]) * t,
          );
        }
        water.quad();
      }

      // ---- 小地图像素（北为上：-Z 朝上）
      const mi = (z * S + x) * 4;
      if (h < W(x, z)) {
        const t = Math.min(1, (W(x, z) - h) / WATER_DEPTH_RANGE);
        minimap[mi] = WATER_SHALLOW[0] + (WATER_DEEP[0] - WATER_SHALLOW[0]) * t;
        minimap[mi + 1] = WATER_SHALLOW[1] + (WATER_DEEP[1] - WATER_SHALLOW[1]) * t;
        minimap[mi + 2] = WATER_SHALLOW[2] + (WATER_DEEP[2] - WATER_SHALLOW[2]) * t;
      } else {
        const north = H(x, z - 1);
        const k = h > north ? 1.1 : h < north ? 0.8 : 0.95;
        minimap[mi] = r * k;
        minimap[mi + 1] = g * k;
        minimap[mi + 2] = b * k;
      }
      minimap[mi + 3] = 255;
    }
  }

  return { terrain: terrain.finish(), water: water.finish(), minimap };
}
