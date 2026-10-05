/** 连续 low poly 地表、水面岸线和瀑布网格；在 Worker 中构建。 */
import { CHUNK_SIZE, SEA_LEVEL } from '../../core/config';
import { BLOCK_RGB, Block } from './Blocks';
import { PADDED_SIZE } from './WorldGenerator';
import { TERRAIN_GRID, terrainDiagonal, terrainHeightAt } from './TerrainSurface';
import { WATER_OFFSET, waterVertexLevel, isWaterCascade } from './WaterSurface';

export interface MeshData {
  positions: Int16Array | Float32Array;
  /** RGB，Uint8 归一化 */
  colors: Uint8Array;
  indices: Uint16Array | Uint32Array;
  /** 水面至可见地表的距离，用于浅水透明度和岸边细浪。 */
  waterDepths?: Float32Array;
}
export interface ChunkMeshes {
  terrain: MeshData | null;
  water: MeshData | null;
  minimap: Uint8ClampedArray<ArrayBuffer>;
}
const WATER_SHALLOW = [96, 166, 158];
const WATER_DEEP = [28, 70, 104];
const WATER_DEPTH_RANGE = 14;

class GeoBuilder {
  private pos: Float32Array;
  private col: Uint8Array;
  private idx: Uint32Array;
  private depth: Float32Array | undefined;
  private vc = 0;
  private ic = 0;

  constructor(capVerts: number, withDepth = false) {
    this.pos = new Float32Array(capVerts * 3);
    this.col = new Uint8Array(capVerts * 3);
    this.idx = new Uint32Array(capVerts * 2);
    if (withDepth) this.depth = new Float32Array(capVerts);
  }
  private grow(): void {
    const cap = this.pos.length / 3 * 2;
    const pos = new Float32Array(cap * 3); pos.set(this.pos); this.pos = pos;
    const col = new Uint8Array(cap * 3); col.set(this.col); this.col = col;
    const idx = new Uint32Array(cap * 2); idx.set(this.idx); this.idx = idx;
    if (this.depth) { const depth = new Float32Array(cap); depth.set(this.depth); this.depth = depth; }
  }
  vertex(x: number, y: number, z: number, r: number, g: number, b: number, depth = 0): void {
    if (this.vc * 3 + 3 > this.pos.length) this.grow();
    const o = this.vc * 3;
    this.pos[o] = x; this.pos[o + 1] = y; this.pos[o + 2] = z;
    this.col[o] = Math.max(0, Math.min(255, r));
    this.col[o + 1] = Math.max(0, Math.min(255, g));
    this.col[o + 2] = Math.max(0, Math.min(255, b));
    if (this.depth) this.depth[this.vc] = Math.max(0, depth);
    this.vc++;
  }
  quad(flip = false): void {
    if (this.ic + 6 > this.idx.length) this.grow();
    const b = this.vc - 4;
    const indices = flip ? [b, b + 1, b + 3, b + 1, b + 2, b + 3] : [b, b + 1, b + 2, b, b + 2, b + 3];
    this.idx.set(indices, this.ic); this.ic += 6;
  }
  triangle(): void {
    if (this.ic + 3 > this.idx.length) this.grow();
    this.idx.set([this.vc - 3, this.vc - 2, this.vc - 1], this.ic); this.ic += 3;
  }
  finish(): MeshData | null {
    if (!this.vc) return null;
    return {
      positions: this.pos.slice(0, this.vc * 3), colors: this.col.slice(0, this.vc * 3),
      indices: this.vc <= 65535 ? Uint16Array.from(this.idx.subarray(0, this.ic)) : this.idx.slice(0, this.ic),
      ...(this.depth ? { waterDepths: this.depth.slice(0, this.vc) } : {}),
    };
  }
}

type WaterVertex = { x: number; y: number; z: number; depth: number };
/** 在与地表相同的三角形内按水深裁剪，岸线落在坡面上，不保留方格水边。 */
function clipShore(vertices: WaterVertex[]): WaterVertex[] {
  const out: WaterVertex[] = [];
  for (let i = 0; i < vertices.length; i++) {
    const a = vertices[i], b = vertices[(i + 1) % vertices.length];
    if (a.depth >= 0) out.push(a);
    if ((a.depth >= 0) !== (b.depth >= 0)) {
      const t = a.depth / (a.depth - b.depth);
      out.push({ x: a.x + (b.x - a.x) * t, y: a.y + (b.y - a.y) * t,
        z: a.z + (b.z - a.z) * t, depth: 0 });
    }
  }
  return out;
}

