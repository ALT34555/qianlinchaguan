import { createNoise3D, type NoiseFunction3D } from 'simplex-noise';
import { mulberry32 } from '../../core/math/Random';
import { CoverageLayer, type CoverageField, type SurfacePoint } from './CoverageLayer';
import {plateUplift, PLATE_BASES, type PlateTier} from './PlateUplift';

export const ELEVATION_TIERS = PLATE_BASES;
export const CONTINENT_SCALE = 512;
/** 常规气温 14°C 时从 1024 开始积雪 */
export const snowLine = (temperature: number): number => Math.max(0, 1024 + (temperature - 14) * 32);
export const altitudeSnow = (height: number, temperature: number): number => smooth((height - snowLine(temperature)) / 96);
export const SNOW_MARGIN = 64;
export const freezeState = (height:number,temperature:number):'liquid'|'margin'|'frozen' =>
  height>=snowLine(temperature)?'frozen':height>=snowLine(temperature)-SNOW_MARGIN?'margin':'liquid';
export const clamp = (v: number, low = 0, high = 1) => Math.max(low, Math.min(high, v));
export const smooth = (v: number) => { const t = clamp(v); return t * t * (3 - 2 * t); };
export const unit = (p: SurfacePoint): SurfacePoint => { const length = Math.hypot(...p); return p.map(v => v / length) as SurfacePoint; };
export const spherePoint = (lon: number, lat: number): SurfacePoint => {
  const angle = Math.PI / 180, cos = Math.abs(lat) >= 90 ? 0 : Math.cos(lat * angle);
  return [Math.cos(lon * angle) * cos, Math.sin(lat * angle), Math.sin(lon * angle) * cos];
};

export interface TerrainSample {
  height: number; uplift: number; plateau: number; valley: number; rift: number; moisture: number;
  coverage: number; baseCoverage: number; largeLandmass: boolean; elevationTier: PlateTier;
  /** 板块底座与其上的相对地貌分别存储 */
  plateBase: number; plateEdge: number; hillRelief: number; highlandRelief: number;
}

export class TerrainLayers {
  readonly coverage: CoverageLayer;
  readonly deformation: NoiseFunction3D;
  private readonly fields: NoiseFunction3D[];

  constructor(seed: number, spherical: boolean, ratio: number, density?: CoverageField, fixedThreshold?: number) {
    this.fields = Array.from({length: 5}, (_, i) => createNoise3D(mulberry32(seed ^ (0x6c617965 + i * 0x9137))));
    this.deformation = this.fields[0];
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

  sample(point: SurfacePoint, activity = 0, clippedCoverage?: number): TerrainSample {
    // 第二层
    const baseCoverage = this.coverage.distance(point);
    // 第三层
    const signal=this.field(point,1.4,3)*.8+this.field(point,.55,3,113)*.2;
    // 覆盖距离来自第二层现成数据
    const plate=plateUplift(Math.min(signal+activity/9*.055,.14+Math.max(0,clippedCoverage??baseCoverage)));
    // 第四层
    const coverage = clippedCoverage ?? this.islandCoverage(point, baseCoverage, activity);
    const regional = this.field(point, 6, 2);
    const coast = smooth(coverage / .12);
    const plateau = smooth((regional - .25) / .4) * coast;
    const valley = smooth((-regional - .2) / .5) * coast;
    const ridge=(1-Math.abs(this.field(point,12,1)))**4;
    const isolated=smooth((this.field(point,4,1,123)-.35)/.3);
    const mountain=Math.max(plate.edge,isolated);
    const hillRelief=mountain>.05?(16+240*ridge)*mountain:0;
    const highlandRelief=plateau>.05?(16+112*smooth((regional+.1)/.8))*plateau:0;
    const rift = (1 - smooth(Math.abs(this.field(point, 4, 2, 90)) / .09)) * coast;
    const plainBase=48+48*smooth(Math.max(0,coverage)/.65)+regional*16-valley*16-rift*24;
    const uplift=plate.base*coast;
    return {height:coverage<=0?this.seabed(coverage,point):(plainBase+plate.base+hillRelief+highlandRelief)*coast,
      uplift,plateau,valley,rift,moisture:this.field(point,1.1,2,79)*.85+this.field(point,6,2,79)*.15,
      coverage,baseCoverage,largeLandmass:baseCoverage>.4,elevationTier:coverage>0?plate.tier:0,
      plateBase:plate.base,plateEdge:plate.edge,hillRelief,highlandRelief};
  }

  /** 海床：陆架不动，深海叠加丘陵/海岭/海山/海沟 */
  private seabed(coverage: number, point: SurfacePoint): number {
    const base = Math.max(-160, coverage * 240);
    // 近岸不动，海岸线与沿岸汇水与旧版逐字一致
    const room = Math.max(0, Math.min(1, (-base - 6) / 96));
    if (!room) return base;
    const t = clamp(-coverage / .667), open = smooth(t / .3);
    const hill = this.field(point, 9, 1) * .62 + this.field(point, 22, 1, 61) * .38;
    const ridge = (1 - Math.abs(this.field(point, 7, 4, 29))) ** 2;
    const seamount = smooth((this.field(point, 5.5, 4, 97) - .45) / .2);
    const trench = smooth((-.58 - this.field(point, 4.2, 3, 211)) / .18);
    const rise = hill * (4 + 16 * t) + ridge * 14 * t + seamount * (30 + 70 * t);
    return base + open * room * (rise - trench * 55 * smooth((t - .1) / .3));
  }
}
