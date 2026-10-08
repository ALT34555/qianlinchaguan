/** 旗帜与旗杆系统入口 */

export {
  FLAG_CHARGE_KINDS, FLAG_COLOR_SLOTS, FLAG_MATERIALS, FLAG_MOUNTS, FLAG_ORIENTATIONS,
  FLAG_SHAPE_CODES, FLAG_SHAPES, FLAGPOLE_BASE_STYLES, FLAGPOLE_COLLAR_STYLES,
  FLAGPOLE_FINIAL_STYLES, FLAGPOLE_KINDS, FLAGPOLE_PLATE_STYLES, flagShapeOfCode,
} from './types';
export type {
  FlagAssemblyOptions, FlagAttachment, FlagBuildOptions, FlagChargeContext, FlagChargeKind,
  FlagChargePainter, FlagChargeSpec, FlagClothDef, FlagClothSurface, FlagDef, FlagEmblemDef,
  FlagEmblemFile, FlagFaceSink, FlagFile, FlagMaterial, FlagMount, FlagMountDef, FlagOrientation,
  FlagPalette, FlagPaletteEntry, FlagPaletteFile, FlagParams, FlagPlacement, FlagShape,
  FlagWaveSpec, FlagpoleArmResolved, FlagpoleArmSpec, FlagpoleBaseSpec, FlagpoleBuildDef,
  FlagpoleBuildOptions, FlagpoleCollarSpec, FlagpoleDef, FlagpoleFile, FlagpoleFinialSpec,
  FlagpoleKind, FlagpoleParams, FlagpolePlateSpec, RGB, ResolvedFlagCharge, ResolvedFlagPalette,
  ResolvedFlagWave, ResolvedFlagpoleParams, SurfaceQuery, V3,
} from './types';

export {
  FlagBuilder, addV3, clamp, clamp01, crossV3, deepen, faceNormal, hexToRgb, lerpV3, lighten, mix,
  normV3, rotateX, rotateY, rotateZ, scaleGeometry, scaleV3, shadeOf, subV3,
} from './geometry';
export type { BakeOptions, BallOptions, BoxOptions, FlagFace, FlagGeometry, RodOptions } from './geometry';

export {
  CHARGE_PAINTERS, FLAG_GLOBAL_DEFAULTS, FLAG_MOUNT_TABLE, FLAG_SHAPE_TABLE, FLAG_WAVE_DEFAULTS,
  FLAGPOLE_GLOBAL_DEFAULTS, FLAGPOLE_KIND_DEFAULTS, assertMountable, buildFlagCloth, buildFlagpole,
  chargeKinds, chargePainterOf, clothSurface, defaultMountFor, finialClearance, mountsFor,
  registerFlagChargePainter, resolveFlagMount, radiusAt,
} from './archetypes';
export type { FlagShapeDef, MountInput, PoleShape } from './archetypes';

export {
  FLAG_DEFS, FLAG_EMBLEMS, FLAG_IDS, FLAG_PALETTES, FLAG_SHAPE_PALETTE,
  FLAGPOLE_DEFS, FLAGPOLE_IDS, FLAGPOLE_KIND_PALETTE, Flag, Flagpole, applyEmblem,
  assemblyMatrixOf, assemblySocketsOf, assemblyStatOf, buildFlagAssembly, buildFlagById, buildFlagpoleById,
  defaultMountOf, emblemsByTag, flagsByShape, flagsByTag, getEmblemDef, getFlagDef, getFlagpoleDef,
  mountsOf, poleStatOf, polesByKind, polesByTag, resolveFlagClothDef, resolveFlagPalette,
  resolveFlagpoleParams, resolveFlagColor, statOf,
} from './Flags';
export { CONTENT_DATA_VERSION } from '../../../core/version';
export type {
  FlagAssembly, FlagAssemblyStat, FlagSocketDef, FlagStat, FlagUnitDef, FlagpoleStat, FlagpoleUnitDef,
} from './Flags';

export {
  WALL_FACINGS, anchorOf, canDeployFlag, chunkOfPlacement, deployQuery, groundAnchor, makePlacement,
  poleAnchorOf, snapPlacement, wallYawOf,
} from './placement';
export type { FlagAnchor, FlagAnchorMode } from './placement';
