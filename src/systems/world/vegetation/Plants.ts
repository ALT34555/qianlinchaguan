/**
 * 植物注册表：把 content/data/world/plants/*.json 的参数表装配成可用的植物原型。
 *
 * 与 Blocks.ts / ChunkTypes.ts 同构的"数据驱动 + 加载即校验"风格：
 *   · 参数表里的 archetype 必须存在于 ARCHETYPES；
 *   · 参数表引用的每个色板键必须存在于 palettes.json；
 *   · 参数与色板都只有四季四套，季节由调用方传入，生成结果与季节一一对应。
 *
 * 新增物种 = 新建/追加一个 JSON 文件并在下方 PLANT_FILES 登记一行；
 * 新增形态 = 在 archetypes.ts 里加一个原型并在 ARCHETYPE_DEFAULTS 登记默认值。
 */
import palettesJson from '../../../../content/data/world/plants/palettes.json';
import treesColdJson from '../../../../content/data/world/plants/trees_cold.json';
import treesSubtropicalJson from '../../../../content/data/world/plants/trees_subtropical.json';
import treesTemperateJson from '../../../../content/data/world/plants/trees_temperate.json';
import treesTropicalJson from '../../../../content/data/world/plants/trees_tropical.json';
import shrubsJson from '../../../../content/data/world/plants/shrubs.json';
import extrasJson from '../../../../content/data/world/plants/extras.json';
import { hashString } from '../../../core/math/Random';
import { ARCHETYPES, ARCHETYPE_DEFAULTS, CLIMATE_DEFAULTS, GLOBAL_DEFAULTS, buildPlant } from './archetypes';
import type { PlantGeometry } from './geometry';
import {
  CLIMATE_ZONES, DEFAULT_BLOOM_COLOR, SEASONS,
  type ClimateZone, type PaletteEntry, type PaletteFile, type PaletteKey, type PlantBloomOptions,
  type PlantBuildOptions, type PlantDef, type PlantFile, type PlantParams, type PlantVariantDef,
  type ResolvedPalette, type Season,
} from './types';

/**
 * 参数表清单（显式导入而不做目录扫描：vite / tsc / rolldown 都能静态分析，
 * 打包时不会漏文件，也不会把 JSON 变成运行时请求）。
 *
 * JSON 的字面量类型与 PlantFile 不会自动相容（缺省键会被推断成 undefined），
 * 因此统一经 asPlantFile 收口 —— 真正的校验在下面的装配循环里做。
 */
function asPlantFile(data: unknown): PlantFile {
  return data as PlantFile;
}

export const PLANT_FILES: readonly { file: string; data: PlantFile }[] = [
  { file: 'trees_tropical.json', data: asPlantFile(treesTropicalJson) },
  { file: 'trees_subtropical.json', data: asPlantFile(treesSubtropicalJson) },
  { file: 'trees_temperate.json', data: asPlantFile(treesTemperateJson) },
  { file: 'trees_cold.json', data: asPlantFile(treesColdJson) },
  { file: 'shrubs.json', data: asPlantFile(shrubsJson) },
  { file: 'extras.json', data: asPlantFile(extrasJson) },
];

/** 参数表格式版本：与 JSON 里的 version 对齐，改结构时必须同步提升 */
export const PLANT_DATA_VERSION = 1;

const paletteData = palettesJson as PaletteFile;

// ---------------------------------------------------------------------------
// 色板
// ---------------------------------------------------------------------------

/** 已注册色板键 -> 四季色阶 */
export const PALETTES: ReadonlyMap<string, PaletteEntry> = new Map<string, PaletteEntry>([
  ...Object.entries(paletteData.bark ?? {}),
  ...Object.entries(paletteData.greenery ?? {}),
]);

const HEX = /^#[0-9a-fA-F]{6}$/;

/**
 * 每个原型会读取哪些色板键。
 * 生成时只解析这些键（物种可用 palette 覆盖成别的键），
 * 这样导出的色表既完整又没有无关条目。
 */
