/** 岩石低模构建器 */

import type { PlantGeometry } from '../vegetation/geometry';
import { hash2 } from '../../../core/math/Random';
import { ROCK_MATERIALS, type RockMaterial } from './types';

/** RGB 三元组（0~255） */
export type RGB = [number, number, number];
/** 三维向量 / 顶点 */
export type V3 = [number, number, number];

/** 一个多边形面 */
export interface RockFace {
  pos: number[];
  col: RGB;
  nrm: V3;
  mat: RockMaterial;
}

/** 一块岩石的低模几何 */
export interface RockGeometry {
  positions: Float32Array;
  normals: Float32Array;
  colors: Uint8Array;
  indices: Uint32Array;
  /** 包围半径，用于剔除与 LOD */
  radius: number;
  /** 几何高度（含顶端伸出部分） */
  height: number;
  /** 最低点沉降深度 */
  bottom: number;
  /** 材质槽三角形索引区间 */
  groups: { mat: RockMaterial; start: number; count: number }[];
  vertexCount: number;
  triangleCount: number;
  /** 各材质槽占全部三角形的比例（0~1） */
  coverRatio: Partial<Record<RockMaterial, number>>;
}

/** 逐面推入回调函数 */
export type FacePush = (pos: number[], col: RGB, nrm: V3, mat?: RockMaterial) => void;

/** 覆被方案 */
export interface CrustSpec {
  /** 覆被占据的材质槽 */
  mat: RockMaterial;
  /** 覆被概率 0~1 */
  amount: number;
  /** 覆被朝上法线阈值 */
  slope: number;
  /** 覆被判定扰动盐值 */
  seed?: number;
}

/** 预烘焙方向明暗阶梯 */
const SHADE_STEPS = [0.58, 0.7, 0.84, 1.0];
/** 烘焙太阳光照方向 */
const SUN: readonly [number, number, number] = [0.42, 0.84, -0.34];
/** 顶点色微扰 */
const COLOR_JITTER = 0.04;

const TAU = Math.PI * 2;

export function clamp(v: number, lo: number, hi: number): number {
  return v < lo ? lo : v > hi ? hi : v;
}

export function clamp01(v: number): number {
  return clamp(v, 0, 1);
}

/** 颜色整体加深 */
export function deepen(c: RGB, depth: number): RGB {
  const k = clamp01(1 - depth);
  return [c[0] * k, c[1] * k, c[2] * k];
}

/** 颜色整体提亮 */
export function lighten(c: RGB, amount: number): RGB {
  const k = clamp01(amount);
  return [c[0] + (255 - c[0]) * k, c[1] + (255 - c[1]) * k, c[2] + (255 - c[2]) * k];
}

/** 颜色线性插值混合 */
export function mix(a: RGB, b: RGB, t: number): RGB {
  const k = clamp01(t);
  return [a[0] + (b[0] - a[0]) * k, a[1] + (b[1] - a[1]) * k, a[2] + (b[2] - a[2]) * k];
}

/** 十六进制取色 */
export function hexToRgb(hex: string | undefined, fallback: RGB = [127, 127, 127]): RGB {
  const m = /^#?([0-9a-fA-F]{6})$/.exec(String(hex ?? '').trim());
  if (!m) return [fallback[0], fallback[1], fallback[2]];
  const v = parseInt(m[1], 16);
  return [(v >> 16) & 255, (v >> 8) & 255, v & 255];
}

/** 方向明暗 */
export function shadeOf(nx: number, ny: number, nz: number): number {
  const d = nx * SUN[0] + ny * SUN[1] + nz * SUN[2];
  const t = (d + 1) / 2;
  const i = Math.round(t * (SHADE_STEPS.length - 1));
  return SHADE_STEPS[i < 0 ? 0 : i > SHADE_STEPS.length - 1 ? SHADE_STEPS.length - 1 : i];
}

/** 单位化三角面法线 */
export function faceNormal(
  ax: number, ay: number, az: number,
  bx: number, by: number, bz: number,
  cx: number, cy: number, cz: number,
): V3 {
  const ux = bx - ax, uy = by - ay, uz = bz - az;
  const vx = cx - ax, vy = cy - ay, vz = cz - az;
  let nx = uy * vz - uz * vy;
  let ny = uz * vx - ux * vz;
  let nz = ux * vy - uy * vx;
  const len = Math.hypot(nx, ny, nz);
  if (len < 1e-9) return [0, 1, 0];
  return [nx / len, ny / len, nz / len];
}

