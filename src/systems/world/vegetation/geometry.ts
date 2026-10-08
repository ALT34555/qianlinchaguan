/** 低多边形几何构建器（植物专用） */
import { PLANT_MATERIALS, type PlantMaterial, type ShapeSpec, type ShapeType } from './types';

/** 单个多边形面 */
export interface Face {
  pos: number[];
  col: number[];
  nrm: number[];
  mat: PlantMaterial;
}

/** 单株植物的低模几何 */
export interface PlantGeometry {
  positions: Float32Array;
  normals: Float32Array;
  colors: Uint8Array;
  indices: Uint32Array;
  /** 包围半径，用于视锥剔除 */
  radius: number;
  /** 几何高度（含枝顶） */
  height: number;
  /** 材质槽三角形索引区间 */
  groups: { mat: PlantMaterial; start: number; count: number }[];
  /** 顶点数与三角形数统计 */
  vertexCount: number;
  triangleCount: number;
}

/** 方向明暗预烘焙阶梯 */
const SHADE_STEPS = [0.58, 0.7, 0.84, 1.0];

/** 太阳方向（标准化为整数权重即可） */
const SUN: readonly [number, number, number] = [0.42, 0.84, -0.34];

const TAU = Math.PI * 2;

export function parseColor(hex: string, fallback: readonly [number, number, number] = [255, 0, 255]): [number, number, number] {
  const m = /^#?([0-9a-fA-F]{6})$/.exec(String(hex).trim());
  if (!m) return [fallback[0], fallback[1], fallback[2]];
  const v = parseInt(m[1], 16);
  return [(v >> 16) & 255, (v >> 8) & 255, v & 255];
}

/** 方向明暗 */
function shadeOf(nx: number, ny: number, nz: number): number {
  const d = nx * SUN[0] + ny * SUN[1] + nz * SUN[2];
  const t = (d + 1) / 2;
  const i = Math.round(t * (SHADE_STEPS.length - 1));
  return SHADE_STEPS[i < 0 ? 0 : i > SHADE_STEPS.length - 1 ? SHADE_STEPS.length - 1 : i];
}

export class LowPolyBuilder {
  private readonly faces: Face[] = [];

  get faceCount(): number {
    return this.faces.length;
  }

  /** 提交面（外侧逆时针） */
  push(pos: number[], col: [number, number, number], nrm: number[], mat: PlantMaterial = 'solid'): void {
    const s = shadeOf(nrm[0], nrm[1], nrm[2]);
    this.faces.push({
      pos,
      col: [clampByte(col[0] * s), clampByte(col[1] * s), clampByte(col[2] * s)],
      nrm,
      mat,
    });
  }

  /** 单位化的三角面法线 */
  static normal(
    ax: number, ay: number, az: number,
    bx: number, by: number, bz: number,
    cx: number, cy: number, cz: number,
  ): [number, number, number] {
    const ux = bx - ax, uy = by - ay, uz = bz - az;
    const vx = cx - ax, vy = cy - ay, vz = cz - az;
    let nx = uy * vz - uz * vy;
    let ny = uz * vx - ux * vz;
    let nz = ux * vy - uy * vx;
    const len = Math.hypot(nx, ny, nz);
    if (len < 1e-9) return [0, 1, 0];
    nx /= len; ny /= len; nz /= len;
    return [nx, ny, nz];
  }

  /** 三角面：自动求法线 */
  triangle(
    a: readonly [number, number, number], b: readonly [number, number, number], c: readonly [number, number, number],
    col: [number, number, number], mat: PlantMaterial = 'solid',
  ): void {
    this.push([...a, ...b, ...c], col, LowPolyBuilder.normal(a[0], a[1], a[2], b[0], b[1], b[2], c[0], c[1], c[2]), mat);
  }

  /** 四边形 */
  quad(
    a: readonly [number, number, number], b: readonly [number, number, number],
    c: readonly [number, number, number], d: readonly [number, number, number],
    col: [number, number, number], mat: PlantMaterial = 'solid',
  ): void {
    const n = LowPolyBuilder.normal(a[0], a[1], a[2], b[0], b[1], b[2], c[0], c[1], c[2]);
    this.push([...a, ...b, ...c], col, n, mat);
    this.push([...a, ...c, ...d], col, n, mat);
  }

