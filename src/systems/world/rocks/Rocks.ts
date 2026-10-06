/** 岩石注册表 */

import palettesJson from '../../../../content/data/world/rocks/palettes.json';
import chunkTypesJson from '../../../../content/data/world/chunk_types.json';
import rockBoulder from '../../../../content/data/world/rocks/rock.boulder.json';
import rockBlock from '../../../../content/data/world/rocks/rock.block.json';
import rockLedge from '../../../../content/data/world/rocks/rock.ledge.json';
import rockSlab from '../../../../content/data/world/rocks/rock.slab.json';
import rockShard from '../../../../content/data/world/rocks/rock.shard.json';
import rockSpire from '../../../../content/data/world/rocks/rock.spire.json';
import rockStub from '../../../../content/data/world/rocks/rock.stub.json';
import rockRubble from '../../../../content/data/world/rocks/rock.rubble.json';
import rockFloe from '../../../../content/data/world/rocks/rock.floe.json';
import { hashString } from '../../../core/math/Random';
import { buildRock, FAMILY_PALETTE, ROCK_ARCHETYPES, ROCK_ARCHETYPE_DEFAULTS } from './archetypes';
import type { RockGeometry } from './geometry';
import {
  ROCK_COLOR_SLOTS, ROCK_FORMS, ROCK_FORM_CODES, ROCK_MATERIAL_FAMILIES, ROCK_MATERIALS,
  type ResolvedRockPalette, type RockBuildOptions, type RockBuildDef, type RockDef, type RockFile,
  type RockForm, type RockMaterial, type RockMaterialFamily, type RockPalette, type RockPaletteEntry,
  type RockPaletteFile, type RockPaletteKey, type RockParams, type Season,
} from './types';

/** 岩性色板的五个语义槽（校验参数表的 palett */
const PALETTE_SLOTS: readonly string[] = ['body', 'band', 'cap', 'base', 'crust'];

/** 参数表清单 */
function asRockFile(data: unknown): RockFile {
  return data as RockFile;
}

export const ROCK_FILES: readonly { file: string; data: RockFile }[] = [
  { file: 'rock.boulder.json', data: asRockFile(rockBoulder) },
  { file: 'rock.block.json', data: asRockFile(rockBlock) },
  { file: 'rock.ledge.json', data: asRockFile(rockLedge) },
  { file: 'rock.slab.json', data: asRockFile(rockSlab) },
  { file: 'rock.shard.json', data: asRockFile(rockShard) },
  { file: 'rock.spire.json', data: asRockFile(rockSpire) },
  { file: 'rock.stub.json', data: asRockFile(rockStub) },
  { file: 'rock.rubble.json', data: asRockFile(rockRubble) },
  { file: 'rock.floe.json', data: asRockFile(rockFloe) },
];

/** 参数表格式版本 */
export const ROCK_DATA_VERSION = 1;

const paletteData = palettesJson as RockPaletteFile;

// 
// 色板
// 

/** 已注册色板键 -> 色阶（岩性 + 覆被） */
export const ROCK_PALETTES: ReadonlyMap<string, RockPaletteEntry> = new Map<string, RockPaletteEntry>([
  ...Object.entries(paletteData.stone ?? {}),
  ...Object.entries(paletteData.cover ?? {}),
]);

const HEX = /^#[0-9a-fA-F]{6}$/;

function paletteEntry(key: string): RockPaletteEntry {
  const e = ROCK_PALETTES.get(key);
  if (!e) {
    throw new Error(`[Rocks] 未知色板键 "${key}"，可用键: ${[...ROCK_PALETTES.keys()].sort().join(', ')}`);
  }
  return e;
}

/** 取色阶 */
function entryColor(entry: RockPaletteEntry, season: Season): string {
  return entry[season] ?? entry.default;
}