const ARCHETYPE_PALETTE_KEYS: Record<string, readonly PaletteKey[]> = {
  conifer: ['bark', 'foliage', 'foliageDark', 'snow'],
  broadleaf: ['bark', 'barkLight', 'foliage', 'foliageDark', 'canopy', 'bloom', 'fruit', 'snow'],
  palm: ['bark', 'barkLight', 'palmFrond', 'foliageDark', 'bloom', 'fruit', 'snow'],
  acacia: ['bark', 'barkLight', 'foliage', 'foliageDark', 'bloom', 'fruit', 'snow'],
  baobab: ['bark', 'barkLight', 'foliage', 'foliageDark', 'bloom', 'fruit', 'snow'],
  shrub: ['barkLight', 'foliage', 'foliageDark', 'bloom', 'fruit', 'snow'],
  succulent: ['foliage', 'foliageDark', 'thorn', 'bloom', 'snow'],
  bramble: ['barkLight', 'foliage', 'foliageDark', 'thorn', 'bloom', 'fruit', 'snow'],
  grassClump: ['foliage', 'foliageDark', 'bloom', 'snow'],
  sapling: ['barkLight', 'foliage', 'bloom', 'snow'],
};

/**
 * 会开花的原型（被子植物）。
 * `conifer` 是裸子植物、无真正的花，被刻意排除在开花版本之外；
 * 其余原型（含棕榈、金合欢、猴面包树、禾草）都会在 bloom 开启时绽放。
 */
export const BLOOM_ARCHETYPES: readonly string[] = Object.keys(ARCHETYPE_PALETTE_KEYS).filter((k) => k !== 'conifer');

/** 该原型是否支持开花版本（针叶树返回 false） */
export function canBloom(archetype: string): boolean {
  return BLOOM_ARCHETYPES.includes(archetype);
}

function paletteEntry(key: string): PaletteEntry {
  const e = PALETTES.get(key);
  if (!e) {
    throw new Error(`[Vegetation] 未知色板键 "${key}"，可用键: ${[...PALETTES.keys()].sort().join(', ')}`);
  }
  return e;
}

function validateOverrideKeys(variant: PlantVariantDef): void {
  for (const [rawKey, key] of Object.entries(variant.palette ?? {})) {
    if (!PALETTES.has(key)) {
      throw new Error(`[Vegetation] 植物 ${variant.id} 引用了未知色板键: ${rawKey} -> ${key}（可用: ${[...PALETTES.keys()].sort().join(', ')}）`);
    }
    const dot = rawKey.indexOf('.');
    if (dot < 0) continue;
    const seasonName = rawKey.slice(dot + 1);
    if (!(SEASONS as readonly string[]).includes(seasonName)) {
      throw new Error(`[Vegetation] 植物 ${variant.id} 的色板覆盖键 "${rawKey}" 季节无效（应为 spring/summer/autumn/winter）`);
    }
  }
}

/**
 * 解析某个物种在指定季节下的实际颜色表。
 * 覆盖优先级：物种定点覆盖(key.season) > 物种整键覆盖(key) > 色板季节色 > 色板默认色。
 */
export function resolvePalette(variant: PlantVariantDef, season: Season): ResolvedPalette {
  const out: Record<string, string> = {};
  const base = ARCHETYPE_PALETTE_KEYS[variant.archetype] ?? [];
  const keys = new Set<string>([...base, ...Object.values(variant.palette ?? {})]);
  for (const key of keys) {
    const entry = paletteEntry(key);
    const spot = variant.palette?.[`${key}.${season}`];
    const whole = variant.palette?.[key];
    const value = spot ? paletteDataEntry(paletteEntry(spot), season) : whole ? paletteDataEntry(paletteEntry(whole), season) : paletteDataEntry(entry, season);
    if (!HEX.test(value)) throw new Error(`[Vegetation] 植物 ${variant.id} 的色板键 ${key} 色值非法: ${value}`);
    out[key] = value;
  }
  return out;
}

/** 取色阶：季节色优先，缺失时回退默认色 */
function paletteDataEntry(entry: PaletteEntry, season: Season): string {
  return entry[season] ?? entry.default;
}

// ---------------------------------------------------------------------------
// 参数分层合并
// ---------------------------------------------------------------------------