  /** 棱柱 */
  prism(
    center: [number, number, number],
    rx: number, rz: number,
    bottom: number, top: number,
    sides: number,
    color: [number, number, number],
    opts: { rotY?: number; jitter?: number; rand?: () => number; mat?: PlantMaterial } = {},
  ): void {
    const n = Math.max(3, Math.round(sides));
    const rot = opts.rotY ?? 0;
    const jit = opts.jitter ?? 0;
    const rand = opts.rand ?? (() => 0.5);
    const [cx, cy, cz] = center;
    const bot: [number, number, number][] = [];
    const topRing: [number, number, number][] = [];
    for (let i = 0; i < n; i++) {
      const a = rot + (i / n) * TAU;
      const k = jit > 0 ? 1 + (rand() * 2 - 1) * jit : 1;
      const x = cx + Math.cos(a) * rx * k;
      const z = cz + Math.sin(a) * rz * k;
      bot.push([x, cy + bottom, z]);
      topRing.push([x, cy + top, z]);
    }
    // 侧面
    for (let i = 0; i < n; i++) {
      const j = (i + 1) % n;
      this.quad(bot[i], bot[j], topRing[j], topRing[i], color, opts.mat);
    }
    // 端盖按扇形三角剖分
    if (Math.abs(bottom) > 1e-6) {
      for (let i = 1; i < n - 1; i++) {
        this.triangle(bot[0], bot[i], bot[i + 1], color, opts.mat);
      }
    }
    for (let i = 1; i < n - 1; i++) {
      this.triangle(topRing[0], topRing[i + 1], topRing[i], color, opts.mat);
    }
  }

  /** 椎体 */
  cone(
    base: [number, number, number],
    rx: number, rz: number,
    height: number,
    sides: number,
    color: [number, number, number],
    opts: { rotY?: number; jitter?: number; rand?: () => number; radiusRatio?: number; cap?: boolean; mat?: PlantMaterial } = {},
  ): void {
    const n = Math.max(3, Math.round(sides));
    const rot = opts.rotY ?? 0;
    const jit = opts.jitter ?? 0;
    const rand = opts.rand ?? (() => 0.5);
    const ratio = opts.radiusRatio ?? 0;
    const [cx, cy, cz] = base;
    const ring: [number, number, number][] = [];
    for (let i = 0; i < n; i++) {
      const a = rot + (i / n) * TAU;
      const r = jit > 0 ? 1 + (rand() * 2 - 1) * jit : 1;
      ring.push([cx + Math.cos(a) * rx * r, cy, cz + Math.sin(a) * rz * r]);
    }
    const apex: [number, number, number] = [cx, cy + height, cz];
    if (ratio > 1e-6) {
      const top: [number, number, number][] = ring.map((p) => [cx + (p[0] - cx) * ratio, cy + height, cz + (p[2] - cz) * ratio]);
      for (let i = 0; i < n; i++) {
        const j = (i + 1) % n;
        this.quad(ring[i], ring[j], top[j], top[i], color, opts.mat);
      }
      for (let i = 1; i < n - 1; i++) this.triangle(top[0], top[i + 1], top[i], color, opts.mat);
      return;
    }
    for (let i = 0; i < n; i++) {
      const j = (i + 1) % n;
      this.triangle(ring[i], ring[j], apex, color, opts.mat);
    }
    if (opts.cap) for (let i = 1; i < n - 1; i++) this.triangle(ring[0], ring[i], ring[i + 1], color, opts.mat);
  }

