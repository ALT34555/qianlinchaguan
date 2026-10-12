export interface SurfaceQuery {
  /** 未加载区域返回 NaN */
  height(x: number, z: number): number;
  /** 未加载区域返回 NaN */
  waterLevel(x: number, z: number): number;
  /** 未加载区域返回 0 */
  surface(x: number, z: number): number;
}