export function buildChunkMeshes(
  cx: number, cz: number, heights: Int16Array | Float32Array, surfaces: Uint8Array, seed: number,
  waterLevels?: Float32Array, surfaceColors?: Uint8Array,
): ChunkMeshes {
  const S = CHUNK_SIZE, P = PADDED_SIZE, ox = cx * S, oz = cz * S;
  const H = (x: number, z: number) => heights[(z + 1) * P + x + 1];
  const W = (x: number, z: number) => waterLevels?.[(z + 1) * P + x + 1] ?? SEA_LEVEL;
  const terrain = new GeoBuilder((S / TERRAIN_GRID) ** 2 * 4);
  const water = new GeoBuilder(S * S * 4, true);
  const minimap = new Uint8ClampedArray(S * S * 4);
  // 一次计算每个共享格角；外扩样本使相邻区块给出完全相同的边缘。
  const V = S + 1;
  const vertexHeights = new Float32Array(V * V);
  const vertexLevels = new Float32Array(V * V);
  const vertexColors = new Uint8Array(V * V * 3);
  for (let z = 0; z <= S; z++) for (let x = 0; x <= S; x++) {
    const i = z * V + x;
    vertexHeights[i] = terrainHeightAt(ox + x, oz + z, seed, (wx, wz) => H(wx - ox, wz - oz));
    vertexLevels[i] = waterVertexLevel(ox + x, oz + z, (wx, wz) => H(wx - ox, wz - oz), (wx, wz) => W(wx - ox, wz - oz), seed);
    for (let c = 0; c < 3; c++) {
      let sum = 0;
      for (const [dx, dz] of [[-1, -1], [0, -1], [-1, 0], [0, 0]]) {
        const px = x + dx, pz = z + dz;
        const block = surfaces[Math.max(0, Math.min(S - 1, pz)) * S + Math.max(0, Math.min(S - 1, px))];
        sum += surfaceColors?.[((pz + 1) * P + px + 1) * 3 + c] ?? BLOCK_RGB[block * 3 + c];
      }
      vertexColors[i * 3 + c] = sum * .25;
    }
  }
  const emitWater = (v: WaterVertex) => {
    const t = Math.min(1, Math.max(0, v.depth) / WATER_DEPTH_RANGE);
    water.vertex(v.x, v.y, v.z,
      WATER_SHALLOW[0] + (WATER_DEEP[0] - WATER_SHALLOW[0]) * t,
      WATER_SHALLOW[1] + (WATER_DEEP[1] - WATER_SHALLOW[1]) * t,
      WATER_SHALLOW[2] + (WATER_DEEP[2] - WATER_SHALLOW[2]) * t, v.depth);
  };
  for (let z = 0; z < S; z++) for (let x = 0; x < S; x++) {
    if (surfaces[z * S + x] === Block.AIR) continue;
    const h = H(x, z), level = W(x, z);
    const corners = [z * V + x, (z + 1) * V + x, (z + 1) * V + x + 1, z * V + x + 1];
    const coordinates = [[x, z], [x, z + 1], [x + 1, z + 1], [x + 1, z]];
    const flip = terrainDiagonal(ox + x, oz + z, seed);
    if (x % TERRAIN_GRID === 0 && z % TERRAIN_GRID === 0) {
      for (const [px, pz] of [[x, z], [x, z + TERRAIN_GRID], [x + TERRAIN_GRID, z + TERRAIN_GRID], [x + TERRAIN_GRID, z]]) {
        const i = pz * V + px;
        terrain.vertex(px, vertexHeights[i], pz, vertexColors[i * 3], vertexColors[i * 3 + 1], vertexColors[i * 3 + 2]);
      }
      terrain.quad(flip);
    }

    const edges = [[1, 0, x + 1, z, x + 1, z + 1], [-1, 0, x, z + 1, x, z],
      [0, 1, x + 1, z + 1, x, z + 1], [0, -1, x, z, x + 1, z]];
    const wet = Number.isFinite(level) && h < level;
    const cascade = isWaterCascade(x, z, H, W);
    // 干格也检查格角，保证连续坡面与海/湖面的交线覆盖到真实岸线。
    const finiteLevels = corners.map(i => vertexLevels[i]).filter(Number.isFinite);
    if (wet || finiteLevels.length) {
      const fallback = wet ? level - WATER_OFFSET : Math.min(...finiteLevels);
      const vertices = corners.map((i, j) => {
        const y = cascade ? level - WATER_OFFSET : Number.isFinite(vertexLevels[i]) ? vertexLevels[i] : fallback;
        return { x: coordinates[j][0], y, z: coordinates[j][1], depth: y - vertexHeights[i] };
      });
      if (vertices.every(v => v.depth > 0)) { vertices.forEach(emitWater); water.quad(flip); }
      else {
        const triangles = flip ? [[0, 1, 3], [1, 2, 3]] : [[0, 1, 2], [0, 2, 3]];
        for (const tri of triangles) {
          const polygon = clipShore(tri.map(j => vertices[j]));
          for (let j = 1; j + 1 < polygon.length; j++) {
            if ([polygon[0], polygon[j], polygon[j + 1]].every(v => v.depth === 0)) continue;
            emitWater(polygon[0]); emitWater(polygon[j]); emitWater(polygon[j + 1]); water.triangle();
          }
        }
      }
      if (wet) for (const [dx, dz, ax, az, bx, bz] of edges) {
        const lower = W(x + dx, z + dz);
        if (!Number.isFinite(lower) || H(x + dx, z + dz) >= lower || level - lower <= 2) continue;
        for (const [px, py, pz] of [[ax, lower, az], [ax, level, az], [bx, level, bz], [bx, lower, bz]]) {
          water.vertex(px, py - WATER_OFFSET, pz, 155, 205, 220, 4);
        }
        water.quad();
      }
    }
    const mi = (z * S + x) * 4;
    if (wet) {
      const t = Math.min(1, (level - h) / WATER_DEPTH_RANGE);
      for (let c = 0; c < 3; c++) minimap[mi + c] = WATER_SHALLOW[c] + (WATER_DEEP[c] - WATER_SHALLOW[c]) * t;
    } else {
      const slope = (H(x, z - 1) - H(x, z + 1)) * .12 + (H(x - 1, z) - H(x + 1, z)) * .08;
      const shade = Math.max(.65, Math.min(1.15, .95 + slope));
      for (let c = 0; c < 3; c++) minimap[mi + c] = vertexColors[corners[0] * 3 + c] * shade;
    }
    minimap[mi + 3] = 255;
  }
  return { terrain: terrain.finish(), water: water.finish(), minimap };
}