  /** 低面圆顶 / 叶团 */
  dome(
    center: [number, number, number],
    rx: number, ry: number, rz: number,
    sides: number,
    color: [number, number, number],
    opts: { rotY?: number; jitter?: number; rand?: () => number; floor?: boolean; mat?: PlantMaterial; squash?: number; rings?: number } = {},
  ): void {
    const n = Math.max(3, Math.round(sides));
    const rot = opts.rotY ?? 0;
    const jit = opts.jitter ?? 0;
    const rand = opts.rand ?? (() => 0.5);
    const rings = Math.max(0, Math.round(opts.rings ?? 0));
    const [cx, cy, cz] = center;
    const ring = (rScale: number, y: number): [number, number, number][] => {
      const out: [number, number, number][] = [];
      for (let i = 0; i < n; i++) {
        const a = rot + (i / n) * TAU;
        const k = jit > 0 ? 1 + (rand() * 2 - 1) * jit : 1;
        out.push([cx + Math.cos(a) * rx * rScale * k, y, cz + Math.sin(a) * rz * rScale * k]);
      }
      return out;
    };
    const equator = ring(1, cy);
    const apex: [number, number, number] = [cx, cy + ry, cz];
    // 从赤道往上逐圈收小，最后一圈再扇到顶点
    let prev = equator;
    for (let l = 1; l <= rings; l++) {
      const phi = (l / (rings + 1)) * (Math.PI / 2);
      const cur = ring(Math.cos(phi), cy + ry * Math.sin(phi));
      for (let i = 0; i < n; i++) {
        const j = (i + 1) % n;
        this.quad(prev[i], prev[j], cur[j], cur[i], color, opts.mat);
      }
      prev = cur;
    }
    for (let i = 0; i < n; i++) {
      const j = (i + 1) % n;
      this.triangle(prev[i], prev[j], apex, color, opts.mat);
    }
    // 底面（可选）：灌木贴地、叶团朝下的那半边
    if (opts.floor !== false) {
      const belly: [number, number, number] = [cx, cy - ry * (opts.squash ?? 1), cz];
      for (let i = 0; i < n; i++) {
        const j = (i + 1) % n;
        this.triangle(equator[i], belly, equator[j], color, opts.mat);
      }
    }
  }

  /** 低面球 */
  ico(
    center: [number, number, number],
    radius: number | [number, number, number],
    sides: number,
    color: [number, number, number],
    opts: { rotY?: number; jitter?: number; rand?: () => number; rings?: number; mat?: PlantMaterial } = {},
  ): void {
    const [rx, ry, rz] = typeof radius === 'number' ? [radius, radius, radius] : radius;
    const n = Math.max(3, Math.round(sides));
    const rings = Math.max(1, Math.round(opts.rings ?? 1));
    const rot = opts.rotY ?? 0;
    const jit = opts.jitter ?? 0;
    const rand = opts.rand ?? (() => 0.5);
    const [cx, cy, cz] = center;
    const row = (level: number): [number, number, number][] => {
      const t = (level + 1) / (rings + 1); // 0..1 从下到上
      const phi = Math.PI * t;
      const ringY = cy + Math.cos(phi) * ry;
      const rScale = Math.sin(phi);
      const out: [number, number, number][] = [];
      for (let i = 0; i < n; i++) {
        const a = rot + (i / n) * TAU + (level % 2 ? Math.PI / n : 0);
        const k = jit > 0 ? 1 + (rand() * 2 - 1) * jit : 1;
        out.push([cx + Math.cos(a) * rx * rScale * k, ringY, cz + Math.sin(a) * rz * rScale * k]);
      }
      return out;
    };
    const rows: [number, number, number][][] = [];
    for (let l = 0; l < rings; l++) rows.push(row(l));
    const south: [number, number, number] = [cx, cy - ry, cz];
    const north: [number, number, number] = [cx, cy + ry, cz];
    for (let i = 0; i < n; i++) {
      const j = (i + 1) % n;
      this.triangle(rows[0][i], south, rows[0][j], color, opts.mat);
    }
    for (let l = 0; l + 1 < rings; l++) {
      for (let i = 0; i < n; i++) {
        const j = (i + 1) % n;
        this.quad(rows[l][i], rows[l][j], rows[l + 1][j], rows[l + 1][i], color, opts.mat);
      }
    }
    const top = rows[rings - 1];
    for (let i = 0; i < n; i++) {
      const j = (i + 1) % n;
      this.triangle(top[i], top[j], north, color, opts.mat);
    }
  }

  /** 轴对齐方块（带轻微随机尺寸） */
  box(
    center: [number, number, number],
    size: [number, number, number],
    color: [number, number, number],
    opts: { rotY?: number; mat?: PlantMaterial } = {},
  ): void {
    const [cx, cy, cz] = center;
    const [sx, sy, sz] = size;
    const c = Math.cos(opts.rotY ?? 0);
    const s = Math.sin(opts.rotY ?? 0);
    const corner = (ix: number, iy: number, iz: number): [number, number, number] => {
      const x = ix * sx, z = iz * sz;
      return [cx + x * c - z * s, cy + iy * sy, cz + x * s + z * c];
    };
    const v = (i: number, j: number, k: number) => corner(i ? 1 : -1, j ? 1 : -1, k ? 1 : -1);
    const faces: [number, number, number][][] = [
      [v(1, 0, 0), v(1, 0, 1), v(1, 1, 1), v(1, 1, 0)], // +X
      [v(0, 0, 0), v(0, 1, 0), v(0, 1, 1), v(0, 0, 1)], // -X
      [v(0, 0, 1), v(0, 1, 1), v(1, 1, 1), v(1, 0, 1)], // +Z
      [v(0, 0, 0), v(1, 0, 0), v(1, 1, 0), v(0, 1, 0)], // -Z
      [v(0, 1, 0), v(1, 1, 0), v(1, 1, 1), v(0, 1, 1)], // +Y
      [v(0, 0, 0), v(0, 0, 1), v(1, 0, 1), v(1, 0, 0)], // -Y
    ];
    for (const f of faces) this.quad(f[0], f[1], f[2], f[3], color, opts.mat);
  }

