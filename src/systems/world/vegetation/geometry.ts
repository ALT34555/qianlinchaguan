/**
 * 低多边形几何构建器（植物专用）。
 *
 * 设计要点：
 *   1. **不依赖 three**：只产出 position / normal / color / index 四个数组。
 *      → 可以在 Web Worker 里生成整片森林的植被网格，主线程只负责上传 GPU；
 *      → 也让导出脚本（Node 环境）可以直接复用同一份几何，产物与游戏内 100% 一致。
 *   2. **纯色低模**：所有颜色写进顶点色，方向明暗直接烘焙进顶点色，
 *      因此渲染只需要一个材质（与 ChunkMesher 的纯色风格一致，无需贴图与光源）。
 *   3. **可复现**：抖动全部来自调用方传入的确定性随机序列，同一种子必然得到同一棵树。
 */
import { PLANT_MATERIALS, type PlantMaterial, type ShapeSpec, type ShapeType } from './types';

/** 单个多边形面：顶点坐标、顶点色（0~255）、法线、材质槽 */
export interface Face {
  pos: number[];
  col: number[];
  nrm: number[];
  mat: PlantMaterial;
}

/** 单株植物的低模几何：positions / normals / colors 为同长扁平数组，indices 为三角形索引 */
export interface PlantGeometry {
  positions: Float32Array;
  normals: Float32Array;
  colors: Uint8Array;
  indices: Uint32Array;
  /** 包围半径（含叶冠），用于视锥剔除与 LOD */
  radius: number;
  /** 几何高度（含枝顶） */
  height: number;
  /** 各材质槽的三角形区间 [start, count]（索引为单位，非三角形数） */
  groups: { mat: PlantMaterial; start: number; count: number }[];
  /** 顶点数与三角形数统计 */
  vertexCount: number;
  triangleCount: number;
}

/** 方向明暗阶梯：预烘焙光照，避免运行时开销 */
const SHADE_STEPS = [0.58, 0.7, 0.84, 1.0];

/** 太阳方向（标准化为整数权重即可）：偏上方、略偏 +X/-Z */
const SUN: readonly [number, number, number] = [0.42, 0.84, -0.34];

const TAU = Math.PI * 2;

export function parseColor(hex: string, fallback: readonly [number, number, number] = [255, 0, 255]): [number, number, number] {
  const m = /^#?([0-9a-fA-F]{6})$/.exec(String(hex).trim());
  if (!m) return [fallback[0], fallback[1], fallback[2]];
  const v = parseInt(m[1], 16);
  return [(v >> 16) & 255, (v >> 8) & 255, v & 255];
}

