import type { NoiseFunction3D } from 'simplex-noise';
import { mulberry32 } from '../../core/math/Random';
import type { CoverageField, SurfacePoint } from './CoverageLayer';
import {unit, smooth} from './TerrainLayers';

type Vector = [number, number, number];
interface Boundary { normal: Vector; offset: number }
interface Fragment { center: Vector; bias: number; inverse: number[]; boundaries: Boundary[]; chip: boolean }
const dot = (a: Vector, b: Vector) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const cross = (a: Vector, b: Vector): Vector => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
const rotationMatrix = ([x, y, z]: Vector, angle: number): number[] => {
  const c = Math.cos(angle), s = Math.sin(angle), k = 1 - c;
  return [c + x*x*k, x*y*k - z*s, x*z*k + y*s,
    y*x*k + z*s, c + y*y*k, y*z*k - x*s,
    z*x*k - y*s, z*y*k + x*s, c + z*z*k];
};
const multiply = (a: number[], b: number[]) => Array.from({length: 9}, (_, i) => {
  const row = Math.floor(i / 3) * 3, col = i % 3;
  return a[row] * b[col] + a[row + 1] * b[col + 3] + a[row + 2] * b[col + 6];
});

/** 原始大陆先划成有限的大块与沿岸碎块，再让整块沿球面漂移。 */
export class ContinentFragments {
  private readonly fragments: Fragment[] = [];
  private readonly opening: number;
  private readonly warp: number;

  constructor(axis: Vector, private readonly noise: NoiseFunction3D, seed: number, strength: number) {
    const rng = mulberry32(seed ^ 0x66726163);
    const t = Math.max(0, Math.min(1, strength / 6));
    const spread = t * t * (3 - 2 * t);
    this.opening = spread * .035;
    this.warp = spread * .22 + smooth(t * 3) * .05;
    const u = unit(cross(axis, Math.abs(axis[1]) < .9 ? [0, 1, 0] : [1, 0, 0]));
    const v = cross(axis, u);
    const direction = (angle: number): Vector => u.map((x, i) => x * Math.cos(angle) + v[i] * Math.sin(angle)) as Vector;
    const add = (radius: number, angle: number, bias: number, drift: number, chip = false) => {
      const outward = direction(angle);
      const center = axis.map((x, i) => x * Math.cos(radius) + outward[i] * Math.sin(radius)) as Vector;
      const rotation = unit(cross(axis, direction(angle + (rng() - .5) * .6)));
      // Rodrigues 逆旋转：查询漂移后的位置时，回到原始大陆寻找对应整块。
      const inverse = multiply(rotationMatrix(center, (rng() - .5) * 1.1 * spread),
        rotationMatrix(rotation, -drift * spread));
      this.fragments.push({center, bias, inverse, boundaries: [], chip});
    };
    // 随机中心保持基本间距，避免主要块挤成细片。
    for (let i = 0; i < 7; i++) {
      let radius = 0, angle = 0, separation = -Infinity;
      for (let candidate = 0; candidate < (i === 0 ? 1 : 12); candidate++) {
        const r = Math.acos(1 - rng() * (1 - Math.cos(1.08))), a = rng() * Math.PI * 2;
        const d = direction(a);
        const center = axis.map((x, j) => x * Math.cos(r) + d[j] * Math.sin(r)) as Vector;
        const nearest = i === 0 ? 0 : Math.min(...this.fragments.map(f => 1 - dot(center, f.center)));
        if (nearest > separation) { radius = r; angle = a; separation = nearest; }
      }
      add(radius, angle, (rng() - .5) * .07, .18 + radius * .45 + rng() * .18);
    }
    // 负权重限制沿岸碎块的面积。
    for (let i = 0; i < 5; i++) {
      add(1.12 + rng() * .14, rng() * Math.PI * 2, -.055 - rng() * .025, 1.25 + rng() * .35, true);
    }
    for (const a of this.fragments) for (const b of this.fragments) {
      if (a === b) continue;
      const difference = a.center.map((x, i) => x - b.center[i]) as Vector;
      const length = Math.hypot(...difference);
      a.boundaries.push({normal: difference.map(x => x / length) as Vector, offset: (a.bias - b.bias) / length});
    }
  }

  private field(p: Vector, frequency: number, offset = 0): number {
    return this.noise(p[0] * frequency + 17.3 + offset, p[1] * frequency - 8.1, p[2] * frequency + 3.7);
  }

  map(world: SurfacePoint, density: CoverageField): {point: SurfacePoint; distance: number} {
    let coast = -Infinity;
    let point = world;
    for (const fragment of this.fragments) {
      const m = fragment.inverse;
      const source: Vector = [m[0]*world[0] + m[1]*world[1] + m[2]*world[2],
        m[3]*world[0] + m[4]*world[1] + m[5]*world[2], m[6]*world[0] + m[7]*world[1] + m[8]*world[2]];
      let distance = density(source);
      // 沿岸碎块有独立尺寸上限，避免把大陆外沿整条切成细长的环带。
      if (fragment.chip) distance = Math.min(distance, (dot(source, fragment.center) - Math.cos(.2)) * 4.3);
      if (distance <= coast) continue;
      // 所有陆块共用原始坐标中的弯曲场，裂纹彼此吻合；两尺度扰动产生湾口和凸角。
      const bend = (offset: number) => this.field(source, 2.4, offset) + this.field(source, 5.2, offset + 61) * .23;
      const warped = unit([source[0] + bend(23) * this.warp,
        source[1] + bend(-31) * this.warp,
        source[2] + bend(47) * this.warp]);
      for (const boundary of fragment.boundaries) {
        distance = Math.min(distance, dot(warped, boundary.normal) + boundary.offset - this.opening);
        if (distance <= coast) break;
      }
      if (distance > coast) { coast = distance; point = source; }
    }
    return {point, distance: coast};
  }
}
