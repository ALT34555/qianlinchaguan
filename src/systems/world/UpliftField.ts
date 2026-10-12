import type { SurfacePoint } from './CoverageLayer';

/** 整数键可覆盖的格点索引范围 */
const INDEX_LIMIT = 512;
/** 整数键的每轴跨度 */
const INDEX_SIDE = INDEX_LIMIT * 2 + 1;
/** 格点值缓存上限（与原实现一致） */
const CACHE_LIMIT = 16384;

/** 按球面格点插值的抬升场：整数键缓存 + 免分配插值 */
export class UpliftField {
  private readonly cache = new Map<number, number>();
  private readonly overflow = new Map<string, number>();
  /** 复用的插值权重与格点索引，避免逐次分配 */
  private readonly weights = new Float64Array(12);
  private readonly lattice = new Int32Array(3);
  readonly spacing: number;

  constructor(private readonly spherical: boolean, blocksPerUnit: number,
    private readonly source: (point: SurfacePoint, activity: number) => number) {
    this.spacing = Math.min(48 * 64 / blocksPerUnit, spherical ? .12 : Infinity);
  }

  private pointAt(ix: number, iy: number, iz: number): SurfacePoint {
    const p: SurfacePoint = [ix * this.spacing, iy * this.spacing, iz * this.spacing];
    if (!this.spherical) return p;
    const length = Math.hypot(p[0], p[1], p[2]);
    return length > 0 ? [p[0] / length, p[1] / length, p[2] / length] : [1, 0, 0];
  }

  /** 取格点值：常见范围走整数键，越界退回字符串键 */
  private at(activity: number, ix: number, iy: number, iz: number): number {
    if (ix < -INDEX_LIMIT || ix > INDEX_LIMIT || iy < -INDEX_LIMIT || iy > INDEX_LIMIT ||
        iz < -INDEX_LIMIT || iz > INDEX_LIMIT) return this.overflowAt(activity, ix, iy, iz);
    // 键含 activity，避免不同活动度共用同一格点值
    const key = ((activity * INDEX_SIDE + (ix + INDEX_LIMIT)) * INDEX_SIDE + (iy + INDEX_LIMIT)) * INDEX_SIDE + (iz + INDEX_LIMIT);
    const cached = this.cache.get(key);
    if (cached !== undefined) return cached;
    const value = this.source(this.pointAt(ix, iy, iz), activity);
    if (this.cache.size >= CACHE_LIMIT) this.cache.delete(this.cache.keys().next().value!);
    this.cache.set(key, value);
    return value;
  }

  private overflowAt(activity: number, ix: number, iy: number, iz: number): number {
    const key = `${activity}:${ix},${iy},${iz}`;
    const cached = this.overflow.get(key);
    if (cached !== undefined) return cached;
    const value = this.source(this.pointAt(ix, iy, iz), activity);
    if (this.overflow.size >= CACHE_LIMIT) this.overflow.delete(this.overflow.keys().next().value!);
    this.overflow.set(key, value);
    return value;
  }

  sample(point: SurfacePoint, activity: number): number {
    const w = this.weights, lattice = this.lattice;
    for (let axis = 0; axis < 3; axis++) {
      // 权重写法与逐点版逐字对应，保证浮点结果一致
      const v = point[axis] / this.spacing, i = Math.floor(v), t = v - i;
      lattice[axis] = i;
      const base = axis * 4;
      w[base] = (1 - t) ** 3 / 6;
      w[base + 1] = (3 * t ** 3 - 6 * t * t + 4) / 6;
      w[base + 2] = (-3 * t ** 3 + 3 * t * t + 3 * t + 1) / 6;
      w[base + 3] = t ** 3 / 6;
    }
    const ix0 = lattice[0], iy0 = lattice[1], iz0 = lattice[2];
    const rows = this.spherical ? 4 : 1;
    let result = 0;
    for (let x = 0; x < 4; x++) for (let z = 0; z < 4; z++) {
      for (let y = 0; y < rows; y++) {
        const value = this.at(activity, ix0 + x - 1, this.spherical ? iy0 + y - 1 : 0, iz0 + z - 1);
        result += value * w[x] * w[8 + z] * (this.spherical ? w[4 + y] : 1);
      }
    }
    return result;
  }
}
