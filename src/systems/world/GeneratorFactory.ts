import { PlanetMacroGenerator } from './planet-macro/PlanetMacroGenerator';
import { DEFAULT_GENERATION, normalizeGeneration, type ClimateWeights, type WorldGeneration } from './WorldSettings';
import type { WatershedState } from './Watershed';
import type { WorldGenerator, RainWorld } from './WorldContract';

/** 世界生成器工厂入口，详见同名文档 */
export function createWorldGenerator(seed:number,weights:ClimateWeights,generation:WorldGeneration=DEFAULT_GENERATION,
  options:{state?:WatershedState;defer?:boolean}={}):WorldGenerator {
  const normalized=normalizeGeneration(generation);
  const generator=new PlanetMacroGenerator(seed,weights,normalized);
  if(options.state){if(options.defer)generator.deferHydrology(options.state);else generator.adoptHydrology(options.state);}
  return generator;
}

export function prepareGeneratorHydrology(generator:WorldGenerator):void {
  generator.prepareMacroHydrology();
}
/** 逐日降水的世界输入，非宏观星球返回空 */
export function precipitationWorld(generator:WorldGenerator):RainWorld|null {
  return generator.generation.terrainRule==='planet-macro'?generator.precipitationWorld:null;
}
