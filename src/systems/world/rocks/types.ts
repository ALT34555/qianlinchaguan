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
  /** 冰材质冷色 */
  | 'ice';

export const ROCK_MATERIAL_FAMILIES: readonly RockMaterialFamily[] = [
  'igneous', 'sedimentary', 'metamorphic', 'clastic', 'ice',
];

/** 岩石形态档定义 */
export type RockForm =
  /** 露头形态（默认） */
  | 'outcrop'
  /** 半埋沉降形态 */
  | 'buried'
  /** 叠置双石组合形态 */
  | 'stacked'
  /** 表面覆被形态 */
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
  /** 通用石头方块同色 */
  | 'stone'
  /** 基岩色（深，用于裂隙内部与叠置阴影） */
  | 'bedrock'
  /** 风化贴地土色 */
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

/** 低模几何形状原语 */
export type RockShapeType =
  /** 环壳 */
  | 'shell'
  /** 柱体 */
  | 'column'
  /** 低面球 */
  | 'blob'
  /** 多边形薄板片 */
  | 'slab';

export const ROCK_SHAPE_TYPES: readonly RockShapeType[] = ['shell', 'column', 'blob', 'slab'];

/** 原型参数 */
export interface RockParams {
  // ---- 整体尺寸 ----
  /** 主轴直径（宽），单位：格 */
  diameter?: number;
  /** 高度，单位：格 */
  height?: number;
  /** 默认高宽比 */
  aspect?: number;
  /** 整体尺寸倍率，默认 1 */
  sizeScale?: number;
  /** 运行时尺寸哈希扰动 */
  variant?: boolean;

  // ---- 轮廓 ----
  /** 径向分段数，默认6 */
  sides?: number;
  /** 壳层数量（上下叠几层壳） */
  shells?: number;
  /** 层间收缩比，默认0.78 */
  shellTaper?: number;
  /** 层间水平错位量（相对直径） */
  shellSkew?: number;
  /** 层间旋转（弧度） */
  shellTwist?: number;
  /** 每层高度占整体高度的比例 */
  shellRatios?: number[];
  /** 贴地壳扁度参数 */
  baseFlatten?: number;
  /** 顶点径向抖动 0~1 */
  jitter?: number;
  /** 顶面封盖开关，默认true */
  cap?: boolean;

  // ---- 倾斜与姿态 ----
  /** 绕 X 轴倾斜弧度 */
  tiltX?: number;
  /** 绕 Z 轴倾斜弧度 */
  tiltZ?: number;
  /** 绕 Y 轴旋转（弧度） */
  rotY?: number;

  // 柱状原型参数
  /** 柱的分节数，默认 3 */
  segments?: number;
  /** 柱顶相对柱底的半径比 */
  tipRatio?: number;
  /** 柱底直径比例，默认0.55 */
  baseRatio?: number;
  /** 分节错位量，默认0.12 */
  nodeSkew?: number;
  /** 分节扭转（弧度），默认 0.35 */
  nodeTwist?: number;

  // ---- 板片（板岩 / 页岩）----
  /** 板片数量，默认 3 */
  plates?: number;
  /** 板厚（相对直径），默认 0.1 */
  plateThickness?: number;
  /** 板片错位量，默认0.12 */
  plateSkew?: number;
  /** 板片倾角（弧度） */
  plateTilt?: number;

  // ---- 组合（碎石堆 / 叠置）----
  /** 碎石数量，默认5 */
  chunks?: number;
  /** 碎石散布半径，默认0.85 */
  spread?: number;
  /** 叠置小石尺寸比，默认0.42 */
  stackRatio?: number;
  /** 叠置小石横偏，默认0.3 */
  stackOffset?: number;

  // 覆被参数
  /** 朝上面的覆被覆盖率 0~1 */
  crust?: number;
  /** 覆被只出现在"坡缓的上面" */
  crustSlope?: number;

  // 尺寸派生参数
  /** 半埋下沉量（相对高度），默认 0 */
  sink?: number;
  /** 横向放大倍率，默认 1 */
  spreadScale?: number;
}

/** 形态档派生 */
export interface RockFormDef {
  /** 中文名（界面 / 文档用） */
  label: string;
  /** 英文标识名 */
  latin: string;
  /** 形态档编号 0~3 */
  code: number;
  /** 形态档参数覆盖 */
  paramPatch?: RockParams;
  /** 数值型尺寸参数统一乘算 */
  paramScale?: number;
  /** 覆被材质槽 */
  crustMaterial?: RockMaterial;
  /** 覆被使用的色板键 */
  crustColor?: RockPaletteKey;
  /** 是否叠置第二块小石 */
  stacked?: boolean;
  /** 秋冬自动积雪开关 */
  snow?: boolean;
}

export type RockFormTable = Record<RockForm, RockFormDef>;

/** 岩石单位原始定义 */
export interface RockDef {
  /** 单位ID标识 */
  id: string;
  /** 原型名 */
  archetype: string;
  /** 中文名（界面 / 文档用） */
  name: string;
  /** 拼音标识名 */
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
  /** 来源配置文件名 */
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
  /** 岩性色 */
  stone?: Record<string, RockPaletteEntry>;
  /** 覆被色键 */
  cover?: Record<string, RockPaletteEntry>;
}

/** 解析后的实际颜色表 */
export type ResolvedRockPalette = Partial<Record<RockColorSlot, string>>;

/** 生成时的选项 */
export interface RockBuildOptions {
  /** 季节：只影响"秋冬挂雪" */
  season?: Season;
  /** 岩石形态档 */
  form?: RockForm;
  /** 个体形态种子 */
  shapeSeed?: number;
  /** 整体缩放（1 = 参数表原始尺寸） */
  scale?: number;
  /** 强制覆被材质槽 */
  crust?: boolean;
  /** 覆被色板键覆盖（如把苔藓换成地衣） */
  crustColor?: RockPaletteKey;
}

/** 装配完成的岩石构建定义 */
export interface RockBuildDef {
  id: string;
  archetype: string;
  name: string;
  latin?: string;
  family: RockMaterialFamily;
  seed: number;
  /** 本次生成使用的形态档 */
  form: RockForm;
  /** 个体生成种子 */
  shapeSeed: number;
  /** 实际覆被材质槽 */
  crustMaterial?: RockMaterial;
  /** 实际覆被色板键 */
  crustColor?: RockPaletteKey;
  /** 覆被的实际颜色 `#rrggbb` */
  crustRgb?: string;
  params: RockParams;
  palette: ResolvedRockPalette;
  /** 确定性形态种子 */
  buildSeed: number;
  season: Season;
  /** 本次生成是否挂雪 */
  snow: boolean;
}