  /** 交叉面片 */
  crossQuad(
    base: [number, number, number],
    width: number, height: number,
    color: [number, number, number],
    opts: { rotY?: number; bend?: number; mat?: PlantMaterial } = {},
  ): void {
    const [cx, cy, cz] = base;
    const bend = opts.bend ?? 0;
    for (let p = 0; p < 2; p++) {
      const a = (opts.rotY ?? 0) + (p * Math.PI) / 2;
      const dx = Math.cos(a), dz = Math.sin(a);
      const px = -dz, pz = dx;
      const tipX = cx + dx * bend, tipZ = cz + dz * bend;
      const bl: [number, number, number] = [cx - (px * width) / 2, cy, cz - (pz * width) / 2];
      const br: [number, number, number] = [cx + (px * width) / 2, cy, cz + (pz * width) / 2];
      const tr: [number, number, number] = [tipX + (px * width * 0.32) / 2, cy + height, tipZ + (pz * width * 0.32) / 2];
      const tl: [number, number, number] = [tipX - (px * width * 0.32) / 2, cy + height, tipZ - (pz * width * 0.32) / 2];
      this.push([...bl, ...br, ...tr, ...tl], color, [0, 1, 0], opts.mat);
      this.push([...bl, ...tr, ...tl], color, [0, 1, 0], opts.mat);
    }
  }

  /** 花瓣 */
  petal(
    center: [number, number, number],
    bearing: number,
    length: number,
    width: number,
    color: [number, number, number],
    opts: { pitch?: number; cup?: number; mat?: PlantMaterial; both?: boolean; jitter?: number; rand?: () => number } = {},
  ): void {
    const [cx, cy, cz] = center;
    const jit = opts.jitter ?? 0;
    const rand = opts.rand ?? (() => 0.5);
    const pitch = opts.pitch ?? 0.3;
    const cup = opts.cup ?? 0.3;
    const len = length * (jit > 0 ? 1 + (rand() * 2 - 1) * jit : 1);
    const wid = width * (jit > 0 ? 1 + (rand() * 2 - 1) * jit : 1);
    const dx = Math.cos(bearing), dz = Math.sin(bearing);
    const sx = -dz, sz = dx;
    const lift = Math.sin(pitch) * len;
    const midY = cy + lift * 0.52 + wid * cup * 0.5;
    const midX = cx + dx * len * 0.52;
    const midZ = cz + dz * len * 0.52;
    const base: [number, number, number] = [cx, cy, cz];
    const tip: [number, number, number] = [cx + dx * len, cy + lift, cz + dz * len];
    const left: [number, number, number] = [midX - sx * wid * 0.5, midY, midZ - sz * wid * 0.5];
    const right: [number, number, number] = [midX + sx * wid * 0.5, midY, midZ + sz * wid * 0.5];
    this.quad(base, right, tip, left, color, opts.mat);
    if (opts.both) this.quad(base, left, tip, right, color, opts.mat);
  }

