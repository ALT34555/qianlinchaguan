/** 植物注册表 */
import palettesJson from '../../../../content/data/world/plants/palettes.json';
// 寒带乔木
import shuYunshan from '../../../../content/data/world/plants/woody/shu.yunshan.json';
import shuLengshan from '../../../../content/data/world/plants/woody/shu.lengshan.json';
import shuLuoye from '../../../../content/data/world/plants/woody/shu.luoye.json';
import shuChuizhi from '../../../../content/data/world/plants/woody/shu.chuizhi.json';
// 温带乔木
import shuGaoshan from '../../../../content/data/world/plants/woody/shu.gaoshan.json';
import shuHongpi from '../../../../content/data/world/plants/woody/shu.hongpi.json';
import shuWujiaofeng from '../../../../content/data/world/plants/woody/shu.wujiaofeng.json';
import shuYinxing from '../../../../content/data/world/plants/woody/shu.yinxing.json';
import shuChuilui from '../../../../content/data/world/plants/woody/shu.chuilui.json';
import shuBaigan from '../../../../content/data/world/plants/woody/shu.baigan.json';
// 亚热带乔木
import shuXiangzhang from '../../../../content/data/world/plants/woody/shu.xiangzhang.json';
import shuCha from '../../../../content/data/world/plants/woody/shu.cha.json';
import shuMasan from '../../../../content/data/world/plants/woody/shu.masan.json';
import shuGanju from '../../../../content/data/world/plants/woody/shu.ganju.json';
import shuPaotong from '../../../../content/data/world/plants/woody/shu.paotong.json';
import shuMaozhu from '../../../../content/data/world/plants/woody/shu.maozhu.json';
// 热带乔木
import shuLongnao from '../../../../content/data/world/plants/woody/shu.longnao.json';
import shuMumian from '../../../../content/data/world/plants/woody/shu.mumian.json';
import shuYezi from '../../../../content/data/world/plants/woody/shu.yezi.json';
import shuBinglang from '../../../../content/data/world/plants/woody/shu.binglang.json';
import shuMangguo from '../../../../content/data/world/plants/woody/shu.mangguo.json';
import shuFenghuang from '../../../../content/data/world/plants/woody/shu.fenghuang.json';
// 灌木
import guanZhen from '../../../../content/data/world/plants/woody/guan.zhen.json';
import guanLianqiao from '../../../../content/data/world/plants/woody/guan.lianqiao.json';
import guanHuanglu from '../../../../content/data/world/plants/woody/guan.huanglu.json';
import guanZhizi from '../../../../content/data/world/plants/woody/guan.zhizi.json';
import guanMoli from '../../../../content/data/world/plants/woody/guan.moli.json';
import guanDujuan from '../../../../content/data/world/plants/woody/guan.dujuan.json';
import guanFusang from '../../../../content/data/world/plants/woody/guan.fusang.json';
import guanXuangouzi from '../../../../content/data/world/plants/woody/guan.xuangouzi.json';
import guanYueju from '../../../../content/data/world/plants/woody/guan.yueju.json';
import guanAili from '../../../../content/data/world/plants/woody/guan.aili.json';
// 地被 / 草本 / 水生
import caoYangmao from '../../../../content/data/world/plants/herbaceous/cao.yangmao.json';
import caoJue from '../../../../content/data/world/plants/herbaceous/cao.jue.json';
import caoLiuwei from '../../../../content/data/world/plants/herbaceous/cao.luwei.json';
import caoDixue from '../../../../content/data/world/plants/herbaceous/cao.dixue.json';
import caoShamo from '../../../../content/data/world/plants/herbaceous/cao.shamo.json';
import caoHanji from '../../../../content/data/world/plants/herbaceous/cao.hanji.json';
import shuiPucao from '../../../../content/data/world/plants/herbaceous/shui.pucao.json';
import shuiLian from '../../../../content/data/world/plants/herbaceous/shui.lian.json';
// 中国特色经济作物
import nongDao from '../../../../content/data/world/plants/herbaceous/nong.dao.json';
import nongMai from '../../../../content/data/world/plants/herbaceous/nong.mai.json';
import nongSu from '../../../../content/data/world/plants/herbaceous/nong.su.json';
import nongDa from '../../../../content/data/world/plants/herbaceous/nong.da.json';
import nongSang from '../../../../content/data/world/plants/herbaceous/nong.sang.json';
// 观赏植物：梅兰竹菊
import huaMei from '../../../../content/data/world/plants/herbaceous/hua.mei.json';
import huaLan from '../../../../content/data/world/plants/herbaceous/hua.lan.json';
import huaZhu from '../../../../content/data/world/plants/herbaceous/hua.zhu.json';
import huaJu from '../../../../content/data/world/plants/herbaceous/hua.ju.json';
import { hashString } from '../../../core/math/Random';
import { ARCHETYPES, ARCHETYPE_DEFAULTS, CLIMATE_DEFAULTS, GLOBAL_DEFAULTS, buildPlant } from './archetypes';
import type { PlantGeometry } from './geometry';
import {
  CLIMATE_ZONES, DEFAULT_BLOOM_COLOR, PLANT_MODELED_STATES, PLANT_STATES, SEASONS,
  type ClimateZone, type PaletteEntry, type PaletteFile, type PaletteKey, type PlantBloomOptions,
  type PlantBuildOptions, type PlantDef, type PlantFile, type PlantParams, type PlantState,
  type PlantStateDef, type PlantStateTable, type PlantVariantDef,
  type ResolvedPalette, type Season,
} from './types';