/** 解析一块岩石在指定季节的实际颜色表 */
export function resolveRockPalette(def: RockDef, season: Season, chunkType?: number): ResolvedRockPalette {
  const family = FAMILY_PALETTE[def.family];
  if (!family) throw new Error(`[Rocks] 岩石 ${def.id} 的岩性族非法: ${def.family}`);
  // 三层由低到高合并，后写的槽覆盖先写的
  const merged: Required<RockPalette> = { ...family, ...(def.palette ?? {}) };
  const override = chunkType === undefined ? undefined : def.chunkPalette?.[String(chunkType)];
  if (override) Object.assign(merged, override);
  const out: ResolvedRockPalette = {};
  for (const slot of ROCK_COLOR_SLOTS) {
    const key = merged[slot];
    if (!key) continue;
    const value = entryColor(paletteEntry(key), season);
    if (!HEX.test(value)) throw new Error(`[Rocks] 岩石 ${def.id} 的色板键 ${key} 色值非法: ${value}`);
    out[slot] = value;
  }
  return out;
}

// 
// 区块类型：标签校验与"这个区块用什么岩性"
// 

interface ChunkTypeLike {
  id: number;
  key: string;
  name: string;
  generate?: boolean;
}

const CHUNK_TYPES = chunkTypesJson as unknown as ChunkTypeLike[];

/** 区块 key -> id（供 `chunk:<k */
const CHUNK_ID_BY_KEY = new Map<string, number>(CHUNK_TYPES.map((c) => [c.key, c.id]));
/** 区块 id -> 区块定义 */
const CHUNK_BY_ID = new Map<number, ChunkTypeLike>(CHUNK_TYPES.map((c) => [c.id, c]));
/** 可生成区块（`generate !== fals */
export const ROCK_CHUNK_IDS: readonly number[] = CHUNK_TYPES.filter((c) => c.generate !== false).map((c) => c.id);

/** 把 `chunk:*` 标签解析成区块 id 集合 */
function chunkIdsOfTags(id: string, tags: readonly string[] | undefined): { chunks: number[]; others: string[] } {
  const chunks: number[] = [];
  const others: string[] = [];
  for (const tag of tags ?? []) {
    if (!tag.startsWith('chunk:')) {
      others.push(tag);
      continue;
    }
    const raw = tag.slice('chunk:'.length);
    // 同时接受 `chunk:103` 与 `chun
    // key 便于人读且不怕以后 id 调整
    const byKey = CHUNK_ID_BY_KEY.get(raw);
    const byId = /^\d+$/.test(raw) ? CHUNK_BY_ID.get(Number(raw)) : undefined;
    const found = byKey ?? byId?.id;
    if (found === undefined) {
      throw new Error(`[Rocks] 岩石 ${id} 的标签 "chunk:${raw}" 找不到对应区块（可用 id: ${ROCK_CHUNK_IDS.join(', ')}）`);
    }
    if ((byId?.generate === false) || (byKey !== undefined && CHUNK_BY_ID.get(byKey)?.generate === false)) {
      throw new Error(`[Rocks] 岩石 ${id} 的标签 "chunk:${raw}" 指向不生成的区块（虚空不放置岩石）`);
    }
    if (!chunks.includes(found)) chunks.push(found);
  }
  return { chunks, others };
}

// 
// 装配
// 

/** 装配后的岩石单位（`chunkPalette` */
export interface RockUnitDef extends RockDef {
  /** 参数表文件名 */
  source: string;
  /** 该单位适用的区块 id（由 `chunk:*` */
  chunks: readonly number[];
  /** 非区块标签（语义标签 */
  labels: readonly string[];
}

const units: RockUnitDef[] = [];
const byId = new Map<string, RockUnitDef>();

