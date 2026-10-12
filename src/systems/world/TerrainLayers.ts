import { createNoise3D, type NoiseFunction3D } from 'simplex-noise';
import { mulberry32 } from '../../core/math/Random';
import { CoverageLayer, type CoverageField, type SurfacePoint } from './CoverageLayer';
import {plateUplift, PLATE_BASES, type PlateTier} from './PlateUplift';
import { MountainField } from './MountainField';
import { UpliftField } from './UpliftField';

export const ELEVATION_TIERS = PLATE_BASES;
export const CONTINENT_SCALE = 512;
/** 谷地与裂谷的下陷量（方块） */
const VALLEY_SINK = 6;
const RIFT_SINK = 20;
/** 常规气温 14°C 时从 1024 开始积雪 */
export const snowLine = (temperature: number): number => Math.max(0, 1024 + (temperature - 14) * 32);
export const altitudeSnow = (height: number, temperature: number): number => smooth((height - snowLine(temperature)) / 96);
export const SNOW_MARGIN = 64;
export const freezeState = (height:number,temperature:number):'liquid'|'margin'|'frozen' =>
  height>=snowLine(temperature)?'frozen':height>=snowLine(temperature)-SNOW_MARGIN?'margin':'liquid';
export const clamp = (v: number, low = 0, high = 1) => Math.max(low, Math.min(high, v));
export const mix = (a: number, b: number, t: number) => a + (b - a) * t;
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
  /** 抬升区边缘山带相对底座的加高 */
  mountainRelief: number;
}

export class TerrainLayers {
  readonly coverage: CoverageLayer;
  readonly deformation: NoiseFunction3D;
  private readonly fields: NoiseFunction3D[];
  private readonly mountains: MountainField;
  private readonly upliftField: UpliftField;

  constructor(seed: number, spherical: boolean, ratio: number, density?: CoverageField, fixedThreshold?: number, blocksPerUnit = CONTINENT_SCALE * 64) {
    this.fields = Array.from({length: 6}, (_, i) => createNoise3D(mulberry32(seed ^ (0x6c617965 + i * 0x9137))));
    this.mountains = new MountainField(seed, blocksPerUnit);
    this.upliftField = new UpliftField(spherical, blocksPerUnit, (p, activity) =>
      plateUplift(this.field(p, 1.4, 3) * .8 + this.field(p, .55, 3, 113) * .2, activity).base);
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

  sample(point: SurfacePoint, activity = 0, _temperature = 14, clippedCoverage?: number,heightOnly=false): TerrainSample {
    // 第二层
    const baseCoverage = this.coverage.distance(point);
    // 第三层
    const signal=heightOnly?0:this.field(point,1.4,3)*.8+this.field(point,.55,3,113)*.2;
    // 覆盖距离来自第二层现成数据
    const rawPlate=heightOnly?{tier:0 as PlateTier,edge:0}:plateUplift(Math.min(signal+activity/9*.055,.14+Math.max(0,clippedCoverage??baseCoverage)));
    const base = this.upliftField.sample(point, activity) * smooth(Math.max(0,clippedCoverage??baseCoverage) / .18);
    const plate = {...rawPlate, base};
    // 第四层
    const coverage = clippedCoverage ?? this.islandCoverage(point, baseCoverage, activity);
    const regional = this.field(point, 6, 2);
    const coast = smooth(coverage / .12);
    const plateau = smooth((regional - .25) / .4) * coast;
    const valley = smooth((-regional - .2) / .5) * coast;
    const ridge=(1-Math.abs(this.field(point,12,1)))**4;
    const isolated=smooth((this.field(point,4,1,123)-.35)/.3);
    const mountain=isolated;
    const hillRelief=(16+240*ridge)*mountain;
    const highlandRelief=(16+112*smooth((regional+.1)/.8))*plateau;
    const rift = this.riftMask(point) * coast;
    const mountainRelief = this.mountains.sample({point, base: plate.base, coast}).relief;
    // 谷地与裂谷在底座之上真实下陷
    const plainBase=48+48*smooth(Math.max(0,coverage)/.65)+regional*16-valley*16-rift*24
      - this.sink(valley, VALLEY_SINK, .35, .8) - this.sink(rift, RIFT_SINK, .4, .16);
    const uplift=plate.base*coast;
    return {height:coverage<=0?this.seabed(coverage,point):(plainBase+plate.base+hillRelief+highlandRelief+mountainRelief)*coast,
      uplift,plateau,valley,rift,moisture:heightOnly?0:this.field(point,1.1,2,79)*.85+this.field(point,6,2,79)*.15,
      coverage,baseCoverage,largeLandmass:baseCoverage>.4,elevationTier:coverage>0?plate.tier:0,
      plateBase:plate.base,plateEdge:plate.edge,hillRelief,highlandRelief,mountainRelief};
  }

  /** 下陷量：低于起点不下陷，边缘导数归零 */
  private sink(mask: number, depth: number, start: number, span: number): number {
    if (!depth || mask <= start) return 0;
    const bowl = Math.min(1, (mask - start) / span);
    return depth * (bowl * bowl * (3 - 2 * bowl)) ** 1.2;
  }

  /** 裂谷面：断层台阶与错位谷底，形成撕裂状边界 */
  private riftMask(point: SurfacePoint): number {
    const band = 1 - smooth(Math.abs(this.field(point, 4, 2, 90)) / .09);
    if (band <= .05) return 0;
    const wall = this.field(point, .9, 4, 17), floor = this.field(point, 7, 1, 55);
    const terrace = 1 - Math.abs(Math.round(wall * 2.4) / 2.4 - wall) * 1.1;
    const tear = smooth((Math.abs(floor) - .1) / .5);
    return band * (.74 + .26 * tear) * terrace;
  }

  /** 海床：陆架不动，深海叠加丘陵/海岭/海山/海沟 */
  private seabed(coverage: number, point: SurfacePoint): number {
    const base = Math.max(-160, coverage * 240);
    // 保持近岸深度与海岸线稳定
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