/** 绕 Y 轴旋转一个水平向量 */
export function rotateY(x: number, z: number, angle: number): [number, number] {
  const c = Math.cos(angle);
  const s = Math.sin(angle);
  return [x * c - z * s, x * s + z * c];
}

function clampByte(v: number): number {
  const r = Math.round(v);
  return r < 0 ? 0 : r > 255 ? 255 : r;
}

/** 岩石构建器 */
export class RockBuilder {
  private readonly faces: RockFace[] = [];
  /** 默认材质槽 */
  private currentMat: RockMaterial = 'solid';
  readonly rand: () => number;

  // 包围盒（推面过程中实时更新）
  private minY = 0;
  private maxY = 0;
  private maxR = 0;

  constructor(rand: () => number) {
    this.rand = rand;
  }

  /** 已推入的面数 */
  get faceCount(): number {
    return this.faces.length;
  }

  /** 设置/读取后续面的默认材质槽 */
  set material(mat: RockMaterial) {
    this.currentMat = mat;
  }

  get material(): RockMaterial {
    return this.currentMat;
  }

  /** 获取面推入函数 */
  pusher(): FacePush {
    return (pos, col, nrm, mat) => {
      this.push(pos, col, nrm, mat ?? this.currentMat);
    };
  }

  /** 推入逆时针面 */
  push(pos: number[], col: RGB, nrm: V3, mat: RockMaterial = this.currentMat, raw = false): void {
    const s = raw ? 1 : shadeOf(nrm[0], nrm[1], nrm[2]);
    const k = 1 + (this.rand() * 2 - 1) * COLOR_JITTER;
    this.faces.push({
      pos,
      col: [clampByte(col[0] * s * k), clampByte(col[1] * s * k), clampByte(col[2] * s * k)],
      nrm,
      mat,
    });
    for (let i = 0; i < pos.length; i += 3) this.track(pos[i], pos[i + 1], pos[i + 2]);
  }

  /** 三角形（法线由三顶点求出） */
  tri(a: V3, b: V3, c: V3, color: RGB, mat: RockMaterial = this.currentMat): void {
    this.push([...a, ...b, ...c], color, faceNormal(a[0], a[1], a[2], b[0], b[1], b[2], c[0], c[1], c[2]), mat);
  }

  /** 四边形拆双三角形 */
  quad(a: V3, b: V3, c: V3, d: V3, color: RGB, mat: RockMaterial = this.currentMat): void {
    this.tri(a, b, c, color, mat);
    this.tri(a, c, d, color, mat);
  }

  /** 合并外部构建器几何 */
  bake(geo: PlantGeometry, opts: { mat?: RockMaterial; offset?: V3; scale?: number; tint?: RGB; tintMix?: number } = {}): void {
    const off = opts.offset ?? [0, 0, 0];
    const s = opts.scale ?? 1;
    const mat = opts.mat ?? this.currentMat;
    const mixT = opts.tint ? (opts.tintMix ?? 1) : 0;
    for (let t = 0; t < geo.triangleCount; t++) {
      const i0 = geo.indices[t * 3] * 3;
      const i1 = geo.indices[t * 3 + 1] * 3;
      const i2 = geo.indices[t * 3 + 2] * 3;
      const p = (i: number): V3 => [
        geo.positions[i] * s + off[0],
        geo.positions[i + 1] * s + off[1],
        geo.positions[i + 2] * s + off[2],
      ];
      const nrm: V3 = [geo.normals[i0], geo.normals[i0 + 1], geo.normals[i0 + 2]];
      let col: RGB = [geo.colors[i0], geo.colors[i0 + 1], geo.colors[i0 + 2]];
      if (opts.tint) col = mix(col, opts.tint, mixT);
      this.push([...p(i0), ...p(i1), ...p(i2)], col, nrm, mat, true);
    }
  }

  private track(x: number, y: number, z: number): void {
    const r = Math.hypot(x, z);
    if (r > this.maxR) this.maxR = r;
    if (y > this.maxY) this.maxY = y;
    if (y < this.minY) this.minY = y;
  }

