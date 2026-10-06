/** 岩石（地表点缀）系统数据模型 */

/** 岩性（视觉材质族） */
export type RockMaterialFamily =
  /** 火成/深成岩 */
  | 'igneous'
  /** 沉积岩 */
  | 'sedimentary'
  /** 变质岩 */
  | 'metamorphic'
  /** 松散堆积物 */
  | 'clastic'
  /** 冰：冰壳、蓝冰 —— 半透明观感，冷色 */
  | 'ice';

export const ROCK_MATERIAL_FAMILIES: readonly RockMaterialFamily[] = [
  'igneous', 'sedimentary', 'metamorphic', 'clastic', 'ice',
];

/** 岩石形态档（**不是生命周期** */
export type RockForm =
  /** 露头：完整露出地表，正常尺寸（默认档） */
  | 'outcrop'
  /** 半埋：整体下沉、横向放大，像从土里长出来的一角 */
  | 'buried'
  /** 叠置：再叠一块小石（组合），有主次关系 */
  | 'stacked'
  /** 覆被 */
  | 'crusted';

export const ROCK_FORMS: readonly RockForm[] = ['outcrop', 'buried', 'stacked', 'crusted'];

/** 形态档编号 */
export const ROCK_FORM_CODES: Readonly<Record<RockForm, number>> = Object.freeze(
  Object.fromEntries(ROCK_FORMS.map((f, i) => [f, i])) as Record<RockForm, number>,
);

/** 编号 -> 形态档 */
export function rockFormOfCode(code: number): RockForm {
  return ROCK_FORMS[code] ?? 'outcrop';
}

/** 季节 */
export type Season = 'spring' | 'summer' | 'autumn' | 'winter';

export const SEASONS: readonly Season[] = ['spring', 'summer', 'autumn', 'winter'];

/** 色板键 */
export type RockPaletteKey =
  // ---- 通用 ----
  /** 通用石头 #7f7f7f（blocks.json */
  | 'stone'
  /** 基岩色（深，用于裂隙内部与叠置阴影） */
  | 'bedrock'
  /** 风化土壳 / 贴地的土色（半埋档的"土"） */
  | 'soil'
  // ---- 火成岩 ----
  | 'granite'
  | 'graniteDark'
  | 'granitePale'
  | 'basalt'
  | 'basaltDark'
  | 'basaltPale'
  // ---- 沉积岩 ----
  | 'limestone'
  | 'limestoneDark'
  | 'limestonePale'
  | 'sandstone'
  | 'sandstoneDark'
  | 'sandstonePale'
  | 'conglomerate'
  | 'conglomerateDark'
  | 'chalk'
  | 'travertine'
  | 'travertineDark'
  // ---- 变质岩 ----
  | 'slate'
  | 'slateDark'
  | 'slatePale'
  | 'shale'
  | 'shaleDark'
  | 'quartzite'
  | 'quartziteDark'
  | 'quartzitePale'
  // ---- 松散堆积 ----
  | 'gravel'
  | 'gravelDark'
  | 'scree'
  | 'screeDark'
  | 'glacialTill'
  // ---- 冰 ----
  | 'ice'
  | 'iceDark'
  | 'snow'
  // ---- 覆被（crusted 档）----
  /** 苔藓（湿林、沼泽、雨林） */
  | 'moss'
  /** 地衣（寒带、高地、裸岩） */
  | 'lichen'
  /** 雪盖（秋冬） */
  | 'snowCap'
  /** 干枯草屑（旱地、草原的贴地碎屑） */
  | 'dryGrass';

/** 几何材质槽 */
export type RockMaterial = 'solid' | 'moss' | 'snow' | 'ice';

export const ROCK_MATERIALS: readonly RockMaterial[] = ['solid', 'moss', 'snow', 'ice'];

/** 低模几何形状（在共用构建器 LowPolyBui */
export type RockShapeType =
  /** 环壳 */
  | 'shell'
  /** 柱体 */
  | 'column'
  /** 低面球 */
  | 'blob'
  /** 板片：薄而扁的多边形板（板岩 / 页岩 / 片岩） */
  | 'slab';

