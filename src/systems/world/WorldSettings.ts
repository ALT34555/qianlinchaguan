/** 顺序固定为由热到冷 */
import { normalizeArtificial, type ArtificialSettings } from './artificial/ArtificialSettings.ts';
import { MACRO_TERRAIN_PROFILES, MACRO_CLIMATE_PROFILES, type MacroTerrainProfile, type MacroClimateProfile } from './planet-macro/Settings.ts';

export const CLIMATES = [
  { name: '热带', zone: 100, color: '#e7ab64' },
  { name: '亚热带', zone: 200, color: '#aac76c' },
  { name: '常规', zone: 0, color: '#68b698' },
  { name: '温带', zone: 300, color: '#7caec4' },
  { name: '寒带', zone: 400, color: '#c6dbea' },
] as const;

export type ClimateWeights = [number, number, number, number, number];
export const DEFAULT_CLIMATE_WEIGHTS: ClimateWeights = [20, 20, 20, 20, 20];

export interface PlanetSettings {
  map: 'procedural' | 'earth';
  poleTemperature: number;
  equatorTemperature: number;
  /** 风吹向的方位角 */
  monsoonDirection: number;
  equatorChunks: number;
  /** 0 原始大陆、5 大块大陆、9 更分散的大块与岛屿 */
  tectonicActivity: number;
}
export interface WorldGeneration {
  /** 唯一生成规则：宏观星球 */
  terrainRule: 'planet-macro';
  macroColumns: number;
  macroTerrain: MacroTerrainProfile;
  macroClimate: MacroClimateProfile;
  artificial?: ArtificialSettings;
  mode: 'planet';
  planet: PlanetSettings;
  landRatio: number;
  /** 降水倍率，0 为无河流 */
  precipitation: number;
}
export const DEFAULT_PLANET: PlanetSettings = {
  map: 'procedural', poleTemperature: -25, equatorTemperature: 30,
  monsoonDirection: 90, equatorChunks: 640000, tectonicActivity: 5,
};
/** 宏观星球默认生成参数 */
export const DEFAULT_GENERATION: WorldGeneration = {
  terrainRule: 'planet-macro', macroColumns: 1024, macroTerrain: 'balanced', macroClimate: 'wind-field',
  mode: 'planet', planet: DEFAULT_PLANET, landRatio: .3, precipitation: 1,
};