for (const { file, data } of ROCK_FILES) {
  if (data.version !== undefined && data.version !== ROCK_DATA_VERSION) {
    throw new Error(`[Rocks] ${file} 的 version=${data.version}，当前支持 ${ROCK_DATA_VERSION}（若确实要升级格式，请同步提升 Rocks.ts 的 ROCK_DATA_VERSION）`);
  }
  if (!Array.isArray(data.rocks) || data.rocks.length === 0) {
    throw new Error(`[Rocks] ${file} 缺少 rocks 数组`);
  }
  for (const raw of data.rocks) {
    if (!raw.id) throw new Error(`[Rocks] ${file} 里有条目缺少 id`);
    if (byId.has(raw.id)) {
      throw new Error(`[Rocks] 岩石 id 重复: ${raw.id}（${byId.get(raw.id)!.source} 与 ${file}）`);
    }
    if (!ROCK_ARCHETYPES[raw.archetype]) {
      throw new Error(`[Rocks] 岩石 ${raw.id} 的原型不存在: ${raw.archetype}（可用: ${Object.keys(ROCK_ARCHETYPES).join(', ')}）`);
    }
    if (!(ROCK_MATERIAL_FAMILIES as readonly string[]).includes(raw.family)) {
      throw new Error(`[Rocks] 岩石 ${raw.id} 的岩性族非法: ${raw.family}（可用: ${ROCK_MATERIAL_FAMILIES.join(', ')}）`);
    }
    // 色板
    for (const [slot, key] of Object.entries(raw.palette ?? {})) {
      if (!PALETTE_SLOTS.includes(slot)) {
        throw new Error(`[Rocks] 岩石 ${raw.id} 的 palette 槽非法: ${slot}（可用: ${PALETTE_SLOTS.join(' / ')}）`);
      }
      paletteEntry(key as string);
    }
    for (const [chunkId, over] of Object.entries(raw.chunkPalette ?? {})) {
      const chunk = CHUNK_BY_ID.get(Number(chunkId));
      if (!chunk) throw new Error(`[Rocks] 岩石 ${raw.id} 的 chunkPalette 引用了未知区块 id: ${chunkId}`);
      if (chunk.generate === false) throw new Error(`[Rocks] 岩石 ${raw.id} 的 chunkPalette 引用了不生成的区块: ${chunkId}`);
      for (const key of Object.values(over)) paletteEntry(key as string);
    }
    const { chunks, others } = chunkIdsOfTags(raw.id, raw.tags);
    const unit: RockUnitDef = { ...raw, source: file, chunks, labels: others, tags: raw.tags ?? [] };
    units.push(unit);
    byId.set(unit.id, unit);
  }
}

/** 岩石单位 id 常量表：代码里引用具体岩石时用这里 */
export const Rock = Object.freeze(
  Object.fromEntries(units.map((u) => [u.id, u.id])) as Record<string, string>,
);

export {
  ROCK_COLOR_SLOTS, ROCK_FORMS, ROCK_FORM_CODES, ROCK_MATERIALS, ROCK_MATERIAL_FAMILIES,
} from './types';

/** 全部岩石单位（参数表顺序） */
export const ROCK_DEFS: readonly RockUnitDef[] = units;
/** 全部岩石单位 id */
export const ROCK_IDS: readonly string[] = units.map((u) => u.id);

/** 区块 -> 可用岩石单位 的速查表（由参数表的 */
export const ROCKS_BY_CHUNK: ReadonlyMap<number, readonly string[]> = (() => {
  const map = new Map<number, string[]>();
  for (const def of units) {
    for (const chunk of def.chunks) {
      const list = map.get(chunk) ?? [];
      list.push(def.id);
      map.set(chunk, list);
    }
  }
  return map;
})();

/** 按 id 取单位定义 */
export function getRockDef(id: string): RockUnitDef | undefined {
  return byId.get(id);
}

/** 按语义标签筛选（如 `hard`、`wet`、` */
export function rocksByTag(tag: string): readonly RockUnitDef[] {
  return units.filter((u) => (u.tags ?? []).includes(tag));
}

/** 某个区块可用的岩石单位（未登记任何单位时返回空数组 */
export function rocksForChunk(chunkType: number): readonly RockUnitDef[] {
  const ids = ROCKS_BY_CHUNK.get(chunkType);
  if (!ids) return [];
  return ids.map((id) => byId.get(id)!).filter(Boolean);
}

// 
// 参数分层合并
// 

/** 所有单位的公共默认值（原型默认之上再兜一层） */
export const ROCK_GLOBAL_DEFAULTS: RockParams = Object.freeze({
  aspect: 0.62,
  sides: 6,
  jitter: 0.18,
  crustSlope: 0.42,
  variant: true,
});

/** 三层合并：通用默认 -> 原型默认 -> 单位参数 */
export function mergeRockParams(def: RockDef): RockParams {
  const archetype = ROCK_ARCHETYPE_DEFAULTS[def.archetype];
  if (!archetype) throw new Error(`[Rocks] 岩石 ${def.id} 的原型 "${def.archetype}" 未登记默认参数`);
  return { ...ROCK_GLOBAL_DEFAULTS, ...archetype, ...(def.params ?? {}) };
}