/** 参数表清单（显式导入而不做目录扫描 */
function asPlantFile(data: unknown): PlantFile {
  return data as PlantFile;
}

export const PLANT_FILES: readonly { file: string; data: PlantFile }[] = [
  // ---- 寒带乔木 ----
  { file: 'woody/shu.yunshan.json', data: asPlantFile(shuYunshan) },
  { file: 'woody/shu.lengshan.json', data: asPlantFile(shuLengshan) },
  { file: 'woody/shu.luoye.json', data: asPlantFile(shuLuoye) },
  { file: 'woody/shu.chuizhi.json', data: asPlantFile(shuChuizhi) },
  // ---- 温带乔木 ----
  { file: 'woody/shu.gaoshan.json', data: asPlantFile(shuGaoshan) },
  { file: 'woody/shu.hongpi.json', data: asPlantFile(shuHongpi) },
  { file: 'woody/shu.wujiaofeng.json', data: asPlantFile(shuWujiaofeng) },
  { file: 'woody/shu.yinxing.json', data: asPlantFile(shuYinxing) },
  { file: 'woody/shu.chuilui.json', data: asPlantFile(shuChuilui) },
  { file: 'woody/shu.baigan.json', data: asPlantFile(shuBaigan) },
  // ---- 亚热带乔木 ----
  { file: 'woody/shu.xiangzhang.json', data: asPlantFile(shuXiangzhang) },
  { file: 'woody/shu.cha.json', data: asPlantFile(shuCha) },
  { file: 'woody/shu.masan.json', data: asPlantFile(shuMasan) },
  { file: 'woody/shu.ganju.json', data: asPlantFile(shuGanju) },
  { file: 'woody/shu.paotong.json', data: asPlantFile(shuPaotong) },
  { file: 'woody/shu.maozhu.json', data: asPlantFile(shuMaozhu) },
  // ---- 热带乔木 ----
  { file: 'woody/shu.longnao.json', data: asPlantFile(shuLongnao) },
  { file: 'woody/shu.mumian.json', data: asPlantFile(shuMumian) },
  { file: 'woody/shu.yezi.json', data: asPlantFile(shuYezi) },
  { file: 'woody/shu.binglang.json', data: asPlantFile(shuBinglang) },
  { file: 'woody/shu.mangguo.json', data: asPlantFile(shuMangguo) },
  { file: 'woody/shu.fenghuang.json', data: asPlantFile(shuFenghuang) },
  // ---- 灌木 ----
  { file: 'woody/guan.zhen.json', data: asPlantFile(guanZhen) },
  { file: 'woody/guan.lianqiao.json', data: asPlantFile(guanLianqiao) },
  { file: 'woody/guan.huanglu.json', data: asPlantFile(guanHuanglu) },
  { file: 'woody/guan.zhizi.json', data: asPlantFile(guanZhizi) },
  { file: 'woody/guan.moli.json', data: asPlantFile(guanMoli) },
  { file: 'woody/guan.dujuan.json', data: asPlantFile(guanDujuan) },
  { file: 'woody/guan.fusang.json', data: asPlantFile(guanFusang) },
  { file: 'woody/guan.xuangouzi.json', data: asPlantFile(guanXuangouzi) },
  { file: 'woody/guan.yueju.json', data: asPlantFile(guanYueju) },
  { file: 'woody/guan.aili.json', data: asPlantFile(guanAili) },
  // ---- 地被 / 草本 / 水生 ----
  { file: 'herbaceous/cao.yangmao.json', data: asPlantFile(caoYangmao) },
  { file: 'herbaceous/cao.jue.json', data: asPlantFile(caoJue) },
  { file: 'herbaceous/cao.luwei.json', data: asPlantFile(caoLiuwei) },
  { file: 'herbaceous/cao.dixue.json', data: asPlantFile(caoDixue) },
  { file: 'herbaceous/cao.shamo.json', data: asPlantFile(caoShamo) },
  { file: 'herbaceous/cao.hanji.json', data: asPlantFile(caoHanji) },
  { file: 'herbaceous/shui.pucao.json', data: asPlantFile(shuiPucao) },
  { file: 'herbaceous/shui.lian.json', data: asPlantFile(shuiLian) },
  // ---- 中国特色经济作物 ----
  { file: 'herbaceous/nong.dao.json', data: asPlantFile(nongDao) },
  { file: 'herbaceous/nong.mai.json', data: asPlantFile(nongMai) },
  { file: 'herbaceous/nong.su.json', data: asPlantFile(nongSu) },
  { file: 'herbaceous/nong.da.json', data: asPlantFile(nongDa) },
  { file: 'herbaceous/nong.sang.json', data: asPlantFile(nongSang) },
  // ---- 观赏植物：梅兰竹菊 ----
  { file: 'herbaceous/hua.mei.json', data: asPlantFile(huaMei) },
  { file: 'herbaceous/hua.lan.json', data: asPlantFile(huaLan) },
  { file: 'herbaceous/hua.zhu.json', data: asPlantFile(huaZhu) },
  { file: 'herbaceous/hua.ju.json', data: asPlantFile(huaJu) },
];