  /** 细枝条 */
  limb(
    start: [number, number, number],
    dir: [number, number, number],
    length: number,
    radius: number,
    color: [number, number, number],
    opts: { sides?: number; rand?: () => number; tipRatio?: number; mat?: PlantMaterial } = {},
  ): void {
    const sides = Math.max(3, Math.round(opts.sides ?? 3));
    const tipRatio = opts.tipRatio ?? 0.35;
    const end: [number, number, number] = [
      start[0] + dir[0] * length,
      start[1] + dir[1] * length,
      start[2] + dir[2] * length,
    ];
    // 构造与方向垂直的正交基
    const up: [number, number, number] = Math.abs(dir[1]) > 0.94 ? [1, 0, 0] : [0, 1, 0];
    let ux = up[1] * dir[2] - up[2] * dir[1];
    let uy = up[2] * dir[0] - up[0] * dir[2];
    let uz = up[0] * dir[1] - up[1] * dir[0];
    const ul = Math.hypot(ux, uy, uz) || 1;
    ux /= ul; uy /= ul; uz /= ul;
    const vx = dir[1] * uz - dir[2] * uy;
    const vy = dir[2] * ux - dir[0] * uz;
    const vz = dir[0] * uy - dir[1] * ux;
    const ring = (p: [number, number, number], r: number): [number, number, number][] => {
      const out: [number, number, number][] = [];
      for (let i = 0; i < sides; i++) {
        const a = (i / sides) * TAU;
        const ca = Math.cos(a) * r, sa = Math.sin(a) * r;
        out.push([p[0] + ux * ca + vx * sa, p[1] + uy * ca + vy * sa, p[2] + uz * ca + vz * sa]);
      }
      return out;
    };
    const r0 = ring(start, radius);
    const r1 = ring(end, radius * tipRatio);
    for (let i = 0; i < sides; i++) {
      const j = (i + 1) % sides;
      this.quad(r0[i], r0[j], r1[j], r1[i], color, opts.mat);
    }
    for (let i = 1; i < sides - 1; i++) this.triangle(r1[0], r1[i], r1[i + 1], color, opts.mat);
  }

  /** 打包单株几何并按材质槽分段 */
  build(): PlantGeometry {
    let vertexCount = 0;
    let indexCount = 0;
    for (const f of this.faces) {
      vertexCount += f.pos.length / 3;
      indexCount += (f.pos.length / 3 - 2) * 3;
    }
    const positions = new Float32Array(vertexCount * 3);
    const normals = new Float32Array(vertexCount * 3);
    const colors = new Uint8Array(vertexCount * 3);
    const indices = new Uint32Array(indexCount);
    const groups: { mat: PlantMaterial; start: number; count: number }[] = [];
    let v = 0;
    let idx = 0;
    let maxR = 0;
    let maxY = 0;
    // 按材质槽顺序输出
    for (const mat of PLANT_MATERIALS) {
      const start = idx;
      for (const f of this.faces) {
        if (f.mat !== mat) continue;
        const n = f.pos.length / 3;
        positions.set(f.pos, v * 3);
        // 面法线
        // 补全法线避免着色异常
        const N = LowPolyBuilder.normal(
          f.pos[0], f.pos[1], f.pos[2],
          f.pos[3], f.pos[4], f.pos[5],
          f.pos[6], f.pos[7], f.pos[8],
        );
        for (let i = 0; i < n; i++) {
          normals[(v + i) * 3] = N[0];
          normals[(v + i) * 3 + 1] = N[1];
          normals[(v + i) * 3 + 2] = N[2];
        }
        // 面的单颜色展开到各顶点
        for (let i = 0; i < n; i++) {
          colors[(v + i) * 3] = f.col[0];
          colors[(v + i) * 3 + 1] = f.col[1];
          colors[(v + i) * 3 + 2] = f.col[2];
        }
        for (let i = 2; i < n; i++) {
          indices[idx++] = v;
          indices[idx++] = v + i - 1;
          indices[idx++] = v + i;
        }
        for (let i = 0; i < n; i++) {
          const x = f.pos[i * 3], y = f.pos[i * 3 + 1], z = f.pos[i * 3 + 2];
          const r = Math.hypot(x, z);
          if (r > maxR) maxR = r;
          if (y > maxY) maxY = y;
        }
        v += n;
      }
      if (idx > start) groups.push({ mat, start, count: idx - start });
    }
    return {
      positions, normals, colors, indices, groups,
      radius: maxR,
      height: maxY,
      vertexCount,
      triangleCount: indexCount / 3,
    };
  }
}

function clampByte(v: number): number {
  const r = Math.round(v);
  return r < 0 ? 0 : r > 255 ? 255 : r;
}

/** 形状枚举映射构建方法 */
export const SHAPE_TYPES: readonly ShapeType[] = ['cone', 'prism', 'box', 'dome', 'ico', 'crossQuad', 'petal'];

/** 转换 ShapeSpec 为几何 */
export function shapeMaterial(spec: ShapeSpec): PlantMaterial {
  return spec.material ?? 'solid';
}