  // ---- 岩石专用原语 ----

  /** 逐面覆被判定 */
  private crustFace(
    index: number,
    col: RGB,
    nrm: V3,
    mat: RockMaterial,
    crust: CrustSpec | undefined,
    crustColor: RGB | undefined,
    /** 覆盖率倍率 */
    scale = 1,
  ): { col: RGB; mat: RockMaterial } {
    if (!crust || !crustColor || crust.amount <= 0) return { col, mat };
    if (nrm[1] < crust.slope) return { col, mat };
    const salt = (crust.seed ?? 0x51ed2701) >>> 0;
    if (hash2(index, 0, salt) >= crust.amount * scale) return { col, mat };
    return { col: crustColor, mat: crust.mat };
  }

  /** 环壳 */
  shell(opts: {
    /** 环心位置 */
    center: V3;
    /** 环在自身平面内的两个半径（允许椭圆） */
    rx: number;
    rz: number;
    /** 壳厚 */
    thickness: number;
    /** 径向分段数（几边形） */
    sides: number;
    /** 环面法线（一般朝上，倾斜由它表达） */
    normal: V3;
    /** 主体（侧面）色 */
    color: RGB;
    /** 顶盖色，默认主体提亮 8% */
    capColor?: RGB;
    /** 底盖色 */
    bottomColor?: RGB;
    /** 侧面色，默认等于主体色 */
    sideColor?: RGB;
    /** 顶点径向抖动量 */
    jitter?: number;
    /** 起始角（弧度） */
    rotY?: number;
    /** 材质槽，默认当前默认槽 */
    mat?: RockMaterial;
    /** 是否封顶盖，默认 true */
    cap?: boolean;
    /** 逐顶点半径倍率（按环上的顶点索引） */
    radiusScale?: readonly number[] | ((i: number) => number);
    /** 逐顶点沿环面法线的位移倍数（0~1） */
    wobble?: readonly number[] | ((i: number) => number);
    /** 壳厚抖动幅度 */
    wobbleAmount?: number;
    /** 覆被方案（只作用于顶盖与朝上的侧面） */
    crust?: CrustSpec;
    /** 覆被色 */
    crustColor?: RGB;
  }): void {
    const { center, rx, rz, thickness, sides, normal, color } = opts;
    const n = Math.max(3, Math.round(sides));
    const jit = opts.jitter ?? 0;
    const mat = opts.mat ?? this.currentMat;
    const capColor = opts.capColor ?? lighten(color, 0.08);
    const sideColor = opts.sideColor ?? color;
    const bottomColor = opts.bottomColor ?? deepen(color, 0.4);
    const rotY = opts.rotY ?? 0;
    const rs = opts.radiusScale;
    const wob = opts.wobble;
    const wobAmp = (opts.wobbleAmount ?? 0) * thickness * 0.5;

    // 计算正交基避免平行
    const [nx0, ny0, nz0] = normal;
    const nl = Math.hypot(nx0, ny0, nz0) || 1;
    const up: V3 = [nx0 / nl, ny0 / nl, nz0 / nl];
    const ref: V3 = Math.abs(up[1]) > 0.94 ? [1, 0, 0] : [0, 1, 0];
    let ux = ref[1] * up[2] - ref[2] * up[1];
    let uy = ref[2] * up[0] - ref[0] * up[2];
    let uz = ref[0] * up[1] - ref[1] * up[0];
    const ul = Math.hypot(ux, uy, uz) || 1;
    ux /= ul; uy /= ul; uz /= ul;
    const vx = up[1] * uz - up[2] * uy;
    const vy = up[2] * ux - up[0] * uz;
    const vz = up[0] * uy - up[1] * ux;

    const half = thickness / 2;
    const top: V3[] = [];
    const bot: V3[] = [];
    for (let i = 0; i < n; i++) {
      const a = rotY + (i / n) * TAU;
      const ripple = typeof rs === 'function' ? rs(i) : (rs ? rs[i] ?? 1 : 1);
      const k = (jit > 0 ? 1 + (this.rand() * 2 - 1) * jit : 1) * ripple;
      const wv = typeof wob === 'function' ? wob(i) : (wob ? wob[i] ?? 0 : 0);
      const lift = wv * wobAmp;
      const cx = Math.cos(a) * rx * k;
      const cz = Math.sin(a) * rz * k;
      const px = center[0] + ux * cx + vx * cz;
      const py = center[1] + uy * cx + vy * cz;
      const pz = center[2] + uz * cx + vz * cz;
      top.push([px + up[0] * (half + lift), py + up[1] * (half + lift), pz + up[2] * (half + lift)]);
      bot.push([px - up[0] * (half - lift), py - up[1] * (half - lift), pz - up[2] * (half - lift)]);
    }
    // 陡壁侧带降低覆被
    for (let i = 0; i < n; i++) {
      const j = (i + 1) % n;
      const nrm = faceNormal(
        bot[i][0], bot[i][1], bot[i][2],
        bot[j][0], bot[j][1], bot[j][2],
        top[j][0], top[j][1], top[j][2],
      );
      const f = this.crustFace(0x800 + i, sideColor, nrm, mat, opts.crust, opts.crustColor, 0.7);
      this.quad(bot[i], bot[j], top[j], top[i], f.col, f.mat);
    }
    // 顶盖（法线 = 环面法线）
    if (opts.cap !== false) {
      for (let i = 1; i < n - 1; i++) {
        const f = this.crustFace(0x100 + i, capColor, up, mat, opts.crust, opts.crustColor);
        this.push([...top[0], ...top[i], ...top[i + 1]], f.col, up, f.mat);
      }
    }
    // 底盖（法线朝下，颜色最暗）
    const down: V3 = [-up[0], -up[1], -up[2]];
    for (let i = 1; i < n - 1; i++) {
      this.push([...bot[0], ...bot[i + 1], ...bot[i]], bottomColor, down, mat);
    }
  }

