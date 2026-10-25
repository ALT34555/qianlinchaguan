import { CURRENT_RULE_ID } from '../../core/WorldRule';
import { CONTENT_DATA_VERSION } from '../../core/version';
import type { ClimateWeights, WorldGeneration } from './WorldSettings';

/** 世界签名缓存键，详见同名文档 */
export function worldSignature(options: {seed:number;climateWeights:ClimateWeights;generation:WorldGeneration}): string {
  const {mode,planet,landRatio,precipitation,terrainRule,macroColumns,macroTerrain,macroClimate}=options.generation;
  const artificial=options.generation.artificial;
  const flagCount=artificial?.enabled?Math.min(32,artificial.initialCivilizationLimit??8):0;
  const rule={terrainRule,macroColumns,macroTerrain,macroClimate,flagCount,initialSites:2};
  return JSON.stringify([CURRENT_RULE_ID,CONTENT_DATA_VERSION,options.seed,options.climateWeights,{mode,planet,landRatio,precipitation,...rule}]);
}