/**
 * 方向明暗：把法线量化成 4 档，得到"上亮、侧中、下暗"的纯色立体感。
 * 与体素世界的烘焙方式保持一致，避免低模在纯色材质下糊成一团。
 */
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

  /** 提交一个面（顶点顺序需符合外侧逆时针） */
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

  /** 四边形：按 a-b-c-d 顺序拆成两个三角形，要求平面且凸 */
  quad(
    a: readonly [number, number, number], b: readonly [number, number, number],
    c: readonly [number, number, number], d: readonly [number, number, number],
    col: [number, number, number], mat: PlantMaterial = 'solid',
  ): void {
    const n = LowPolyBuilder.normal(a[0], a[1], a[2], b[0], b[1], b[2], c[0], c[1], c[2]);
    this.push([...a, ...b, ...c], col, n, mat);
    this.push([...a, ...c, ...d], col, n, mat);
  }

  /**
   * 棱柱：底面多边形（逆时针）向上挤出，带上下封盖与四边形侧面。
   * bottom / top 为逐顶点的 y 坐标，允许锥形收口。
   */
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
    // 侧面：外法线朝外，绕序为 bot[i] -> bot[i+1] -> top[i+1] -> top[i]
    for (let i = 0; i < n; i++) {
      const j = (i + 1) % n;
      this.quad(bot[i], bot[j], topRing[j], topRing[i], color, opts.mat);
    }
    // 底 / 顶盖：统一按"逆时针环 + 首顶点扇形"拆
    if (Math.abs(bottom) > 1e-6) {
      for (let i = 1; i < n - 1; i++) {
        this.triangle(bot[0], bot[i], bot[i + 1], color, opts.mat);
      }
    }
    for (let i = 1; i < n - 1; i++) {
      this.triangle(topRing[0], topRing[i + 1], topRing[i], color, opts.mat);
    }
  }

  /**
   * 椎体：底面多边形收到一点。radiusRatio 为顶端相对半径（0 = 尖顶，>0 = 平头）。
   * 底部默认不封盖（针叶层叠伞盖、棕榈树干收口都用不到底面）。
   */
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

  /**
   * 低面圆顶 / 叶团：一圈等分顶点 + 顶点，floor=true 时封底。
   * 用于阔叶树冠、灌木、浆果，是"低模球"最省面的做法（6 边 = 6 + 6 三角形）。
   */
  dome(
    center: [number, number, number],
    rx: number, ry: number, rz: number,
    sides: number,
    color: [number, number, number],
    opts: { rotY?: number; jitter?: number; rand?: () => number; floor?: boolean; mat?: PlantMaterial; squash?: number } = {},
  ): void {
    const n = Math.max(3, Math.round(sides));
    const rot = opts.rotY ?? 0;
    const jit = opts.jitter ?? 0;
    const rand = opts.rand ?? (() => 0.5);
    const [cx, cy, cz] = center;
    const ring: [number, number, number][] = [];
    for (let i = 0; i < n; i++) {
      const a = rot + (i / n) * TAU;
      const k = jit > 0 ? 1 + (rand() * 2 - 1) * jit : 1;
      ring.push([cx + Math.cos(a) * rx * k, cy, cz + Math.sin(a) * rz * k]);
    }
    const apex: [number, number, number] = [cx, cy + ry, cz];
    for (let i = 0; i < n; i++) {
      const j = (i + 1) % n;
      this.triangle(ring[i], ring[j], apex, color, opts.mat);
    }
    // 底面（可选）：灌木贴地、叶团朝下的那半边
    if (opts.floor !== false) {
      const belly: [number, number, number] = [cx, cy - ry * (opts.squash ?? 1), cz];
      for (let i = 0; i < n; i++) {
        const j = (i + 1) % n;
        this.triangle(ring[i], belly, ring[j], color, opts.mat);
      }
    }
  }

  /**
   * 低面球：上下各一层盖 + 中间一圈，rings 增加纬线层数（果实/大雪团用 2 层）。
   */
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

  /**
   * 交叉面片：两片十字相交的单面四边形（草丛、蕨叶、幼苗）。
   * 法线强制朝上，否则背光面会黑成一片，草丛在纯色低模下像剪影。
   */
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

  /** 细枝条：从 start 沿方向 direction（已被调用方归一化）生长，向末端收细 */
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

  /** 把构建结果打包成单株几何，并按材质槽分段 */
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
    // 以 PLANT_MATERIALS 的顺序输出，保证导出结果稳定可比对
    for (const mat of PLANT_MATERIALS) {
      const start = idx;
      for (const f of this.faces) {
        if (f.mat !== mat) continue;
        const n = f.pos.length / 3;
        positions.set(f.pos, v * 3);
        // 面法线：低模不需要平滑法线，平面法线正好符合"棱角分明"的观感。
        // 注意必须在这里补齐 —— 法线缺省为全 0 时，外部工具（Blender / three.js）
        // 会按零向量处理，整株模型会黑掉。
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
        // f.col 是**单个 RGB 三元组**（面的统一顶点色），不是每顶点数组，
        // 因此必须为这个面的每个顶点重复写出一次，否则只有第一个顶点有色、其余全黑。
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

/** 形状枚举 -> 构建器方法名（参数表校验与文档生成共用） */
export const SHAPE_TYPES: readonly ShapeType[] = ['cone', 'prism', 'box', 'dome', 'ico', 'crossQuad'];

/** 供导出脚本把 ShapeSpec 转成实际几何：材质槽缺失时按 solid 处理 */
export function shapeMaterial(spec: ShapeSpec): PlantMaterial {
  return spec.material ?? 'solid';
}