  /** 分节柱 */
  column(opts: {
    /** 柱底中心 */
    base: V3;
    /** 柱底半径 */
    radius: number;
    /** 总高 */
    height: number;
    /** 分节数 */
    segments: number;
    /** 顶半径 / 底半径（0 = 收成尖） */
    tipRatio: number;
    /** 径向分段数 */
    sides: number;
    color: RGB;
    /** 节间错位量 */
    skew?: number;
    /** 节间扭转（弧度），默认 0.35 */
    twist?: number;
    /** 顶端色，默认主体提亮 6% */
    tipColor?: RGB;
    mat?: RockMaterial;
    jitter?: number;
    /** 节末覆被参数配置 */
    crust?: CrustSpec;
    /** 覆被色 */
    crustColor?: RGB;
  }): void {
    const segs = Math.max(1, Math.round(opts.segments));
    const n = Math.max(3, Math.round(opts.sides));
    const mat = opts.mat ?? this.currentMat;
    const skew = opts.skew ?? 0.12;
    const twist = opts.twist ?? 0.35;
    const jit = opts.jitter ?? 0;
    const tipRatio = clamp01(opts.tipRatio);
    const tipColor = opts.tipColor ?? lighten(opts.color, 0.06);
    let cx = opts.base[0];
    let cz = opts.base[2];
    let radius = opts.radius;
    let y = opts.base[1];
    for (let s = 0; s < segs; s++) {
      const t0 = s / segs;
      const t1 = (s + 1) / segs;
      const r0 = radius;
      const r1 = opts.radius * (1 - (1 - tipRatio) * t1);
      const h = (opts.height / segs) * (jit > 0 ? 1 + (this.rand() * 2 - 1) * jit : 1);
      const nextX = cx + (this.rand() * 2 - 1) * skew * r1;
      const nextZ = cz + (this.rand() * 2 - 1) * skew * r1;
      const phaseBot = -twist * 0.5;
      const phaseTop = twist * 0.5;
      const ringBot: V3[] = [];
      const ringTop: V3[] = [];
      for (let i = 0; i < n; i++) {
        const aBot = phaseBot + (i / n) * TAU;
        const aTop = phaseTop + (i / n) * TAU;
        const kBot = jit > 0 ? 1 + (this.rand() * 2 - 1) * jit : 1;
        const kTop = jit > 0 ? 1 + (this.rand() * 2 - 1) * jit : 1;
        ringBot.push([cx + Math.cos(aBot) * r0 * kBot, y, cz + Math.sin(aBot) * r0 * kBot]);
        ringTop.push([nextX + Math.cos(aTop) * r1 * kTop, y + h, nextZ + Math.sin(aTop) * r1 * kTop]);
      }
      const segColor = mix(opts.color, tipColor, t0 * 0.5);
      for (let i = 0; i < n; i++) {
        const j = (i + 1) % n;
        const nrm = faceNormal(
          ringBot[i][0], ringBot[i][1], ringBot[i][2],
          ringBot[j][0], ringBot[j][1], ringBot[j][2],
          ringTop[j][0], ringTop[j][1], ringTop[j][2],
        );
        const f = this.crustFace(s * 0x40 + i, segColor, nrm, mat, opts.crust, opts.crustColor);
        this.quad(ringBot[i], ringBot[j], ringTop[j], ringTop[i], f.col, f.mat);
      }
      // 节末封顶：不封的话收细处会漏出内部
      const faceCol = s === segs - 1 ? tipColor : mix(opts.color, tipColor, t1 * 0.6);
      for (let i = 1; i < n - 1; i++) {
        const f = this.crustFace(0x200 + s * 0x40 + i, faceCol, [0, 1, 0], mat, opts.crust, opts.crustColor);
        this.push([...ringTop[0], ...ringTop[i], ...ringTop[i + 1]], f.col, [0, 1, 0], f.mat);
      }
      cx = nextX;
      cz = nextZ;
      y += h;
      radius = r1;
    }
  }

