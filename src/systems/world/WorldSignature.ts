import { GENERATOR_VERSION, CONTENT_DATA_VERSION } from '../../core/version';
import type { ClimateWeights, WorldGeneration } from './WorldSettings';
export function worldSignature(options: {seed:number;climateWeights:ClimateWeights;generation:WorldGeneration}): string {
  const {mode,planet,landRatio,precipitation}=options.generation;
  return JSON.stringify([GENERATOR_VERSION,CONTENT_DATA_VERSION,options.seed,options.climateWeights,{mode,planet,landRatio,precipitation}]);
}
