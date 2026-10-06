/** 旗帜低模构建器 */

import { FLAG_MATERIALS, type FlagMaterial, type RGB, type V3 } from './types';

export interface FlagFace {
  pos: number[];
  col: RGB;
  nrm: V3;
  mat: FlagMaterial;
}

export interface FlagGeometry {
  positions: Float32Array;
  normals: Float32Array;
  colors: Uint8Array;
  indices: Uint32Array;
  radius: number;
  height: number;
  bottom: number;
  groups: { mat: FlagMaterial; start: number; count: number }[];
  vertexCount: number;
  triangleCount: number;
  materialRatio: Partial<Record<FlagMaterial, number>>;
}

const SHADE_STEPS = [0.58, 0.7, 0.84, 1.0];
const SUN: readonly [number, number, number] = [0.42, 0.84, -0.34];
const COLOR_JITTER = 0.03;
const TAU = Math.PI * 2;
const EPS = 1e-9;

export function clamp(v: number, lo: number, hi: number): number {
  return v < lo ? lo : v > hi ? hi : v;
}

export function clamp01(v: number): number {
  return clamp(v, 0, 1);
}

export function deepen(c: RGB, depth: number): RGB {
  const k = clamp01(1 - depth);
  return [c[0] * k, c[1] * k, c[2] * k];
}

export function lighten(c: RGB, amount: number): RGB {
  const k = clamp01(amount);
  return [c[0] + (255 - c[0]) * k, c[1] + (255 - c[1]) * k, c[2] + (255 - c[2]) * k];
}

export function mix(a: RGB, b: RGB, t: number): RGB {
  const k = clamp01(t);
  return [a[0] + (b[0] - a[0]) * k, a[1] + (b[1] - a[1]) * k, a[2] + (b[2] - a[2]) * k];
}

export function hexToRgb(hex: string | undefined, fallback: RGB = [127, 127, 127]): RGB {
  const m = /^#?([0-9a-fA-F]{6})$/.exec(String(hex ?? '').trim());
  if (!m) return [fallback[0], fallback[1], fallback[2]];
  const v = parseInt(m[1], 16);
  return [(v >> 16) & 255, (v >> 8) & 255, v & 255];
}

export function shadeOf(nx: number, ny: number, nz: number): number {
  const d = nx * SUN[0] + ny * SUN[1] + nz * SUN[2];
  const t = (d + 1) / 2;
  const i = Math.round(t * (SHADE_STEPS.length - 1));
  return SHADE_STEPS[i < 0 ? 0 : i > SHADE_STEPS.length - 1 ? SHADE_STEPS.length - 1 : i];
}

export function faceNormal(ax: number, ay: number, az: number, bx: number, by: number, bz: number, cx: number, cy: number, cz: number): V3 {
  const ux = bx - ax, uy = by - ay, uz = bz - az;
  const vx = cx - ax, vy = cy - ay, vz = cz - az;
  let nx = uy * vz - uz * vy;
  let ny = uz * vx - ux * vz;
  let nz = ux * vy - uy * vx;
  const len = Math.hypot(nx, ny, nz);
  if (len < EPS) return [0, 1, 0];
  return [nx / len, ny / len, nz / len];
}

export function rotateY(x: number, z: number, angle: number): [number, number] {
  const c = Math.cos(angle);
  const s = Math.sin(angle);
  return [x * c - z * s, x * s + z * c];
}

export function rotateZ(x: number, y: number, angle: number): [number, number] {
  const c = Math.cos(angle);
  const s = Math.sin(angle);
  return [x * c - y * s, x * s + y * c];
}

export function rotateX(y: number, z: number, angle: number): [number, number] {
  const c = Math.cos(angle);
  const s = Math.sin(angle);
  return [y * c - z * s, y * s + z * c];
}

export function addV3(a: V3, b: V3): V3 {
  return [a[0] + b[0], a[1] + b[1], a[2] + b[2]];
}

