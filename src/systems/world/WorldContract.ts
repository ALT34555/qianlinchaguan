/** 世界生成器契约，详见同名文档 */
import { CHUNK_SIZE } from '../../core/config';
import type { PlateTier } from './PlateUplift';
import type { WaterField, WaterSample } from './DynamicWater';
import { DEFAULT_WATER_FREEZE_RULES, validateWaterFreezeRules, type WaterFreezeRules } from './WaterFreezing';
import { validateWaterAppearance, type WaterAppearanceEntry, type WaterAppearanceOverride, type WaterAppearanceUpdate, type ChunkWaterAppearance } from './WaterAppearance';
import { DEFAULT_CLIMATE_WEIGHTS, normalizeClimateWeights, normalizeGeneration, type ClimateWeights, type WorldGeneration } from './WorldSettings';
import type { TerrainSample } from './TerrainLayers';
import type { WatershedState } from './Watershed';
import type { RiverPoint } from './RiverChannels';
import { resolveWaterAppearance } from './WaterAppearance';

export type { RiverPoint };
export type { WatershedState };
export { FLOW_DIRECTIONS } from './Hydrology';

export const PADDED_SIZE = CHUNK_SIZE + 2;
/** 舆图粗预览每边采样格数 */
export const PREVIEW_SIZE = 16;

/** 降水模型所需的宏观格气候数组 */
export interface RainWorld {
  columns:number;rows:number;circumference:number;options:{seed:number};fingerprint:string;
  rainfall:Float32Array;temperature:Float32Array;height:Float32Array;
}

/** 宏观全球河网全览摘要 */
export interface MacroRiverOverview {
  columns:number;rows:number;receiver:Int32Array;widths:Float32Array;discharge:Float64Array;water:Uint8Array;
}

export interface ChunkInfo {
  cx: number; cz: number; type: number; climate: number; temperature: number;
  elevation: number; flow: number; discharge: number; riverWidth: number; riverDepth: number;riverBlocks:number;
  lakeLevel: number | null;
  elevationTier: PlateTier; largeLandmass: boolean;
  plateBase:number;hillRelief:number;highlandRelief:number;mountainRelief:number;waterfallDrop:number;wetland:number;
  downstream: { cx: number; cz: number } | null;
  /** 舆图与调试显示用流向 */
  displayFlow: number;
  /** 内部走廊值，供流向补算 */
  courseVal: number;
}

export interface ChunkGenResult {
  cx: number; cz: number; type: number;
  heights: Float32Array;
  surfaces: Uint32Array;
  /** 外扩一圈的地表混合 RGB */
  surfaceColors: Uint8Array;
  /** 外扩一圈 */
  waterLevels: Float32Array;
  waterField: WaterField;
}

/** 稀疏采样结果：供舆图平均色与粗预览，无外扩圈 */
export interface PreviewChunk {
  cx: number; cz: number;
  /** 采样边长（格） */
  size: number;
  /** 采样步长（格） */
  step: number;
  heights: Float32Array;
  waterLevels: Float32Array;
  surfaceColors: Uint8Array;
}

const key = (x: number, z: number) => `${x},${z}`;

/** 生成器基类，详见同名文档 */
export abstract class WorldGenerator {
  readonly seed: number;
  /** 启用显示流向邻近主河查询 */
  displayFlowWanted = false;
  readonly climateWeights: ClimateWeights;
  readonly generation: WorldGeneration;
  protected waterFreezeRules: WaterFreezeRules = {...DEFAULT_WATER_FREEZE_RULES};
  protected readonly waterAppearances = new Map<string, WaterAppearanceOverride>();
  private readonly naturalWaterAppearances = new Map<string, ChunkWaterAppearance>();
  /** 节点表上限，超过即分批淘汰，可下调以便测试 */
  cacheLimit = 120000;

  protected constructor(seed: number, climateWeights: ClimateWeights = DEFAULT_CLIMATE_WEIGHTS, generation?: WorldGeneration) {
    this.seed = seed >>> 0;
    this.climateWeights = normalizeClimateWeights(climateWeights);
    this.generation = normalizeGeneration(generation);
  }

  /** 经度环绕；平面世界不做处理 */
  protected wrapBlockX(x: number): number {
    if (this.generation.mode !== 'planet') return x;
    const period = this.generation.planet.equatorChunks * CHUNK_SIZE;
    return ((x + period / 2) % period + period) % period - period / 2;
  }

  setWaterSeason(_phase:number|null):boolean {return false;}
  getWaterSeason():number|null {return null;}
  /** 逐日降水只能在宏观星球上接入 */
  get precipitationWorld():RainWorld|null {return null;}

  setWaterAppearance(cx: number, cz: number, appearance: WaterAppearanceUpdate | null): void {
    if (!Number.isInteger(cx) || !Number.isInteger(cz)) throw new Error('水体坐标须为整数区块');
    const k = key(this.wrapBlockX(cx * CHUNK_SIZE) / CHUNK_SIZE, cz);
    if (!appearance) { this.waterAppearances.delete(k); return; }
    const next = {...this.waterAppearances.get(k)};
    for (const field of ['form', 'freezing', 'temperature'] as const) {
      if (appearance[field] === null) delete next[field];
      else if (appearance[field] !== undefined) Object.assign(next, {[field]: appearance[field]});
    }
    if (appearance.colors === null) delete next.colors;
    else if (appearance.colors) next.colors = {...next.colors, ...appearance.colors};
    this.waterAppearances.set(k, validateWaterAppearance(next));
  }

