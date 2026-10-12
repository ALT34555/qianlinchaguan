import { createNoise3D, type NoiseFunction3D } from 'simplex-noise';
import { mulberry32 } from '../../core/math/Random';
import type { SurfacePoint } from './CoverageLayer';

export const MOUNTAIN_LIFT_THRESHOLD = .14;
export const MOUNTAIN_MIN_RELIEF = 32;
const clamp = (v: number) => Math.max(0, Math.min(1, v));
const fade = (v: number) => { const t = clamp(v); return t*t*t*(t*(t*6-15)+10); };
export const mountainProfile = (distance: number, core: number, skirt: number): number =>
  .68 * (1 - fade(Math.abs(distance) / core)) + .32 * (1 - fade(Math.abs(distance) / skirt));

export interface MountainFieldInput {
  point: SurfacePoint;
  base: number;
  coast: number;
}

export interface MountainSample {
  core: number;
  band: number;
  relief: number;
  distance: number;
}

export class MountainField {
  private readonly fields: NoiseFunction3D[];
  private readonly frequency: number;

  constructor(seed: number, blocksPerUnit = 512 * 64) {
    this.fields = Array.from({length: 5}, (_, i) => createNoise3D(mulberry32(seed ^ (0x6d6f756e + i * 0x9e3779))));
    this.frequency = Math.max(.8, blocksPerUnit / (256 * 64));
  }

  sample(input: MountainFieldInput): MountainSample {
    const {point, base, coast} = input;
    const envelope = fade(base / 256) * fade((1792 - base) / 768) * fade((coast - .08) / .5);
    const core = 768 + 512 * fade(base / 1024);
    if (envelope <= 0) return {core, band: 0, relief: 0, distance: 0};
    const f = this.frequency;
    const warp = this.field(point, f * .4, 3) * .16;
    const spine = this.field(point, f, 0) + warp;
    const distance = spine * 6400;
    const asymmetric = distance < 0 ? .8 : 1.25;
    const main = mountainProfile(distance, core * asymmetric, core * 3.5 * asymmetric);
    const branchDistance = (this.field(point, f * 2.7, 1) + spine * .45) * 3600;
    const branch = mountainProfile(branchDistance, core * .38, core * 1.4) *
      (1 - fade(Math.abs(distance) / (core * 4.5))) * .52;
    const band = 1 - (1 - main) * (1 - branch);
    const crest = .4 + .6 * fade((this.field(point, f * .65, 2) + 1) * .5);
    const budget = 352 + 400 * fade(base / 1024);
    return {core, band: band * envelope, relief: budget * crest * band * envelope, distance};
  }

  private field(p: SurfacePoint, frequency: number, channel: number): number {
    return this.fields[channel](p[0] * frequency + 17.3, p[1] * frequency - 8.1, p[2] * frequency + 3.7);
  }
}