export function subV3(a: V3, b: V3): V3 {
  return [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
}

export function scaleV3(a: V3, k: number): V3 {
  return [a[0] * k, a[1] * k, a[2] * k];
}

export function normV3(a: V3): V3 {
  const len = Math.hypot(a[0], a[1], a[2]);
  if (len < EPS) return [0, 1, 0];
  return [a[0] / len, a[1] / len, a[2] / len];
}

export function crossV3(a: V3, b: V3): V3 {
  return [
    a[1] * b[2] - a[2] * b[1],
    a[2] * b[0] - a[0] * b[2],
    a[0] * b[1] - a[1] * b[0],
  ];
}

export function lerpV3(a: V3, b: V3, t: number): V3 {
  return [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t];
}

function clampByte(v: number): number {
  const r = Math.round(v);
  return r < 0 ? 0 : r > 255 ? 255 : r;
}

export interface RodOptions {
  radius2?: number;
  sides?: number;
  rotY?: number;
  color2?: RGB;
  mat?: FlagMaterial;
  caps?: boolean;
}

export interface BallOptions {
  sides?: number;
  rings?: number;
  squashY?: number;
  rotY?: number;
  mat?: FlagMaterial;
}

export interface BoxOptions {
  rotY?: number;
  mat?: FlagMaterial;
  topColor?: RGB;
}

export interface BakeOptions {
  offset?: V3;
  scale?: number;
  rotX?: number;
  rotZ?: number;
  rotY?: number;
  tint?: RGB;
  tintMix?: number;
  mat?: FlagMaterial;
  flip?: boolean;
}

export class FlagBuilder {
  private readonly faces: FlagFace[] = [];
  private currentMat: FlagMaterial = 'cloth';
  readonly rand: () => number;
  private minY = 0;
  private maxY = 0;
  private maxR = 0;

  constructor(rand?: () => number) {
    this.rand = rand ?? (() => 0.5);
  }

  get faceCount(): number {
    return this.faces.length;
  }

  set material(mat: FlagMaterial) {
    this.currentMat = mat;
  }

  get material(): FlagMaterial {
    return this.currentMat;
  }

  push(pos: number[], col: RGB, nrm: V3, mat: FlagMaterial = this.currentMat, raw = false): void {
    const s = raw ? 1 : shadeOf(nrm[0], nrm[1], nrm[2]);
    const k = raw ? 1 : 1 + (this.rand() * 2 - 1) * COLOR_JITTER;
    this.faces.push({
      pos,
      col: [clampByte(col[0] * s * k), clampByte(col[1] * s * k), clampByte(col[2] * s * k)],
      nrm,
      mat,
    });
    for (let i = 0; i < pos.length; i += 3) this.track(pos[i], pos[i + 1], pos[i + 2]);
  }

  tri(a: V3, b: V3, c: V3, col: RGB, mat: FlagMaterial = this.currentMat): void {
    this.push([...a, ...b, ...c], col, faceNormal(a[0], a[1], a[2], b[0], b[1], b[2], c[0], c[1], c[2]), mat);
  }

  quad(a: V3, b: V3, c: V3, d: V3, col: RGB, mat: FlagMaterial = this.currentMat): void {
    const nrm = faceNormal(a[0], a[1], a[2], b[0], b[1], b[2], c[0], c[1], c[2]);
    this.push([...a, ...b, ...c], col, nrm, mat);
    this.push([...a, ...c, ...d], col, nrm, mat);
  }

  rod(from: V3, to: V3, radius: number, color: RGB, opts: RodOptions = {}): void {
    const sides = Math.max(3, Math.round(opts.sides ?? 6));
    const mat = opts.mat ?? this.currentMat;
    const r0 = radius;
    const r1 = opts.radius2 ?? radius;
    const axis = subV3(to, from);
    const len = Math.hypot(axis[0], axis[1], axis[2]);
    if (len < EPS) return;
    const dir = scaleV3(axis, 1 / len);
    const ref: V3 = Math.abs(dir[1]) > 0.94 ? [1, 0, 0] : [0, 1, 0];
    const u = normV3(crossV3(ref, dir));
    const v = crossV3(dir, u);
    const rot = opts.rotY ?? 0;
    const side: RGB = opts.color2 ? mix(color, opts.color2, 0.5) : color;
    const tipCol: RGB = opts.color2 ?? lighten(color, 0.08);
    const ringA: V3[] = [];
    const ringB: V3[] = [];
    for (let i = 0; i < sides; i++) {
      const a = rot + (i / sides) * TAU;
      const ca = Math.cos(a);
      const sa = Math.sin(a);
      ringA.push([
        from[0] + (u[0] * ca + v[0] * sa) * r0,
        from[1] + (u[1] * ca + v[1] * sa) * r0,
        from[2] + (u[2] * ca + v[2] * sa) * r0,
      ]);
      ringB.push([
        to[0] + (u[0] * ca + v[0] * sa) * r1,
        to[1] + (u[1] * ca + v[1] * sa) * r1,
        to[2] + (u[2] * ca + v[2] * sa) * r1,
      ]);
    }
    for (let i = 0; i < sides; i++) {
      const j = (i + 1) % sides;
      if (r1 < EPS) this.tri(ringA[i], ringA[j], to, side, mat);
      else this.quad(ringA[i], ringA[j], ringB[j], ringB[i], side, mat);
    }
    if (opts.caps !== false) {
      if (r1 > EPS) {
        const nB = faceNormal(ringB[0][0], ringB[0][1], ringB[0][2], ringB[1][0], ringB[1][1], ringB[1][2], to[0], to[1], to[2]);
        for (let i = 1; i < sides - 1; i++) {
          this.push([...ringB[0], ...ringB[i], ...ringB[i + 1]], tipCol, nB, mat);
        }
      }
      const nA = faceNormal(from[0], from[1], from[2], ringA[1][0], ringA[1][1], ringA[1][2], ringA[0][0], ringA[0][1], ringA[0][2]);
      for (let i = 1; i < sides - 1; i++) {
        this.push([...ringA[0], ...ringA[i + 1], ...ringA[i]], color, nA, mat);
      }
    }
  }

  box(center: V3, size: V3, color: RGB, opts: BoxOptions = {}): void {
    const mat = opts.mat ?? this.currentMat;
    const hx = size[0] / 2;
    const hy = size[1] / 2;
    const hz = size[2] / 2;
    const top = opts.topColor ?? lighten(color, 0.08);
    const side = deepen(color, 0.1);
    const rot = opts.rotY ?? 0;
    const c = Math.cos(rot);
    const s = Math.sin(rot);
    const p = (sx: number, sy: number, sz: number): V3 => [
      center[0] + sx * hx * c - sz * hz * s,
      center[1] + sy * hy,
      center[2] + sx * hx * s + sz * hz * c,
    ];
    const v000 = p(-1, -1, -1), v100 = p(1, -1, -1), v110 = p(1, 1, -1), v010 = p(-1, 1, -1);
    const v001 = p(-1, -1, 1), v101 = p(1, -1, 1), v111 = p(1, 1, 1), v011 = p(-1, 1, 1);
    this.quad(v000, v001, v011, v010, side, mat);
    this.quad(v100, v110, v111, v101, side, mat);
    this.quad(v000, v100, v101, v001, deepen(color, 0.24), mat);
    this.quad(v010, v011, v111, v110, top, mat);
    this.quad(v000, v010, v110, v100, color, mat);
    this.quad(v001, v101, v111, v011, color, mat);
  }

  ball(center: V3, radius: number, color: RGB, opts: BallOptions = {}): void {
    const sides = Math.max(3, Math.round(opts.sides ?? 6));
    const rings = Math.max(1, Math.round(opts.rings ?? 1));
    const mat = opts.mat ?? this.currentMat;
    const squash = opts.squashY ?? 1;
    const rot = opts.rotY ?? 0;
    const topCol = lighten(color, 0.12);
    const rowY = (level: number): number => {
      const t = (level + 1) / (rings + 1);
      return center[1] + Math.cos(Math.PI * t) * radius * squash;
    };
    const rowR = (level: number): number => Math.sin(Math.PI * ((level + 1) / (rings + 1))) * radius;
    const row = (level: number): V3[] => {
      const r = rowR(level);
      const y = rowY(level);
      const out: V3[] = [];
      for (let i = 0; i < sides; i++) {
        const a = rot + (i / sides) * TAU + (level % 2 ? Math.PI / sides : 0);
        out.push([center[0] + Math.cos(a) * r, y, center[2] + Math.sin(a) * r]);
      }
      return out;
    };
    const south: V3 = [center[0], center[1] - radius * squash, center[2]];
    const north: V3 = [center[0], center[1] + radius * squash, center[2]];
    const rows: V3[][] = [];
    for (let l = 0; l < rings; l++) rows.push(row(l));
    for (let i = 0; i < sides; i++) {
      const j = (i + 1) % sides;
      this.tri(rows[0][i], south, rows[0][j], color, mat);
    }
    for (let l = 0; l + 1 < rings; l++) {
      const col = mix(color, topCol, (l + 1) / rings);
      for (let i = 0; i < sides; i++) {
        const j = (i + 1) % sides;
        this.quad(rows[l][i], rows[l][j], rows[l + 1][j], rows[l + 1][i], col, mat);
      }
    }
    const top = rows[rings - 1];
    for (let i = 0; i < sides; i++) {
      const j = (i + 1) % sides;
      this.tri(top[i], top[j], north, topCol, mat);
    }
  }

  bake(geo: FlagGeometry, opts: BakeOptions = {}): void {
    const off = opts.offset ?? [0, 0, 0];
    const sc = opts.scale ?? 1;
    const rx = opts.rotX ?? 0;
    const rz = opts.rotZ ?? 0;
    const ry = opts.rotY ?? 0;
    const mat = opts.mat ?? this.currentMat;
    const mixT = opts.tint ? (opts.tintMix ?? 1) : 0;
    const triMat: FlagMaterial[] = [];
    for (const g of geo.groups) {
      const count = Math.round(g.count / 3);
      for (let i = 0; i < count; i++) triMat[Math.round(g.start / 3) + i] = g.mat;
    }
    for (let t = 0; t < geo.triangleCount; t++) {
      const i0 = geo.indices[t * 3];
      const i1 = geo.indices[opts.flip ? t * 3 + 2 : t * 3 + 1];
      const i2 = geo.indices[opts.flip ? t * 3 + 1 : t * 3 + 2];
      const p = (i: number): V3 => {
        let x = geo.positions[i * 3] * sc;
        let y = geo.positions[i * 3 + 1] * sc;
        let z = geo.positions[i * 3 + 2] * sc;
        if (rz !== 0) {
          const r = rotateZ(x, y, rz);
          x = r[0];
          y = r[1];
        }
        if (rx !== 0) {
          const r = rotateX(y, z, rx);
          y = r[0];
          z = r[1];
        }
        if (ry !== 0) {
          const r = rotateY(x, z, ry);
          x = r[0];
          z = r[1];
        }
        return [x + off[0], y + off[1], z + off[2]];
      };
      const a = p(i0);
      const b = p(i1);
      const c = p(i2);
      const nrm = faceNormal(a[0], a[1], a[2], b[0], b[1], b[2], c[0], c[1], c[2]);
      let col: RGB = [geo.colors[i0 * 3], geo.colors[i0 * 3 + 1], geo.colors[i0 * 3 + 2]];
      if (opts.tint) col = mix(col, opts.tint, mixT);
      this.push([...a, ...b, ...c], col, nrm, opts.mat ?? triMat[t] ?? mat, true);
    }
  }

  private track(x: number, y: number, z: number): void {
    const r = Math.hypot(x, z);
    if (r > this.maxR) this.maxR = r;
    if (y > this.maxY) this.maxY = y;
    if (y < this.minY) this.minY = y;
  }

  build(): FlagGeometry {
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
    const groups: { mat: FlagMaterial; start: number; count: number }[] = [];
    const ratio: Partial<Record<FlagMaterial, number>> = {};
    let v = 0;
    let idx = 0;
    for (const mat of FLAG_MATERIALS) {
      const start = idx;
      for (const f of this.faces) {
        if (f.mat !== mat) continue;
        const n = f.pos.length / 3;
        positions.set(f.pos, v * 3);
        const N = faceNormal(
          f.pos[0], f.pos[1], f.pos[2],
          f.pos[3], f.pos[4], f.pos[5],
          f.pos[6], f.pos[7], f.pos[8],
        );
        for (let i = 0; i < n; i++) {
          normals[(v + i) * 3] = N[0];
          normals[(v + i) * 3 + 1] = N[1];
          normals[(v + i) * 3 + 2] = N[2];
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
        ratio[mat] = (idx - start) / 3;
      }
    }
    const total = indexCount / 3 || 1;
    for (const key of Object.keys(ratio) as FlagMaterial[]) {
      ratio[key] = (ratio[key] ?? 0) / total;
    }
    return {
      positions, normals, colors, indices, groups,
      radius: Math.max(this.maxR, 1e-3),
      height: Math.max(this.maxY, 1e-3),
      bottom: Math.min(this.minY, 0),
      vertexCount,
      triangleCount: indexCount / 3,
      materialRatio: ratio,
    };
  }
}

export function scaleGeometry(geo: FlagGeometry, scale: number): void {
  if (scale === 1) return;
  for (let i = 0; i < geo.positions.length; i++) geo.positions[i] *= scale;
  geo.radius *= scale;
  geo.height *= scale;
  geo.bottom *= scale;
}