// 
// 形态档派生
// 

/** 形态档的派生规则 */
export interface ResolvedRockForm {
  form: RockForm;
  /** 追加到 params 上的覆盖 */
  patch: RockParams;
  /** 覆被材质槽（关闭覆被时为 undefined） */
  crustMaterial?: RockMaterial;
  /** 覆被色板键（关闭覆被时为 undefined） */
  crustColor?: RockPaletteKey;
  /** 是否叠置第二块小石 */
  stacked: boolean;
}

/** 该单位的"默认覆被色" */
export function defaultCrustKey(def: RockDef): RockPaletteKey {
  return (def.palette?.crust ?? FAMILY_PALETTE[def.family]?.crust ?? 'moss') as RockPaletteKey;
}

/** 把形态档 + 季节 + 岩性折成一次生成用的参数 */
export function resolveRockForm(def: RockDef, options: RockBuildOptions = {}): ResolvedRockForm {
  const form: RockForm = options.form ?? 'outcrop';
  if (!(ROCK_FORMS as readonly string[]).includes(form)) {
    throw new Error(`[Rocks] 未知岩石形态档: ${form}（可用: ${ROCK_FORMS.join(', ')}）`);
  }
  const season: Season = options.season ?? 'summer';
  const crustKey = options.crustColor ?? defaultCrustKey(def);
  const crustMaterial = crustMaterialOf(crustKey, def);
  const cover = options.crust === false
    ? undefined
    : { crustMaterial, crustColor: crustKey };
  switch (form) {
    // 半埋
    // 在任何地貌上都比"完整露头"更像原地长的。
    // 注意 **不能** 用 `sink = 原值 +
    // 预览图上直接看不见（`bottom` 会等于
    case 'buried':
      return {
        form,
        patch: {
          spreadScale: 1.18,
          sizeScale: 0.82,
          sink: scalarOf(def.params?.sink, 0.12) + 0.15,
        },
        stacked: false,
        ...cover,
      };
    // 叠置
    case 'stacked':
      return { form, patch: {}, stacked: true, ...cover };
    // 覆被
    case 'crusted':
      return { form, patch: { sizeScale: 0.94, crust: 0.85 }, stacked: false, ...cover };
    // 露头
    // 霜降之后崖面上先长霜
    default:
      return { form, patch: { crust: defaultCoverage(def, season) }, stacked: false, ...cover };
  }
}

/** 露头档的默认覆盖率 */
function defaultCoverage(def: RockDef, season: Season): number {
  const base = def.family === 'metamorphic' ? 0.3
    : def.family === 'igneous' ? 0.22
      : def.family === 'clastic' ? 0.18
        : 0.14;
  const winter = season === 'winter' ? 0.24 : season === 'autumn' ? 0.12 : 0;
  return Math.min(0.85, base + winter);
}

function scalarOf(value: number | undefined, fallback: number): number {
  return typeof value === 'number' && Number.isFinite(value) ? value : fallback;
}

/** 覆被键 -> 材质槽 */
function crustMaterialOf(key: RockPaletteKey, def: RockDef): RockMaterial {
  if (key === 'snowCap' || key === 'snow') return 'snow';
  if (key === 'ice' || key === 'iceDark') return 'ice';
  if (key === 'moss') return def.family === 'ice' ? 'ice' : 'moss';
  return 'moss';
}

// 
// 生成
// 

/** 生成一块岩石的低模几何 */
export function buildRockById(id: string, season: Season = 'summer', options: RockBuildOptions & { chunkType?: number } = {}): RockGeometry {
  const def = byId.get(id);
  if (!def) throw new Error(`[Rocks] 未知岩石 id: ${id}`);
  const shapeSeed = options.shapeSeed ?? 0;
  const resolved = resolveRockForm(def, { ...options, season });
  const finalParams: RockParams = { ...mergeRockParams(def), ...resolved.patch };
  const palette = resolveRockPalette(def, season, options.chunkType);
  // 覆被色可能不是 palette.crust（调用
  const crustRgb = resolved.crustColor ? entryColor(paletteEntry(resolved.crustColor), season) : undefined;
  const build: RockBuildDef = {
    id: def.id,
    archetype: def.archetype,
    name: def.name,
    latin: def.latin,
    family: def.family,
    seed: def.seed ?? 0,
    form: resolved.form,
    shapeSeed,
    crustMaterial: resolved.crustMaterial,
    crustColor: resolved.crustColor,
    crustRgb,
    params: finalParams,
    palette,
    // 形态种子
    buildSeed: hashString(`${def.archetype}:${def.id}:${def.seed ?? 0}`),
    season,
    // 只有覆被色是雪（覆被档 + 雪原 / 雪山 /
    snow: resolved.crustMaterial === 'snow',
  };
  const geo = buildRock(build);
  const scale = options.scale ?? 1;
  if (scale !== 1) scaleGeometry(geo, scale);
  return geo;
}