/** 四层合并：通用默认 -> 生长带默认 -> 原型默认 -> 物种参数（后者覆盖前者） */
export function mergeParams(variant: PlantVariantDef): PlantParams {
  const climate = CLIMATE_DEFAULTS[variant.climate];
  if (!climate) throw new Error(`[Vegetation] 植物 ${variant.id} 的生长带非法: ${variant.climate}`);
  const archetype = ARCHETYPE_DEFAULTS[variant.archetype];
  if (!archetype) throw new Error(`[Vegetation] 植物 ${variant.id} 的原型 "${variant.archetype}" 未登记默认参数`);
  return { ...GLOBAL_DEFAULTS, ...climate, ...archetype, ...variant.params };
}

/**
 * 把参数表的一条定义装配成可直接生成的 PlantDef。
 * `bloom` 为开花版本开关与花色接口；针叶树会被强制关掉开花。
 */
export function toPlantDef(variant: PlantVariantDef, season: Season, bloom?: PlantBloomOptions): PlantDef {
  const seed = variant.seed ?? 0;
  // 针叶树是裸子植物：即使调用方传了 bloom，也不开花
  const blooming = !!bloom?.bloom && canBloom(variant.archetype);
  // 形态种子：原型名 + 物种 id + 形态编号，保证"改参数不改形态，改形态不影响别的物种"
  return {
    id: variant.id,
    archetype: variant.archetype,
    climate: variant.climate,
    name: variant.name,
    latin: variant.latin,
    seed,
    tags: variant.tags ?? [],
    params: mergeParams(variant),
    palette: resolvePalette(variant, season),
    buildSeed: hashString(`${variant.archetype}:${variant.id}:${seed}`),
    // 积雪只在秋末与冬季出现，避免冷带树在盛夏顶着雪壳
    snow: season === 'winter' || season === 'autumn',
    bloom: blooming,
    // 只有开花时才带上接口花色；未传则留空，让 tone() 回退到色板/默认花色
    bloomColor: blooming ? bloom?.bloomColor : undefined,
  };
}

/** 尺寸倍率：物种 scale 缺省为 1 */
function buildOptionsOf(variant: PlantVariantDef): { scale: number } {
  const scale = typeof variant.scale === 'number' && Number.isFinite(variant.scale) && variant.scale > 0 ? variant.scale : 1;
  return { scale };
}

// ---------------------------------------------------------------------------
// 装配与校验
// ---------------------------------------------------------------------------

const allVariants: PlantVariantDef[] = [];
for (const { file, data } of PLANT_FILES) {
  if (data.version !== undefined && data.version !== PLANT_DATA_VERSION) {
    throw new Error(`[Vegetation] ${file} 的 version=${data.version}，当前支持 ${PLANT_DATA_VERSION}`);
  }
  if (!Array.isArray(data.plants) || data.plants.length === 0) {
    throw new Error(`[Vegetation] ${file} 缺少 plants 数组`);
  }
  for (const p of data.plants) allVariants.push({ ...p, source: file });
}

/** 全部植物定义（未按季节解析色板前） */
export const PLANT_VARIANTS: readonly PlantVariantDef[] = allVariants;

const byId = new Map<string, PlantVariantDef>();
for (const v of allVariants) {
  if (byId.has(v.id)) throw new Error(`[Vegetation] 植物 id 重复: ${v.id}（${byId.get(v.id)!.source} 与 ${v.source}）`);
  if (!ARCHETYPES[v.archetype]) {
    throw new Error(`[Vegetation] 植物 ${v.id} 的原型不存在: ${v.archetype}（可用: ${Object.keys(ARCHETYPES).join(', ')}）`);
  }
  if (!(CLIMATE_ZONES as readonly string[]).includes(v.climate)) {
    throw new Error(`[Vegetation] 植物 ${v.id} 的生长带非法: ${v.climate}`);
  }
  validateOverrideKeys(v);
  byId.set(v.id, v);
}

/**
 * 四季色板缓存：季节切换时树木颜色会整片刷新，
 * 缓存避免每次生成都重新解析（也保证同一季节的产物字节级一致）。
 */
const paletteCache = new Map<string, ResolvedPalette>();