export const ROCK_SHAPE_TYPES: readonly RockShapeType[] = ['shell', 'column', 'blob', 'slab'];

/** 原型参数 */
export interface RockParams {
  // ---- 整体尺寸 ----
  /** 主轴直径（宽），单位：格 */
  diameter?: number;
  /** 高度，单位：格 */
  height?: number;
  /** 高宽比（覆盖 height/diameter 的 */
  aspect?: number;
  /** 整体尺寸倍率，默认 1 */
  sizeScale?: number;
  /** 是否允许在运行时按 hash 再做 0.85~1 */
  variant?: boolean;

  // ---- 轮廓 ----
  /** 环壳的径向分段数（决定"几边形"的棱），默认 6 */
  sides?: number;
  /** 壳层数量（上下叠几层壳） */
  shells?: number;
  /** 层间半径收缩比（上一层相对下一层），默认 0.78 */
  shellTaper?: number;
  /** 层间水平错位量（相对直径） */
  shellSkew?: number;
  /** 层间旋转（弧度） */
  shellTwist?: number;
  /** 每层高度占整体高度的比例 */
  shellRatios?: number[];
  /** 贴地壳的扁度（0.3 = 像一块被压扁的饼） */
  baseFlatten?: number;
  /** 顶点径向抖动 0~1 */
  jitter?: number;
  /** 顶面是否封盖，默认 true（不封会看到内部） */
  cap?: boolean;

  // ---- 倾斜与姿态（"旋转"的一半）----
  /** 绕 X 轴倾斜（弧度），正值顶部偏向 +Z */
  tiltX?: number;
  /** 绕 Z 轴倾斜（弧度），正值顶部偏向 +X */
  tiltZ?: number;
  /** 绕 Y 轴旋转（弧度） */
  rotY?: number;

  // ---- 柱状（尖石 / 石柱 / 矮墩）----
  /** 柱的分节数，默认 3 */
  segments?: number;
  /** 柱顶相对柱底的半径比 */
  tipRatio?: number;
  /** 柱底相对主体直径的比例，默认 0.55 */
  baseRatio?: number;
  /** 分节错位量（相对半径），默认 0.12 */
  nodeSkew?: number;
  /** 分节扭转（弧度），默认 0.35 */
  nodeTwist?: number;

  // ---- 板片（板岩 / 页岩）----
  /** 板片数量，默认 3 */
  plates?: number;
  /** 板厚（相对直径），默认 0.1 */
  plateThickness?: number;
  /** 板片错位量（相对直径），默认 0.12 */
  plateSkew?: number;
  /** 板片倾角（弧度） */
  plateTilt?: number;

  // ---- 组合（碎石堆 / 叠置）----
  /** 碎石数量（rubble 原型），默认 5 */
  chunks?: number;
  /** 碎石散布半径（相对直径），默认 0.85 */
  spread?: number;
  /** 叠置档追加的小石尺寸比，默认 0.42 */
  stackRatio?: number;
  /** 叠置档小石的横向偏移（相对直径），默认 0.3 */
  stackOffset?: number;

  // ---- 覆被（moss / snow / li
  /** 朝上面的覆被覆盖率 0~1 */
  crust?: number;
  /** 覆被只出现在"坡缓的上面" */
  crustSlope?: number;

  // ---- 尺寸派生（由形态档写入
  /** 半埋下沉量（相对高度），默认 0 */
  sink?: number;
  /** 横向放大倍率，默认 1 */
  spreadScale?: number;
}

/** 形态档派生 */
export interface RockFormDef {
  /** 中文名（界面 / 文档用） */
  label: string;
  /** 英文名（导出文件名 / id 后缀用） */
  latin: string;
  /** 形态档编号 0~3 */
  code: number;
  /** 参数覆盖（在物种参数之上叠加，先乘算再覆盖） */
  paramPatch?: RockParams;
  /** 数值型尺寸参数统一乘算 */
  paramScale?: number;
  /** 覆被材质槽（crusted 档把朝上面换成这个槽 */
  crustMaterial?: RockMaterial;
  /** 覆被使用的色板键 */
  crustColor?: RockPaletteKey;
  /** 是否叠置第二块小石 */
  stacked?: boolean;
  /** 该档在秋冬是否自动挂雪（crusted 之外的原 */
  snow?: boolean;
}

