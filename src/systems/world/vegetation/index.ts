/** 植被系统入口 */
export * from './types';
export { LowPolyBuilder, parseColor, SHAPE_TYPES, shapeMaterial } from './geometry';
export type { Face, PlantGeometry } from './geometry';
export { ARCHETYPES, ARCHETYPE_DEFAULTS, CLIMATE_DEFAULTS, GLOBAL_DEFAULTS, buildPlant, buildPetalGeometry } from './archetypes';
export type { PetalShapeSpec } from './archetypes';
export {
  BLOOM_ARCHETYPES, FRUIT_ARCHETYPES, PALETTES, PLANT_FILES, PLANT_DATA_VERSION, PLANT_IDS,
  PLANT_SPECIES_IDS, PLANT_VARIANTS, STATE_DEFAULTS, Plant,
  baseIdOf, bloomColorOf, buildAllStates, buildOptions, buildPlantById, canBloom, canFruit, effectiveState,
  getPlantVariant, mergeParams, modeledStatesOf, plantStatesOf, plantsByClimate, plantsByTag, resolvePalette,
  scaleGeometry, scatterablePlants, stateDef, stateMatrixOf, statOf, supportsState, toPlantDef,
} from './Plants';
export type { PlantStat } from './Plants';
export { canHostPlant, chunkQuery, scatterPlants } from './placement';
export type { PlantPlacement, SurfaceQuery } from './placement';