  /** 板片 */
  slab(opts: {
    center: V3;
    rx: number;
    rz: number;
    thickness: number;
    sides: number;
    /** 倾角（弧度）：板面的翘起量 */
    tilt: number;
    /** 板岩倾角朝向弧度 */
    bearing?: number;
    color: RGB;
    mat?: RockMaterial;
    jitter?: number;
    rotY?: number;
    /** 沿法线位移边缘起伏 */
    wobble?: readonly number[] | ((i: number) => number);
    /** wobble 幅度（相对板厚） */
    wobbleAmount?: number;
    /** 覆被方案 */
    crust?: CrustSpec;
    /** 覆被色 */
    crustColor?: RGB;
  }): void {
    const bearing = opts.bearing ?? 0;
    const normal: V3 = [
      -Math.sin(opts.tilt) * Math.cos(bearing),
      Math.cos(opts.tilt),
      -Math.sin(opts.tilt) * Math.sin(bearing),
    ];
    this.shell({
      center: opts.center,
      rx: opts.rx,
      rz: opts.rz,
      thickness: opts.thickness,
      sides: opts.sides,
      normal,
      color: opts.color,
      capColor: lighten(opts.color, 0.14),
      bottomColor: deepen(opts.color, 0.45),
      sideColor: deepen(opts.color, 0.18),
      jitter: opts.jitter,
      wobble: opts.wobble,
      wobbleAmount: opts.wobbleAmount,
      rotY: opts.rotY,
      mat: opts.mat,
      crust: opts.crust,
      crustColor: opts.crustColor,
    });
  }