/** 参数表格式版本 */
export const PLANT_DATA_VERSION = 2;

const paletteData = palettesJson as PaletteFile;

// 
// 色板
// 

/** 已注册色板键 -> 四季色阶 */
export const PALETTES: ReadonlyMap<string, PaletteEntry> = new Map<string, PaletteEntry>([
  ...Object.entries(paletteData.bark ?? {}),
  ...Object.entries(paletteData.greenery ?? {}),
]);

const HEX = /^#[0-9a-fA-F]{6}$/;

/** 每个原型会读取哪些色板键 */
const ARCHETYPE_PALETTE_KEYS: Record<string, readonly PaletteKey[]> = {
  conifer: ['bark', 'barkWarm', 'foliage', 'foliageDark', 'pollen', 'snow'],
  broadleaf: ['bark', 'barkLight', 'barkDark', 'foliage', 'foliageDark', 'canopy', 'bloom', 'pollen', 'fruit', 'snow'],
  palm: ['bark', 'barkLight', 'foliageDark', 'palmFrond', 'palmFrondDry', 'bloom', 'pollen', 'fruit', 'snow'],
  acacia: ['bark', 'barkLight', 'foliage', 'foliageDark', 'bloom', 'pollen', 'fruit', 'snow'],
  baobab: ['bark', 'barkLight', 'foliage', 'foliageDark', 'bloom', 'pollen', 'fruit', 'snow'],
  shrub: ['barkLight', 'foliage', 'foliageDark', 'bloom', 'pollen', 'fruit', 'snow'],
  succulent: ['foliage', 'foliageDark', 'thorn', 'bloom', 'pollen', 'snow'],
  bramble: ['barkLight', 'foliage', 'foliageDark', 'thorn', 'bloom', 'pollen', 'fruit', 'snow'],
  grassClump: ['foliage', 'foliageDark', 'bloom', 'pollen', 'fruit', 'grain', 'grassDry', 'snow'],
  sapling: ['barkLight', 'foliage', 'foliageDark', 'bloom', 'pollen', 'snow'],
  // ---- 状态形态与专类形态 ----
  sprout: ['barkLight', 'foliage', 'foliageDark', 'bloom', 'pollen', 'fruit', 'snow'],
  withered: ['barkLight', 'foliage', 'foliageDark', 'withered', 'witheredDark', 'witheredStem', 'witheredNeedle'],
  blossomTree: ['barkDark', 'barkLight', 'foliage', 'foliageDark', 'bloom', 'pollen', 'snow'],
  bamboo: ['bamboo', 'bambooCulm', 'foliageDeep', 'bloom', 'pollen', 'snow'],
  orchid: ['bamboo', 'foliage', 'foliageDeep', 'bloom', 'pollen'],
};

/** 干枯状态额外需要的色键（withered 原型 */
const WITHERED_PALETTE_KEYS: readonly PaletteKey[] = [
  'withered', 'witheredDark', 'witheredStem', 'witheredNeedle',
];

/** 会开花的原型 */
export const BLOOM_ARCHETYPES: readonly string[] = [
  'broadleaf', 'palm', 'acacia', 'baobab', 'shrub', 'succulent', 'bramble', 'grassClump', 'sapling',
  'blossomTree', 'bamboo', 'orchid', 'conifer',
];

