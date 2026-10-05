/**
 * 植被系统入口。
 *
 * 植物资源与低模生成：
 *   · 植物参数表  content/data/world/plants/*.json
 *   · 低模生成     geometry.ts + archetypes.ts（纯几何，可在 Worker 内运行）
 *   · 注册表       Plants.ts
 *   · 散布判定     placement.ts
 *
 * 典型用法：
 *   import { buildPlantById, Plant } from './vegetation';
 *   const geo = buildPlantById(Plant['tree.temp.broadleaf_oak'], 'summer');
 *   // 开花版本（花色默认 #a8456b，可用 bloomColor 覆盖；针叶树不开花）
 *   const flowered = buildPlantById(Plant['tree.temp.broadleaf_oak'], 'spring', { bloom: true });
 */
export * from './types';
export { LowPolyBuilder, parseColor, SHAPE_TYPES, shapeMaterial } from './geometry';
export type { Face, PlantGeometry } from './geometry';
export { ARCHETYPES, ARCHETYPE_DEFAULTS, CLIMATE_DEFAULTS, GLOBAL_DEFAULTS, buildPlant } from './archetypes';
export {
  BLOOM_ARCHETYPES, PALETTES, PLANT_FILES, PLANT_DATA_VERSION, PLANT_IDS, PLANT_VARIANTS, Plant,
  bloomColorOf, buildOptions, buildPlantById, canBloom, getPlantVariant, mergeParams,
  plantsByClimate, plantsByTag, resolvePalette, scaleGeometry, statOf, toPlantDef,
} from './Plants';
export type { PlantStat } from './Plants';
export { canHostPlant, chunkQuery, scatterPlants } from './placement';
export type { PlantPlacement, SurfaceQuery } from './placement';