/** 就地缩放几何（含包围半径与高度、最低点） */
export function scaleGeometry(geo: RockGeometry, scale: number): void {
  if (scale === 1) return;
  for (let i = 0; i < geo.positions.length; i++) geo.positions[i] *= scale;
  geo.radius *= scale;
  geo.height *= scale;
  geo.bottom *= scale;
}

/** 一块岩石的四个形态档（导出与预览遍历用） */
export function buildAllForms(id: string, season: Season = 'summer', options: RockBuildOptions & { chunkType?: number } = {}): readonly { form: RockForm; geometry: RockGeometry }[] {
  return ROCK_FORMS.map((form) => ({ form, geometry: buildRockById(id, season, { ...options, form }) }));
}

// 
// 诊断
// 

/** 诊断 */
export interface RockStat {
  id: string;
  name: string;
  archetype: string;
  family: RockMaterialFamily;
  /** 请求的形态档 */
  form: RockForm;
  /** 实际使用的形态档（帧内都是同一值 */
  renderedAs: RockForm;
  season: Season;
  formCode: number;
  /** 主轴直径（格） */
  diameter: number;
  /** 高度（格） */
  height: number;
  /** 包围半径（格） */
  radius: number;
  /** 最低点（格，负数表示沉到地表以下） */
  bottom: number;
  triangles: number;
  vertices: number;
  /** 材质槽面数构成 */
  materials: string;
  /** 覆被（苔藓 / 雪 / 冰）占全部三角形的比例 */
  coverRatio: number;
  /** 实际使用的岩性色板（校验用） */
  palette: ResolvedRockPalette;
  chunks: readonly number[];
}

export function statOf(id: string, season: Season = 'summer', form: RockForm = 'outcrop', options: RockBuildOptions & { chunkType?: number } = {}): RockStat {
  const def = byId.get(id);
  if (!def) throw new Error(`[Rocks] 未知岩石 id: ${id}`);
  const geo = buildRockById(id, season, { ...options, form });
  const params = mergeRockParams(def);
  const cover = geo.coverRatio;
  const coverRatio = ROCK_MATERIALS
    .filter((m) => m !== 'solid')
    .reduce((sum, m) => sum + (cover[m] ?? 0), 0);
  return {
    id: def.id,
    name: def.name,
    archetype: def.archetype,
    family: def.family,
    form,
    renderedAs: form,
    season,
    formCode: ROCK_FORM_CODES[form],
    diameter: params.diameter ?? 0,
    height: geo.height,
    radius: geo.radius,
    bottom: geo.bottom,
    triangles: geo.triangleCount,
    vertices: geo.vertexCount,
    materials: geo.groups.map((g) => `${g.mat}:${g.count / 3}`).join(' '),
    coverRatio,
    palette: resolveRockPalette(def, season, options.chunkType),
    chunks: def.chunks,
  };
}

/** 单位 × 形态档的对照表（清单 / 文档 / 校 */
export function formMatrixOf(): readonly {
  id: string;
  name: string;
  archetype: string;
  family: RockMaterialFamily;
  forms: readonly RockForm[];
  chunks: readonly number[];
  labels: readonly string[];
}[] {
  return units.map((u) => ({
    id: u.id,
    name: u.name,
    archetype: u.archetype,
    family: u.family,
    forms: ROCK_FORMS,
    chunks: u.chunks,
    labels: u.labels,
  }));
}

export type { RockGeometry, RockForm, RockMaterial };