  /** 低面球（巨砾 / 鹅卵石） */
  blob(opts: {
    center: V3;
    /** 半径或三轴形变 */
    radius: number | V3;
    sides: number;
    color: RGB;
    /** 纬线层数，默认 1（最省面的橄榄球） */
    rings?: number;
    /** 顶面色，默认主体提亮 10% */
    capColor?: RGB;
    jitter?: number;
    rotY?: number;
    mat?: RockMaterial;
    /** 顶部覆被参数配置 */
    crust?: CrustSpec;
    /** 覆被色 */
    crustColor?: RGB;
  }): void {
    const [rx, ry, rz] = typeof opts.radius === 'number'
      ? [opts.radius, opts.radius, opts.radius] as V3
      : opts.radius;
    const n = Math.max(3, Math.round(opts.sides));
    const rings = Math.max(1, Math.round(opts.rings ?? 1));
    const rot = opts.rotY ?? 0;
    const jit = opts.jitter ?? 0;
    const mat = opts.mat ?? this.currentMat;
    const capColor = opts.capColor ?? lighten(opts.color, 0.1);
    const [cx, cy, cz] = opts.center;
    const row = (level: number): V3[] => {
      const t = (level + 1) / (rings + 1);
      const phi = Math.PI * t;
      const ringY = cy + Math.cos(phi) * ry;
      const rScale = Math.sin(phi);
      const out: V3[] = [];
      for (let i = 0; i < n; i++) {
        const a = rot + (i / n) * TAU + (level % 2 ? Math.PI / n : 0);
        const k = jit > 0 ? 1 + (this.rand() * 2 - 1) * jit : 1;
        out.push([cx + Math.cos(a) * rx * rScale * k, ringY, cz + Math.sin(a) * rz * rScale * k]);
      }
      return out;
    };
    const rows: V3[][] = [];
    for (let l = 0; l < rings; l++) rows.push(row(l));
    const south: V3 = [cx, cy - ry, cz];
    const north: V3 = [cx, cy + ry, cz];
    /** 逐面覆被 */
    const face = (index: number, a: V3, b: V3, c: V3, color: RGB): void => {
      const nrm = faceNormal(a[0], a[1], a[2], b[0], b[1], b[2], c[0], c[1], c[2]);
      const f = this.crustFace(index, color, nrm, mat, opts.crust, opts.crustColor);
      this.push([...a, ...b, ...c], f.col, nrm, f.mat);
    };
    for (let i = 0; i < n; i++) {
      const j = (i + 1) % n;
      face(i, rows[0][i], south, rows[0][j], opts.color);
    }
    for (let l = 0; l + 1 < rings; l++) {
      const col = mix(opts.color, capColor, (l + 1) / rings);
      for (let i = 0; i < n; i++) {
        const j = (i + 1) % n;
        const idx = 0x300 + l * 0x40 + i;
        // 四边形两面分别判定
        face(idx * 2, rows[l][i], rows[l][j], rows[l + 1][j], col);
        face(idx * 2 + 1, rows[l][i], rows[l + 1][j], rows[l + 1][i], col);
      }
    }
    const top = rows[rings - 1];
    for (let i = 0; i < n; i++) {
      const j = (i + 1) % n;
      face(0x400 + i, top[i], top[j], north, capColor);
    }
  }

  /** 打包成 RockGeometry */
  build(): RockGeometry {
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
    const groups: { mat: RockMaterial; start: number; count: number }[] = [];
    const coverTris: Partial<Record<RockMaterial, number>> = {};
    let v = 0;
    let idx = 0;
    // 以 ROCK_MATERIALS 顺序输出
    for (const mat of ROCK_MATERIALS) {
      const start = idx;
      for (const f of this.faces) {
        if (f.mat !== mat) continue;
        const n = f.pos.length / 3;
        positions.set(f.pos, v * 3);
        // 面法线按最终顶点重新算
        const N = faceNormal(
          f.pos[0], f.pos[1], f.pos[2],
          f.pos[3], f.pos[4], f.pos[5],
          f.pos[6], f.pos[7], f.pos[8],
        );
        for (let i = 0; i < n; i++) {
          normals[(v + i) * 3] = N[0];
          normals[(v + i) * 3 + 1] = N[1];
          normals[(v + i) * 3 + 2] = N[2];
          // 面的单颜色展开到各顶点
          colors[(v + i) * 3] = f.col[0];
          colors[(v + i) * 3 + 1] = f.col[1];
          colors[(v + i) * 3 + 2] = f.col[2];
        }
        for (let i = 2; i < n; i++) {
          indices[idx++] = v;
          indices[idx++] = v + i - 1;
          indices[idx++] = v + i;
        }
        v += n;
      }
      if (idx > start) {
        groups.push({ mat, start, count: idx - start });
        coverTris[mat] = (coverTris[mat] ?? 0) + (idx - start) / 3;
      }
    }
    const totalTris = indexCount / 3 || 1;
    const coverRatio: Partial<Record<RockMaterial, number>> = {};
    for (const [mat, tris] of Object.entries(coverTris)) {
      coverRatio[mat as RockMaterial] = tris / totalTris;
    }
    return {
      positions, normals, colors, indices, groups,
      radius: Math.max(this.maxR, 1e-3),
      height: Math.max(this.maxY, 1e-3),
      bottom: Math.min(this.minY, 0),
      vertexCount,
      triangleCount: indexCount / 3,
      coverRatio,
    };
  }
}

/** 供外部工具判断某个槽是不是岩石槽 */
export function isRockMaterial(mat: string): mat is RockMaterial {
  return (ROCK_MATERIALS as readonly string[]).includes(mat);
}
