import { createNoise2D } from 'simplex-noise';
import { mulberry32 } from '../../core/math/Random';
import { planetCoordinates, temperatureClimate, type ClimateWeights, type WorldGeneration } from './WorldSettings';

export class ClimateLayer {
  private readonly warp;
  private readonly phase;

  constructor(seed: number, private readonly weights: ClimateWeights, private readonly generation: WorldGeneration) {
    const rng = mulberry32(seed ^ 0x636c696d);
    this.warp = createNoise2D(rng);
    this.phase = rng() * 128;
  }

  sample(cx: number, cz: number): {index: number; temperature: number} {
    if (this.generation.mode === 'planet') {
      const p = this.generation.planet;
      const {latitude} = planetCoordinates(cx, cz, p.equatorChunks);
      const blend = Math.max(0, Math.cos(latitude * Math.PI / 180)) ** 1.4;
      const temperature = p.poleTemperature + (p.equatorTemperature - p.poleTemperature) * blend;
      return {index: temperatureClimate(temperature), temperature};
    }
    const latitude = cz + this.phase + 5 * this.warp(cx / 25, cz / 32);
    const cycle = ((latitude % 128) + 128) % 128;
    const position = Math.min(1 - Number.EPSILON, 1 - Math.abs(cycle - 64) / 64);
    let cumulative = 0, index = 0;
    for (let i = 0; i < this.weights.length; i++) {
      cumulative += this.weights[i];
      if (this.weights[i] > 0 && position < cumulative) { index = i; break; }
    }
    return {index, temperature: 38 - 58 * position};
  }
}