export type RockFormTable = Record<RockForm, RockFormDef>;

/** 参数表里的一块岩石（一个文件 = 一个"岩石单位 */
export interface RockDef {
  /** 单位 id，形如 `rock.boulder` */
  id: string;
  /** 原型名 */
  archetype: string;
  /** 中文名（界面 / 文档用） */
  name: string;
  /** 拼音名（导出文件名 / manifest 用） */
  latin?: string;
  /** 岩性族：决定默认色板与默认粗糙度 */
  family: RockMaterialFamily;
  /** 同原型下区分形态的编号，默认 0 */
  seed?: number;
  params?: RockParams;
  /** 岩性色板 */
  palette?: RockPalette;
  /** 逐区块岩性覆盖 */
  chunkPalette?: Record<string, RockPalette>;
  /** 标签 */
  tags?: string[];
  /** 参数表文件名（注册表自动回填，便于报错定位） */
  source?: string;
}

/** 岩性色板的五个语义槽 */
export type RockColorSlot = 'body' | 'band' | 'cap' | 'base' | 'crust';

export const ROCK_COLOR_SLOTS: readonly RockColorSlot[] = ['body', 'band', 'cap', 'base', 'crust'];

/** 岩性色板映射 */
export interface RockPalette {
  /** 主体色（侧面大部分面） */
  body?: RockPaletteKey;
  /** 层理 / 棱线 / 凹槽（比主体深） */
  band?: RockPaletteKey;
  /** 受光顶面（比主体浅） */
  cap?: RockPaletteKey;
  /** 贴地阴影与半埋处的土色 */
  base?: RockPaletteKey;
  /** 覆被（苔藓 / 雪 / 地衣） */
  crust?: RockPaletteKey;
}

export interface RockFile {
  /** 参数表格式版本，默认不写 */
  version?: number;
  rocks: RockDef[];
}

/** 色板 */
export interface RockPaletteEntry {
  default: string;
  spring?: string;
  summer?: string;
  autumn?: string;
  winter?: string;
}

export interface RockPaletteFile {
  version?: number;
  /** 岩性色 */
  stone?: Record<string, RockPaletteEntry>;
  /** 覆被色：苔藓 / 雪 / 地衣 / 干草 */
  cover?: Record<string, RockPaletteEntry>;
}

/** 解析后的实际颜色表 */
export type ResolvedRockPalette = Partial<Record<RockColorSlot, string>>;

/** 生成时的选项 */
export interface RockBuildOptions {
  /** 季节：只影响"秋冬挂雪" */
  season?: Season;
  /** 形态档（露头 / 半埋 / 叠置 / 覆被） */
  form?: RockForm;
  /** 个体形态种子 */
  shapeSeed?: number;
  /** 整体缩放（1 = 参数表原始尺寸） */
  scale?: number;
  /** 强制覆被（不传时按形态档与季节自动决定） */
  crust?: boolean;
  /** 覆被色板键覆盖（如把苔藓换成地衣） */
  crustColor?: RockPaletteKey;
}

/** 装配完成、可直接交给原型生成的一块岩石 */
export interface RockBuildDef {
  id: string;
  archetype: string;
  name: string;
  latin?: string;
  family: RockMaterialFamily;
  seed: number;
  /** 本次生成使用的形态档 */
  form: RockForm;
  /** 本次生成的个体种子（世界坐标哈希），默认 0 */
  shapeSeed: number;
  /** 本档实际使用的覆被材质槽（无覆被时为 undef */
  crustMaterial?: RockMaterial;
  /** 本档实际使用的覆被色板键（覆被关闭时为 unde */
  crustColor?: RockPaletteKey;
  /** 覆被的实际颜色 `#rrggbb` */
  crustRgb?: string;
  params: RockParams;
  palette: ResolvedRockPalette;
  /** 确定性形态种子（`原型:单位id:seed` 的 */
  buildSeed: number;
  season: Season;
  /** 本次生成是否挂雪（由季节 + 覆被档决定） */
  snow: boolean;
}