function cachedPalette(variant: PlantVariantDef, season: Season): ResolvedPalette {
  const key = `${variant.id}|${season}`;
  let p = paletteCache.get(key);
  if (!p) {
    p = resolvePalette(variant, season);
    paletteCache.set(key, p);
  }
  return p;
}

/** 植物 id 常量表：与参数表一一对应，代码里引用具体植物时用这里 */
export const Plant = Object.freeze(
  Object.fromEntries(allVariants.map((v) => [v.id, v.id])) as Record<string, string>,
);

/** 按 id 取参数表定义 */
export function getPlantVariant(id: string): PlantVariantDef | undefined {
  return byId.get(id);
}

/** 按生长带筛选物种 */
export function plantsByClimate(climate: ClimateZone): readonly PlantVariantDef[] {
  return allVariants.filter((v) => v.climate === climate);
}

/** 按标签筛选（如 'conifer'、'flowering'、'crop'） */
export function plantsByTag(tag: string): readonly PlantVariantDef[] {
  return allVariants.filter((v) => (v.tags ?? []).includes(tag));
}

/**
 * 生成一株植物的低模几何。
 *
 * `bloom` 为开花版本的开关与花色接口（默认花色 #a8456b）：
 *   buildPlantById(id, 'spring')                          // 常规
 *   buildPlantById(id, 'spring', { bloom: true })         // 开花版本，花色 #a8456b
 *   buildPlantById(id, 'spring', { bloom: true, bloomColor: '#fff3b0' })
 * 针叶树不受 bloom 影响（裸子植物不开花）。
 */
export function buildPlantById(id: string, season: Season = 'summer', bloom?: PlantBloomOptions): PlantGeometry {
  const variant = byId.get(id);
  if (!variant) throw new Error(`[Vegetation] 未知植物 id: ${id}`);
  const def: PlantDef = { ...toPlantDef(variant, season, bloom), palette: cachedPalette(variant, season) };
  const geo = buildPlant(def);
  scaleGeometry(geo, buildOptionsOf(variant).scale);
  return geo;
}

/**
 * 该物种开花版本实际使用的花色（#rrggbb）；针叶树返回 null（不开花）。
 * 与 tone() 的取值链完全一致：色板 bloom > 默认花色，用于清单/文档展示。
 */
export function bloomColorOf(variant: PlantVariantDef, season: Season = 'spring'): string | null {
  if (!canBloom(variant.archetype)) return null;
  return cachedPalette(variant, season).bloom ?? DEFAULT_BLOOM_COLOR;
}

/** 就地缩放几何（含包围半径与高度），参数表的 scale 用它实现 */
export function scaleGeometry(geo: PlantGeometry, scale: number): void {
  if (scale === 1) return;
  for (let i = 0; i < geo.positions.length; i++) geo.positions[i] *= scale;
  geo.radius *= scale;
  geo.height *= scale;
}

/** 生成时的完整选项（供导出脚本按季节/开花批量产出） */
export function buildOptions(season: Season, bloom?: PlantBloomOptions): PlantBuildOptions {
  return { seed: 0, season, bloom: bloom?.bloom, bloomColor: bloom?.bloomColor };
}

/** 诊断：统计物种数、几何面数与各材质槽三角形数 */
export interface PlantStat {
  id: string;
  name: string;
  archetype: string;
  climate: ClimateZone;
  height: number;
  radius: number;
  triangles: number;
  vertices: number;
  materials: string;
}

export function statOf(id: string, season: Season = 'summer'): PlantStat {
  const variant = byId.get(id)!;
  const geo = buildPlantById(id, season);
  return {
    id,
    name: variant.name,
    archetype: variant.archetype,
    climate: variant.climate,
    height: geo.height,
    radius: geo.radius,
    triangles: geo.triangleCount,
    vertices: geo.vertexCount,
    materials: geo.groups.map((g) => `${g.mat}:${g.count / 3}`).join(' '),
  };
}

/** 全部植物 id（参数表顺序） */
export const PLANT_IDS: readonly string[] = allVariants.map((v) => v.id);

export type { PlantDef, PlantGeometry, Season };
export { buildPlant };
