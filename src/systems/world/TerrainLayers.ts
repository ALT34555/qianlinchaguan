import { createNoise3D, type NoiseFunction3D } from 'simplex-noise';
import { mulberry32 } from '../../core/math/Random';
import { CoverageLayer, type CoverageField, type SurfacePoint } from './CoverageLayer';

export const ELEVATION_TIERS = [512, 1024, 2048] as const;
export const clamp = (v: number, low = 0, high = 1) => Math.max(low, Math.min(high, v));
export const smooth = (v: number) => { const t = clamp(v); return t * t * (3 - 2 * t); };
export const unit = (p: SurfacePoint): SurfacePoint => { const length = Math.hypot(...p); return p.map(v => v / length) as SurfacePoint; };
export const spherePoint = (lon: number, lat: number): SurfacePoint => {
  const angle = Math.PI / 180, cos = Math.abs(lat) >= 90 ? 0 : Math.cos(lat * angle);
  return [Math.cos(lon * angle) * cos, Math.sin(lat * angle), Math.sin(lon * angle) * cos];
};

export interface TerrainSample {
  height: number; uplift: number; plateau: number; valley: number; rift: number; moisture: number;
  coverage: number; baseCoverage: number; largeLandmass: boolean; elevationTier: 1 | 2 | 3;
}

export class TerrainLayers {
  readonly coverage: CoverageLayer;
  readonly deformation: NoiseFunction3D;
  private readonly fields: NoiseFunction3D[];
  private readonly thirdRoll: number;

  constructor(seed: number, private readonly spherical: boolean, ratio: number, density?: CoverageField, fixedThreshold?: number) {
    this.fields = Array.from({length: 5}, (_, i) => createNoise3D(mulberry32(seed ^ (0x6c617965 + i * 0x9137))));
    this.deformation = this.fields[0];
    this.thirdRoll = mulberry32(seed ^ 0x74696572)();
    const probes: SurfacePoint[] = [];
    const rng = mulberry32(0x636f7665);
    for (let i = 0; i < 1536; i++) {
      if (spherical) {
        const y = 1 - 2 * (i + .5) / 1536, r = Math.sqrt(1 - y*y), angle = i * Math.PI * (3 - Math.sqrt(5));
        probes.push([r * Math.cos(angle), y, r * Math.sin(angle)]);
      } else probes.push([(rng() - .5) * 1000, 0, (rng() - .5) * 1000]);
    }
    this.coverage = new CoverageLayer(density ?? (p => this.field(p, 1, 0) + this.field(p, 2.6, 0, 31) * .12), ratio, probes, fixedThreshold);
  }

  field(p: SurfacePoint, frequency: number, channel = 0, offset = 0): number {
    return this.fields[channel](p[0] * frequency + 17.3 + offset, p[1] * frequency - 8.1,
      p[2] * frequency + 3.7);
  }

  islandCoverage(point: SurfacePoint, base = this.coverage.distance(point), activity = 0): number {
    if (this.coverage.ratio === 0 || this.coverage.ratio === 1 || base <= -.2 || base >= -.025) return base;
    const surge = Math.pow(Math.max(0, (activity - 5) / 4), 3);
    const noise = this.field(point, 10, 4) * .85 + this.field(point, 18, 4, -53) * .15;
    const shelf = smooth((base + .2) / .09) * (1 - smooth((base + .075) / .05));
    return base + (.25 + surge * .05) * shelf * smooth((noise - .7 + surge * .13) / .18);
  }

  private interior(point: SurfacePoint, base: number): number {
    if (base < .18) return 0;
    let core = base;
    const step = this.spherical ? .23 : .55;
    for (const [dx, dz] of [[step, 0], [-step, 0], [0, step], [0, -step]]) {
      const neighbour: SurfacePoint = [point[0] + dx, point[1], point[2] + dz];
      core = Math.min(core, this.coverage.distance(this.spherical ? unit(neighbour) : neighbour));
    }
    return smooth((core - .06) / .2);
  }

  sample(point: SurfacePoint, activity = 0, clippedCoverage?: number): TerrainSample {
    const baseCoverage = this.coverage.distance(point);
    const regional = this.field(point, 6, 2);
    const margin = clippedCoverage === undefined ? smooth((baseCoverage - .18) / .15) : smooth((clippedCoverage - .04) / .1);
    const interior = Math.min(this.interior(point, baseCoverage), margin), boost = activity / 9;
    const large = smooth((interior - .5) / .5);
    const second = large * smooth((this.field(point, .7, 3) - .52 + boost * .025) / .2);
    const thirdAllowed = !this.spherical || this.thirdRoll < .12 + boost * .06;
    const third = thirdAllowed ? large * smooth((this.field(point, .9, 3, 113) - .75 + boost * .02) / .15) : 0;
    const ceiling = ELEVATION_TIERS[0] + 512 * Math.max(second, third) + 1024 * third;
    const envelope = smooth((this.field(point, 2.1, 1) + .22) / .8);
    const main = (1 - Math.abs(this.field(point, 5, 1))) ** 3;
    const crossing = (1 - Math.abs(this.field(point, 8, 1, 43))) ** 4;
    const compression = Math.max((.65 * main + .35 * crossing) * envelope, second * .72, third * .9);
    const coverage = clippedCoverage ?? this.islandCoverage(point, baseCoverage, activity);
    const coast = smooth(coverage / .12);
    const plateau = smooth((regional - .25) / .4) * coast;
    const valley = smooth((-regional - .2) / .5) * coast;
    const rift = (1 - smooth(Math.abs(this.field(point, 4, 2, 90)) / .09)) * envelope * coast;
    const baseHeight = (48 * (1 - Math.exp(-Math.max(0, coverage) / .12)) + regional * 12 +
      90 * envelope + plateau * 38 - valley * 10 - rift * 18) * coast;
    const uplift = Math.max(0, ceiling - baseHeight) * compression * coast;
    return {height: coverage <= 0 ? Math.max(-160, coverage * 240) : baseHeight + uplift,
      uplift, plateau, valley, rift, moisture: this.field(point, 8, 2, 79), coverage, baseCoverage,
      largeLandmass: interior > .5, elevationTier: third > 0 ? 3 : second > 0 ? 2 : 1};
  }
}