  getWaterAppearanceOverrides(): WaterAppearanceEntry[] {
    return [...this.waterAppearances].map(([k, appearance]) => {
      const [cx, cz] = k.split(',').map(Number);
      return { cx, cz, appearance: validateWaterAppearance(appearance) };
    });
  }

  replaceWaterAppearanceOverrides(entries: readonly WaterAppearanceEntry[]): void {
    this.waterAppearances.clear();
    for (const { cx, cz, appearance } of entries) this.setWaterAppearance(cx, cz, appearance);
  }

  setWaterFreezeRules(rules: Partial<WaterFreezeRules>): void {
    const next = validateWaterFreezeRules({...this.waterFreezeRules, ...rules});
    if ((Object.keys(next) as (keyof WaterFreezeRules)[]).every(k => next[k] === this.waterFreezeRules[k])) return;
    this.waterFreezeRules = next;
    this.naturalWaterAppearances.clear();
  }

  getWaterFreezeRules(): WaterFreezeRules { return {...this.waterFreezeRules}; }

  getChunkWaterState(cx: number, cz: number): ChunkWaterAppearance {
    cx = this.wrapBlockX(cx * CHUNK_SIZE) / CHUNK_SIZE;
    const k = key(cx, cz);
    let natural = this.naturalWaterAppearances.get(k);
    if (!natural) {
      const info = this.getChunkInfo(cx, cz);
      const path = this.getRiverPath(cx, cz);
      const level = info.lakeLevel ?? path[Math.floor(path.length / 2)]?.level ?? Math.max(0, info.elevation);
      natural = resolveWaterAppearance(info.temperature - Math.max(0, level) * this.waterFreezeRules.altitudeLapseRate, undefined, this.waterFreezeRules);
      if (this.naturalWaterAppearances.size > 8192) this.naturalWaterAppearances.clear();
      this.naturalWaterAppearances.set(k, natural);
    }
    const override = this.waterAppearances.get(k);
    return override ? resolveWaterAppearance(natural.temperature, override, this.waterFreezeRules) : natural;
  }

  getChunkWaterAppearance(cx: number, cz: number): ChunkWaterAppearance { return this.getChunkWaterState(cx, cz); }

  getSurfaceTemperature(cx:number,cz:number):number {return this.getClimate(cx,cz).temperature-Math.max(0,this.getTerrainSample(cx,cz).height)*.0065;}

  // ---- 子类必须提供 ----
  abstract getClimate(cx: number, cz: number): {index: number; temperature: number};
  abstract getTerrainSample(cx: number, cz: number, rainfall?: boolean): TerrainSample;
  abstract getChunkInfo(cx: number, cz: number): ChunkInfo;
  abstract getOverviewInfo(cx: number, cz: number): ChunkInfo;
  abstract getChunkType(cx: number, cz: number): number;
  abstract getBaseChunkInfo(cx: number, cz: number): ChunkInfo;
  abstract getAtlasNodeInfo(cx: number, cz: number): ChunkInfo;
  abstract cachedChunkInfo(cx: number, cz: number): ChunkInfo | null;
  abstract getRiverPath(cx: number, cz: number): readonly RiverPoint[];
  abstract getRiverPaths(cx: number, cz: number): readonly (readonly RiverPoint[])[];
  abstract hasRiverPaths(cx: number, cz: number): boolean;
  abstract getHeight(wx: number, wz: number): number;
  abstract getWaterLevel(wx: number, wz: number): number;
  abstract getWaterState(wx: number, wz: number): WaterSample;
  abstract findSpawnChunk(maxRadius?: number): { cx: number; cz: number };
  abstract findSpawnOrigin(): { cx: number; cz: number };
  abstract spawnSurface(cx: number, cz: number): { height: number; water: number } | null;
  abstract generateChunk(cx: number, cz: number): ChunkGenResult;
  abstract generatePreviewChunk(cx: number, cz: number, size?: number): PreviewChunk;

  // 全球骨架快照接口
  /** 导出全球骨架快照 */
  exposeHydrology(): WatershedState { throw new Error('本生成器不导出骨架快照'); }
  /** 采用外部骨架快照 */
  adoptHydrology(_state: WatershedState): void {}
  /** 登记待用快照，首次访问时再校验采用 */
  deferHydrology(_state: WatershedState): void {}
  /** 构建全球骨架（板块、气候、排水） */
  prepareMacroHydrology(): void {}
  adoptMapSummary(_info: ChunkInfo, _paths: readonly (readonly RiverPoint[])[]): void {}

  /** 卫星配色：仅宏观链路实现 */
  getSatelliteColor(_cx: number, _cz: number, _relief = true): number | null { return null; }
  getMacroRiverOverview(): MacroRiverOverview | null { return null; }
}

/** 生成器实例的统一类型别名 */
export type WorldGeneratorBase = WorldGenerator;