/** 归一化生成参数，仅收宏观星球 */
export function normalizeGeneration(value: unknown = DEFAULT_GENERATION): WorldGeneration {
  if (!value || typeof value !== 'object') throw new Error('地形生成规则无效。');
  const g = value as { terrainRule?: string; hydrology?: unknown; templateRecipe?: unknown; mode?: unknown; planet?: PlanetSettings;
    macroColumns?: number; macroTerrain?: MacroTerrainProfile; macroClimate?: MacroClimateProfile; landRatio?: number;
    precipitation?: number; artificial?: unknown };
  if (g.terrainRule === 'planet-v1013') throw Error('星球1013规则已退役；请创建新的宏观星球。');
  if (g.hydrology !== undefined || g.templateRecipe !== undefined) throw Error('轻量湖海规则已退役；请创建新的宏观星球。');
  if (g.terrainRule !== undefined && g.terrainRule !== 'planet-macro') throw Error('地形生成规则无效：当前版本仅支持宏观星球。');
  if (g.mode !== undefined && g.mode !== 'planet') throw Error('地形生成规则无效：当前版本仅支持宏观星球。');
  const p = g.planet;
  if (!p) throw new Error('星球地图无效。');
  if (p.map !== 'procedural') throw new Error('预制地球规则已退役；当前版本仅支持自然星球。');
  if (![p.poleTemperature, p.equatorTemperature].every(t => typeof t === 'number' && Number.isFinite(t) && t >= -100 && t <= 100)) {
    throw new Error('极点与赤道温度须在 −100～100 °C 之间。');
  }
  if (typeof p.monsoonDirection !== 'number' || !Number.isFinite(p.monsoonDirection) || p.monsoonDirection < 0 || p.monsoonDirection >= 360) {
    throw new Error('季风方向须在 0～359° 之间（风吹向的方向）。');
  }
  if (!Number.isInteger(p.equatorChunks) || p.equatorChunks < 256 || p.equatorChunks > 640000 || p.equatorChunks % 4 !== 0) {
    throw new Error('赤道长度须为 256～640000 区块，且是 4 的倍数。');
  }
  const tectonicActivity = p.tectonicActivity === undefined ? DEFAULT_PLANET.tectonicActivity : p.tectonicActivity;
  if (!Number.isInteger(tectonicActivity) || tectonicActivity < 0 || tectonicActivity > 9) {
    throw new Error('板块活动度须为 0～9 的整数。');
  }
  const landRatio = g.landRatio === undefined ? .3 : g.landRatio;
  if (typeof landRatio !== 'number' || !Number.isFinite(landRatio) || landRatio < 0 || landRatio > 1 ||
      Math.abs(landRatio * 10 - Math.round(landRatio * 10)) > 1e-8) {
    throw new Error('陆占比须为 0～1，步进为 0.1。');
  }
  const precipitation = g.precipitation === undefined ? 1 : g.precipitation;
  if (typeof precipitation !== 'number' || !Number.isFinite(precipitation) || precipitation < 0 || precipitation > 2 ||
      Math.abs(precipitation * 4 - Math.round(precipitation * 4)) > 1e-8) {
    throw new Error('降水倍率须为0～2，步进0.25。');
  }
  const macroColumns = g.macroColumns ?? 1024;
  if (!Number.isInteger(macroColumns) || macroColumns < 64 || macroColumns > 1024 || macroColumns % 4) throw Error('宏观网格须为64～1024列且是4的倍数');
  if (p.equatorChunks / macroColumns < 64) throw Error('宏观星球最低尺度为64区块/粗格');
  const macroTerrain = g.macroTerrain ?? 'balanced';
  if (!MACRO_TERRAIN_PROFILES.includes(macroTerrain)) throw Error('宏观地形配方无效');
  const macroClimate = g.macroClimate ?? 'wind-field';
  if (!MACRO_CLIMATE_PROFILES.some(profile => profile === macroClimate)) throw Error('宏观气候规则无效');
  return { mode: 'planet', terrainRule: 'planet-macro', macroColumns, macroTerrain, macroClimate,
    planet: { ...p, tectonicActivity }, landRatio, precipitation, artificial: normalizeArtificial(g.artificial) };
}

/** 区块中心对应经纬度 */
export function planetCoordinates(cx: number, cz: number, circumference: number): { longitude: number; latitude: number } {
  const wrap = (v: number) => ((v + circumference / 2) % circumference + circumference) % circumference - circumference / 2;
  let latitude = -wrap(cz + .5) / circumference * 360;
  let longitude = wrap(cx + .5) / circumference * 360;
  if (latitude > 90) { latitude = 180 - latitude; longitude += 180; }
  if (latitude < -90) { latitude = -180 - latitude; longitude += 180; }
  longitude = ((longitude + 180) % 360 + 360) % 360 - 180;
  return { longitude, latitude: latitude || 0 };
}

export function temperatureClimate(temperature: number): number {
  return temperature >= 26 ? 0 : temperature >= 18 ? 1 : temperature >= 10 ? 2 : temperature >= 0 ? 3 : 4;
}

export function normalizeClimateWeights(value: unknown): ClimateWeights {
  if (!Array.isArray(value) || value.length !== 5 ||
      value.some(v => typeof v !== 'number' || !Number.isFinite(v) || v < 0)) {
    throw new Error('请填写五个非负的气候权重。');
  }
  const sum = value.reduce((a, b) => a + b, 0);
  if (!Number.isFinite(sum) || sum <= 0) throw new Error('至少一种气候的权重必须大于 0。');
  return value.map(v => v / sum) as ClimateWeights;
}
