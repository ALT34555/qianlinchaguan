export type SurfacePoint = [number, number, number];
export type CoverageField = (point: SurfacePoint) => number;

/** 通用实地/空域覆盖场，不负责水、虚空或地表材质。 */
export class CoverageLayer {
  readonly threshold: number;

  constructor(readonly field: CoverageField, readonly ratio: number, probes: readonly SurfacePoint[], fixedThreshold?: number) {
    const values = probes.map(field).sort((a, b) => a - b);
    this.threshold = fixedThreshold ?? values[Math.min(values.length - 1, Math.floor((1 - ratio) * values.length))];
  }

  distance(point: SurfacePoint): number {
    if (this.ratio === 0) return -1;
    if (this.ratio === 1) return 1;
    return this.field(point) - this.threshold;
  }
}
