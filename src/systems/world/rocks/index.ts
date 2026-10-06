/** 岩石系统入口：逐个列出导出，rolldown 打包更稳 */

export {
  ROCK_COLOR_SLOTS, ROCK_FORMS, ROCK_FORM_CODES, ROCK_MATERIALS, ROCK_MATERIAL_FAMILIES,
  ROCK_SHAPE_TYPES, SEASONS, rockFormOfCode,
} from './types';
export type {
  RockDef, RockFile, RockForm, RockFormDef, RockFormTable, RockMaterialFamily, RockMaterial,
  RockShapeType, RockParams, RockPalette, RockPaletteEntry, RockPaletteFile, RockPaletteKey,
  RockColorSlot, RockBuildDef, RockBuildOptions, ResolvedRockPalette, Season,
} from './types';

export {
  RockBuilder, clamp, clamp01, deepen, faceNormal, hexToRgb, isRockMaterial, lighten, mix, shadeOf,
} from './geometry';
export type { CrustSpec, FacePush, RGB, RockFace, RockGeometry, V3 } from './geometry';

export {
  FAMILY_PALETTE, ROCK_ARCHETYPES, ROCK_ARCHETYPE_DEFAULTS, bakeCone, buildRock,
} from './archetypes';
export type { RockArchetypeName } from './archetypes';

export {
  ROCK_CHUNK_IDS, ROCK_DEFS, ROCK_FILES, ROCK_GLOBAL_DEFAULTS, ROCK_IDS,
  ROCK_PALETTES, ROCKS_BY_CHUNK, Rock, buildAllForms, buildRockById, defaultCrustKey, formMatrixOf,
  getRockDef, mergeRockParams, resolveRockForm, resolveRockPalette, rocksByTag, rocksForChunk,
  scaleGeometry, statOf,
} from './Rocks';
export { CONTENT_DATA_VERSION } from '../../../core/version';
export type { ResolvedRockForm, RockStat, RockUnitDef } from './Rocks';

export { canHostRock, rockQuery, rocksAt, scatterRocks } from './placement';
export type { RockPlacement, SurfaceQuery } from './placement';