/** 会结果的原型 */
export const FRUIT_ARCHETYPES: readonly string[] = [
  'broadleaf', 'palm', 'acacia', 'baobab', 'shrub', 'succulent', 'bramble', 'grassClump',
];

/** 该原型是否支持开花版本（针叶树支持 */
export function canBloom(archetype: string): boolean {
  return BLOOM_ARCHETYPES.includes(archetype);
}

/** 该原型是否支持结实版本（枯木 / 幼苗 / 兰花 */
export function canFruit(archetype: string): boolean {
  return FRUIT_ARCHETYPES.includes(archetype);
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

/** 解析某个物种在指定季节下的实际颜色表 */
export function resolvePalette(variant: PlantVariantDef, season: Season, state: PlantState = 'normal'): ResolvedPalette {
  const out: Record<string, string> = {};
  for (const key of paletteKeysOf(variant, state)) {
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

/** 该物种在指定状态下会用到哪些色板键 */
function paletteKeysOf(variant: PlantVariantDef, state: PlantState): readonly string[] {
  const resolved = resolveState(variant, state);
  const keys = new Set<string>([
    ...(ARCHETYPE_PALETTE_KEYS[resolved.archetype] ?? []),
    ...Object.values(variant.palette ?? {}),
  ]);
  if (resolved.bloom) keys.add('bloom');
  if (resolved.fruit) keys.add('fruit');
  if (state === 'withered') for (const k of WITHERED_PALETTE_KEYS) keys.add(k);
  return [...keys];
}

// 
// 物种条目：状态条目从成株条目继承参数与色板
// 

const BASE_ID = /@[a-z]+$/;

/** 从 "shu.qinggang@seedling */
export function baseIdOf(id: string): string {
  return id.replace(BASE_ID, '');
}

/** 一个物种的六态视图 */
interface SpeciesEntry {
  base: PlantVariantDef;
  byState: Map<PlantState, PlantVariantDef>;
  /** 该物种完整的状态清单（按 PLANT_STATE */
  states: PlantState[];
}

// 
// 生命周期状态：六态如何从"成熟健株"的物种参数派生
// 

/** 幼苗相对成株的尺寸：只影响尺寸，不改变物种比例 */
const SEEDLING_SCALE = 0.24;
/** 亚成相对成株的尺寸 */
const SUBADULT_SCALE = 0.55;
/** 结实时果实相对常规果径的放大倍率 */
const FRUITING_SCALE = 1.18;

/** 生命周期状态 */
export const STATE_DEFAULTS: PlantStateTable = {
  planted: {
    label: '种植',
    latin: 'planted',
    code: 0,
    // 没有模型
    modeless: true,
    fallback: 'normal',
  },
  normal: {
    label: '普通',
    latin: 'normal',
    code: 1,
    bloomMode: 'species',
    fruitMode: 'species',
  },
  seedling: {
    label: '幼苗',
    latin: 'seedling',
    code: 2,
    archetype: 'sprout',
    stateForm: 'sprout',
    bloomMode: 'none',
    fruitMode: 'none',
    paramScale: SEEDLING_SCALE,
    paramPatch: { trunkRadius: 0.85, blobs: 2, canopyRatio: 0.55 },
  },
  subadult: {
    label: '亚成',
    latin: 'subadult',
    code: 3,
    bloomMode: 'none',
    fruitMode: 'none',
    paramScale: SUBADULT_SCALE,
    paramPatch: { fruitScale: 0.7 },
  },
  flowering: {
    label: '开花',
    latin: 'flowering',
    code: 4,
    bloomMode: 'force',
    fruitMode: 'none',
    // 只给"花量"兜底
    // 否则小灌木也会得到 1 格大的花。
    paramPatch: { bloomCount: 7 },
  },
  fruiting: {
    label: '结实',
    latin: 'fruiting',
    code: 5,
    bloomMode: 'none',
    fruitMode: 'force',
    paramPatch: { fruitCount: 4, fruitScale: FRUITING_SCALE },
  },
  withered: {
    label: '干枯',
    latin: 'withered',
    code: 6,
    archetype: 'withered',
    stateForm: 'withered',
    bloomMode: 'none',
    fruitMode: 'none',
    paramScale: 0.82,
    paramPatch: { snow: 0 },
  },
};

/** 尺寸类参数 */
const SCALED_PARAM_KEYS: readonly (keyof PlantParams)[] = [
  'height', 'trunk', 'trunkRadius', 'trunkTopRadius', 'limbLength', 'canopy', 'layerRadius',
  'frondLength', 'nodeStep', 'spread', 'bloomRadius', 'fruitRadius', 'snow',
];

/** 状态查询 */
export function stateDef(state: PlantState): PlantStateDef {
  const def = STATE_DEFAULTS[state];
  if (!def) throw new Error(`[Vegetation] 未知植物状态: ${state}（可用: ${PLANT_STATES.join(', ')}）`);
  return def;
}

/** 实际生成用的状态 */
export function effectiveState(state: PlantState): PlantState {
  const def = STATE_DEFAULTS[state] as PlantStateDef | undefined;
  if (!def) return 'normal';
  return def.modeless ? (def.fallback ?? 'normal') : state;
}

/** 该物种支持的状态（先查参数表声明 */
function supportedStates(species: SpeciesEntry): PlantState[] {
  if (species.states.length > 0) return species.states;
  return defaultStatesOf();
}

/** 未显式声明 states 时的推断 */
function defaultStatesOf(): PlantState[] {
  return [...PLANT_MODELED_STATES];
}

/** 状态派生结果 */
interface ResolvedState {
  archetype: string;
  params: PlantParams;
  bloom: boolean;
  fruit: boolean;
}

/** 把物种参数与状态派生叠在一起（不做四层默认值合并 */
function resolveState(variant: PlantVariantDef, state: PlantState): ResolvedState {
  const def = stateDef(state);
  const base: PlantParams = { ...variant.params };
  const archetype = def.archetype ?? variant.archetype;
  const params: PlantParams = { ...base };
  if (def.paramScale !== undefined && def.paramScale !== 1) {
    for (const key of SCALED_PARAM_KEYS) {
      const value = params[key];
      if (typeof value === 'number' && Number.isFinite(value)) {
        (params as Record<string, unknown>)[key] = value * def.paramScale;
      }
    }
  }
  if (def.paramPatch) Object.assign(params, def.paramPatch);
  if (def.stateForm) params.stateForm = def.stateForm;
  // 枯木不积雪：雪落在枯枝上没有意义，也会掩盖枯枝剪影
  if (state === 'withered') params.snow = 0;

  const speciesBloom = Math.round(params.bloomCount ?? 0) > 0;
  const speciesFruit = Math.round(params.fruitCount ?? 0) > 0;
  // flowerless / fruitless
  // 于是开花/结实态不会去凑花凑果
  const bloom = !params.flowerless
    && (def.bloomMode === 'force' || (def.bloomMode === 'species' && speciesBloom)) && canBloom(archetype);
  const fruit = !params.fruitless
    && (def.fruitMode === 'force' || (def.fruitMode === 'species' && speciesFruit)) && canFruit(archetype);
  return { archetype, params, bloom, fruit };
}

// 
// 参数分层合并
// 

/** 五层合并 */
export function mergeParams(variant: PlantVariantDef, state: PlantState = 'normal'): PlantParams {
  const climate = CLIMATE_DEFAULTS[variant.climate];
  if (!climate) throw new Error(`[Vegetation] 植物 ${variant.id} 的生长带非法: ${variant.climate}`);
  const resolved = resolveState(variant, state);
  const archetype = ARCHETYPE_DEFAULTS[resolved.archetype];
  if (!archetype) throw new Error(`[Vegetation] 植物 ${variant.id} 的状态原型 "${resolved.archetype}" 未登记默认参数`);
  return { ...GLOBAL_DEFAULTS, ...climate, ...archetype, ...resolved.params };
}

/** 把参数表的一条定义装配成可直接生成的 Plant */
export function toPlantDef(variant: PlantVariantDef, season: Season, bloom?: PlantBloomOptions): PlantDef {
  const seed = variant.seed ?? 0;
  const asked: PlantState = bloom?.state ?? (bloom?.bloom ? 'flowering' : 'normal');
  // 先过"只有接口的状态"这道闸（0 种植 -> 1
  const requested = effectiveState(asked);
  const species = speciesOf(variant);
  // 状态与原型不相容时（针叶树的花期/果期）退回普通态
  const allowed = species ? supportedStates(species) : [requested];
  const declared: PlantState = allowed.includes(requested) ? requested : 'normal';
  const probe = resolveState(variant, declared);
  const state: PlantState = (declared === 'flowering' && !probe.bloom) || (declared === 'fruiting' && !probe.fruit)
    ? 'normal'
    : declared;
  const resolved = state === declared ? probe : resolveState(variant, state);
  const blooming = (resolved.bloom || (state === 'normal' && !!bloom?.bloom)) && canBloom(resolved.archetype);
  // 形态种子
  return {
    id: variant.id,
    archetype: resolved.archetype,
    climate: variant.climate,
    name: variant.name,
    latin: variant.latin,
    seed,
    tags: variant.tags ?? [],
    params: mergeParams(variant, state),
    palette: resolvePalette(variant, season, state),
    buildSeed: hashString(`${resolved.archetype}:${baseIdOf(variant.id)}:${seed}`),
    state,
    // 积雪只在秋末与冬季出现
    snow: (season === 'winter' || season === 'autumn') && state !== 'withered',
    bloom: blooming,
    // 只有开花时才带上接口花色
    bloomColor: blooming ? bloom?.bloomColor : undefined,
    fruit: resolved.fruit,
  };
}

/** 尺寸倍率：物种 scale 缺省为 1 */
function buildOptionsOf(variant: PlantVariantDef): { scale: number } {
  const raw = variant.scale;
  const scale = typeof raw === 'number' && Number.isFinite(raw) && raw > 0 ? raw : 1;
  return { scale };
}

// 
// 装配与校验
// 

/** 第一遍 */
const speciesById = new Map<string, SpeciesEntry>();
/** 全部条目（成株 + 状态），id 唯一 */
const allVariants: PlantVariantDef[] = [];

function speciesOf(variant: PlantVariantDef): SpeciesEntry | undefined {
  return speciesById.get(baseIdOf(variant.id));
}

for (const { file, data } of PLANT_FILES) {
  // 参数表**不再需要写 version**
  // 平时它只是每份文件里的一行冗余噪声
  if (data.version !== undefined && data.version !== PLANT_DATA_VERSION) {
    throw new Error(`[Vegetation] ${file} 的 version=${data.version}，当前支持 ${PLANT_DATA_VERSION}（若确实要升级格式，请同步提升 Plants.ts 的 PLANT_DATA_VERSION）`);
  }
  if (!Array.isArray(data.plants) || data.plants.length === 0) {
    throw new Error(`[Vegetation] ${file} 缺少 plants 数组`);
  }
  // 成株条目先入册，保证同文件内的状态条目一定能继承到
  for (const p of data.plants) {
    if (p.state) continue;
    const base: PlantVariantDef = { ...p, source: file };
    if (speciesById.has(base.id)) {
      throw new Error(`[Vegetation] 物种 id 重复: ${base.id}（${speciesById.get(base.id)!.base.source} 与 ${file}）`);
    }
    speciesById.set(base.id, { base, byState: new Map<PlantState, PlantVariantDef>(), states: [] });
    allVariants.push(base);
  }
  for (const p of data.plants) {
    if (!p.state) continue;
    const species = speciesById.get(baseIdOf(p.id));
    if (!species) {
      throw new Error(`[Vegetation] ${file} 的状态条目 ${p.id} 找不到成株条目 ${baseIdOf(p.id)}（同文件内必须有该物种的成株定义）`);
    }
    if (species.byState.has(p.state)) {
      throw new Error(`[Vegetation] 物种 ${species.base.id} 的 ${p.state} 状态重复定义`);
    }
    // 继承成株条目的形状与身份
    const merged: PlantVariantDef = {
      ...species.base,
      ...p,
      id: p.id,
      state: p.state,
      archetype: p.archetype ?? species.base.archetype,
      name: p.name ?? species.base.name,
      latin: p.latin ?? species.base.latin,
      seed: p.seed ?? species.base.seed,
      tags: p.tags ?? species.base.tags,
      params: { ...species.base.params, ...p.params },
      palette: { ...species.base.palette, ...p.palette },
      source: file,
    };
    species.byState.set(p.state, merged);
    allVariants.push(merged);
  }
}

/** 物种清单 */
for (const species of speciesById.values()) {
  const declared = species.base.states;
  const states = declared && declared.length > 0 ? declared : defaultStatesOf();
  const seen = new Set<PlantState>();
  const ordered: PlantState[] = [];
  for (const s of PLANT_STATES) {
    if (!states.includes(s)) continue;
    if (seen.has(s)) throw new Error(`[Vegetation] 物种 ${species.base.id} 的状态清单有重复项: ${s}`);
    seen.add(s);
    ordered.push(s);
    // 0 种植这类"只有接口没有模型"的状态不需要参数
    if (s !== 'normal' && !stateDef(s).modeless && !species.byState.has(s)) {
      throw new Error(`[Vegetation] 物种 ${species.base.id} 声明了状态 ${s}，但参数表里没有 ${species.base.id}@${s} 条目`);
    }
  }
  for (const s of species.byState.keys()) {
    if (!seen.has(s)) {
      throw new Error(`[Vegetation] 物种 ${species.base.id} 有 ${s} 状态条目，但 states 清单里没声明它`);
    }
  }
  // 归一化到固定顺序
  species.states = ordered;
  // 状态条目的 archetype 缺省时补成成株原型
  for (const [s, v] of species.byState) {
    if (!v.archetype) throw new Error(`[Vegetation] 物种 ${species.base.id} 的状态 ${s} 缺少 archetype`);
  }
}

// 
// 校验：原型存在、生长带合法、色板键存在、状态可派生
// 

const byId = new Map<string, PlantVariantDef>();
for (const v of allVariants) {
  if (byId.has(v.id)) throw new Error(`[Vegetation] 植物 id 重复: ${v.id}（${byId.get(v.id)!.source} 与 ${v.source}）`);
  if (!ARCHETYPES[v.archetype]) {
    throw new Error(`[Vegetation] 植物 ${v.id} 的原型不存在: ${v.archetype}（可用: ${Object.keys(ARCHETYPES).join(', ')}）`);
  }
  if (!(CLIMATE_ZONES as readonly string[]).includes(v.climate)) {
    throw new Error(`[Vegetation] 植物 ${v.id} 的生长带非法: ${v.climate}`);
  }
  if (v.states) {
    for (const state of v.states) {
      if (!(PLANT_STATES as readonly string[]).includes(state)) {
        throw new Error(`[Vegetation] 植物 ${v.id} 声明了未知状态: ${state}（可用: ${PLANT_STATES.join(', ')}）`);
      }
    }
    // 注意
    // 原型不支持时（如针叶树没有结实态）
    // 数据里多写一个状态不会让整个植被系统加载失败。
  }
  validateOverrideKeys(v);
  byId.set(v.id, v);
}

/** 六态全量派生一遍 */
for (const species of speciesById.values()) {
  for (const state of species.states) {
    const variant = state === 'normal' ? species.base : species.byState.get(state)!;
    resolveState(variant, state);
    resolvePalette(variant, 'spring', state);
  }
}

// 
// 查询接口
// 

/** 四季色板缓存 */
const paletteCache = new Map<string, ResolvedPalette>();

function cachedPalette(variant: PlantVariantDef, season: Season, state: PlantState): ResolvedPalette {
  const key = `${variant.id}|${season}|${state}`;
  let p = paletteCache.get(key);
  if (!p) {
    p = resolvePalette(variant, season, state);
    paletteCache.set(key, p);
  }
  return p;
}

/** 植物 id 常量表 */
export const Plant = Object.freeze(
  Object.fromEntries(allVariants.map((v) => [v.id, v.id])) as Record<string, string>,
);

/** 按 id 取参数表定义（含状态条目） */
export function getPlantVariant(id: string): PlantVariantDef | undefined {
  return byId.get(id);
}

/** 该物种支持的状态；未知物种返回空数组 */
export function plantStatesOf(id: string): readonly PlantState[] {
  const species = speciesById.get(baseIdOf(id));
  if (!species) return [];
  // **七态统一**
  // 上层玩法不需要按物种分支
  return PLANT_STATES;
}

/** 该物种在参数表里实际写了条目 / 声明的状态（六 */
export function modeledStatesOf(id: string): readonly PlantState[] {
  const species = speciesById.get(baseIdOf(id));
  if (!species) return [];
  return species.states;
}

/** 该物种在指定状态下是否可用（七态统一 */
export function supportsState(id: string, state: PlantState): boolean {
  const species = speciesById.get(baseIdOf(id));
  if (!species) return false;
  return plantStatesOf(id).includes(state);
}

/** 全部物种 id（不含状态条目），参数表顺序 */
export const PLANT_SPECIES_IDS: readonly string[] = [...speciesById.keys()];

/** 全部植物 id（含各状态条目），参数表顺序 */
export const PLANT_IDS: readonly string[] = allVariants.map((v) => v.id);

/** 全部植物定义（含状态条目，未按季节解析色板前） */
export const PLANT_VARIANTS: readonly PlantVariantDef[] = allVariants;

/** 按生长带筛选物种（直接读参数表的 climate */
export function plantsByClimate(climate: ClimateZone): readonly PlantVariantDef[] {
  return [...speciesById.values()].map((s) => s.base).filter((v) => v.climate === climate);
}

/** 按标签筛选物种（如 'conifer'、'flo */
export function plantsByTag(tag: string): readonly PlantVariantDef[] {
  return [...speciesById.values()].map((s) => s.base).filter((v) => (v.tags ?? []).includes(tag));
}

/** 可自然散布的物种 */
export function scatterablePlants(climate?: ClimateZone): readonly PlantVariantDef[] {
  return [...speciesById.values()].map((s) => s.base)
    .filter((v) => climate === undefined || v.climate === climate)
    .filter((v) => !(v.tags ?? []).includes('no_scatter'));
}

/** 生成一株植物的低模几何 */
export function buildPlantById(id: string, season: Season = 'summer', bloom?: PlantBloomOptions): PlantGeometry {
  const species = speciesById.get(baseIdOf(id));
  if (!species) throw new Error(`[Vegetation] 未知植物 id: ${id}`);
  const suffix = id.slice(baseIdOf(id).length).replace(/^@/, '');
  const state: PlantState = bloom?.state ?? (suffix && (PLANT_STATES as readonly string[]).includes(suffix) ? (suffix as PlantState) : 'normal');
  const variant = state === 'normal' ? species.base : (species.byState.get(state) ?? species.base);
  const def = toPlantDef(variant, season, { ...bloom, state });
  const geo = buildPlant({ ...def, palette: cachedPalette(variant, season, def.state) });
  scaleGeometry(geo, buildOptionsOf(species.base).scale);
  return geo;
}

/** 该物种开花版本实际使用的花色（#rrggbb） */
export function bloomColorOf(variant: PlantVariantDef, season: Season = 'spring'): string | null {
  if (!canBloom(variant.archetype)) return null;
  const species = speciesById.get(baseIdOf(variant.id));
  if (species && !species.states.includes('flowering')) return null;
  const flowering = species?.byState.get('flowering') ?? variant;
  return cachedPalette(flowering, season, 'flowering').bloom ?? DEFAULT_BLOOM_COLOR;
}

/** 就地缩放几何（含包围半径与高度） */
export function scaleGeometry(geo: PlantGeometry, scale: number): void {
  if (scale === 1) return;
  for (let i = 0; i < geo.positions.length; i++) geo.positions[i] *= scale;
  geo.radius *= scale;
  geo.height *= scale;
}

/** 生成时的完整选项（供导出脚本按季节/状态批量产出） */
export function buildOptions(season: Season, bloom?: PlantBloomOptions): PlantBuildOptions {
  return { seed: 0, season, state: bloom?.state ?? 'normal', bloom: bloom?.bloom, bloomColor: bloom?.bloomColor };
}

/** 诊断：统计物种数、几何面数与各材质槽三角形数 */
export interface PlantStat {
  id: string;
  name: string;
  archetype: string;
  climate: ClimateZone;
  /** 请求的状态（可能是"只有接口"的 0 种植） */
  state: PlantState;
  /** 实际生成用的形态状态 */
  renderedAs: PlantState;
  height: number;
  radius: number;
  triangles: number;
  vertices: number;
  materials: string;
}

export function statOf(id: string, season: Season = 'summer', state: PlantState = 'normal'): PlantStat {
  const variant = getPlantVariant(id) ?? speciesById.get(baseIdOf(id))?.base;
  if (!variant) throw new Error(`[Vegetation] 未知植物 id: ${id}`);
  const geo = buildPlantById(id, season, { state });
  const def = toPlantDef(variant, season, { state });
  return {
    id,
    name: variant.name,
    archetype: def.archetype,
    climate: variant.climate,
    state,
    renderedAs: def.state,
    height: geo.height,
    radius: geo.radius,
    triangles: geo.triangleCount,
    vertices: geo.vertexCount,
    materials: geo.groups.map((g) => `${g.mat}:${g.count / 3}`).join(' '),
  };
}

/** 物种 × 状态的对照表（清单 / 文档 / 校验 */
export function stateMatrixOf(): readonly {
  id: string;
  name: string;
  archetype: string;
  climate: ClimateZone;
  /** 参数表里实际写了条目的状态（六种有模型的状态） */
  states: readonly PlantState[];
  /** 统一七态契约（含 0 种植） */
  allStates: readonly PlantState[];
}[] {
  return [...speciesById.values()].map((s) => ({
    id: s.base.id,
    name: s.base.name,
    archetype: s.base.archetype,
    climate: s.base.climate,
    states: s.states,
    allStates: PLANT_STATES,
  }));
}

/** 一个物种的全部状态几何（导出与预览遍历用） */
export function buildAllStates(id: string, season: Season = 'summer'): readonly { state: PlantState; geometry: PlantGeometry }[] {
  return plantStatesOf(id)
    .filter((s) => !stateDef(s).modeless)
    .map((state) => ({ state, geometry: buildPlantById(id, season, { state }) }));
}

export type { PlantDef, PlantGeometry, Season };
export { buildPlant };
