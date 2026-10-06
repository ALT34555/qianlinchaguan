/** 植被（植物）系统数据模型 */

/** 生长带（气候 + 地貌），决定默认尺寸与默认色板 */
export type ClimateZone = 'tropical' | 'subtropical' | 'temperate' | 'cold';

export const CLIMATE_ZONES: readonly ClimateZone[] = ['tropical', 'subtropical', 'temperate', 'cold'];

/** 四季 */
export type Season = 'spring' | 'summer' | 'autumn' | 'winter';

export const SEASONS: readonly Season[] = ['spring', 'summer', 'autumn', 'winter'];

/** 植物状态 */
export type PlantState = 'planted' | 'normal' | 'seedling' | 'subadult' | 'flowering' | 'fruiting' | 'withered';

/** 状态编号 */
export const PLANT_STATES: readonly PlantState[] = ['planted', 'normal', 'seedling', 'subadult', 'flowering', 'fruiting', 'withered'];

/** 状态 -> 编号（0~6） */
export const PLANT_STATE_CODES: Readonly<Record<PlantState, number>> = Object.freeze(
  Object.fromEntries(PLANT_STATES.map((s, i) => [s, i])) as Record<PlantState, number>,
);

/** 编号 -> 状态 */
export function plantStateOfCode(code: number): PlantState {
  return PLANT_STATES[code] ?? 'normal';
}

/** 有模型的六态（不含 0 种植） */
export const PLANT_MODELED_STATES: readonly PlantState[] = PLANT_STATES.filter((s) => s !== 'planted');

/** 状态的种子物候档位 */
export type StateForm = 'sprout' | 'withered' | 'orchid';

export const STATE_FORMS: readonly StateForm[] = ['sprout', 'withered', 'orchid'];

/** 色板键：参数表只能引用这些键，避免拼写漂移 */
export type PaletteKey =
  // ---- 树干 / 土壤 / 雪 / 刺 ----
  | 'bark'
  | 'barkLight'
  | 'barkDark'
  | 'barkPale'
  | 'barkWarm'
  | 'soil'
  | 'snow'
  | 'thorn'
  // ---- 干枯专用色阶 ----
  | 'withered'
  | 'witheredDark'
  | 'witheredStem'
  | 'witheredNeedle'
  // ---- 叶色 ----
  | 'foliage'
  | 'foliageDark'
  | 'foliageDeep'
  | 'canopy'
  | 'conifer'
  | 'coniferDark'
  | 'coniferDeep'
  | 'larch'
  | 'jungle'
  | 'jungleDark'
  | 'jungleDeep'
  | 'palmFrond'
  | 'palmFrondDry'
  | 'grassDry'
  // ---- 专类叶色（竹 / 茶 / 桑 / 农作
  | 'bamboo'
  | 'bambooCulm'
  | 'tea'
  | 'mulberry'
  | 'cropGreen'
  | 'riceGreen'
  | 'cropGold'
  | 'cropGoldDark'
  | 'sorghum'
  // ---- 花色 ----
  | 'bloom'
  | 'bloomWhite'
  | 'bloomPink'
  | 'bloomRed'
  | 'bloomGold'
  | 'bloomWarm'
  | 'plumBloom'
  | 'orchidBloom'
  | 'chrysBloom'
  | 'lotusBloom'
  /** 花粉颗粒色（针叶树花期用 */
  | 'pollen'
  // ---- 果实 / 谷粒 ----
  | 'fruit'
  | 'fruitYellow'
  | 'fruitPale'
  | 'grain'
  | 'grainPale'
  | 'beanPod';

/** 几何材质槽 */
export type PlantMaterial = 'solid' | 'snow' | 'bloom' | 'fruit';

export const PLANT_MATERIALS: readonly PlantMaterial[] = ['solid', 'snow', 'bloom', 'fruit'];

/** 花朵默认色 */
export const DEFAULT_BLOOM_COLOR = '#a8456b';

/** 低模形状类型 */
export type ShapeType =
  /** 三角锥（针叶树的层叠伞盖、尖顶） */
  | 'cone'
  /** 棱柱（树干、枝干、石块） */
  | 'prism'
  /** 方块（棕榈树冠基座、竹节、支撑物） */
  | 'box'
  /** 点 + 一圈等分顶点构成的穹顶（灌木） */
  | 'dome'
  /** 完整低面球（浆果、叶团、椰子） */
  | 'ico'
  /** 交叉面片（草丛、蕨叶、幼苗） */
  | 'crossQuad'
  /** 单花瓣薄片（花朵的最小元素 */
  | 'petal';

export interface ShapeSpec {
  type: ShapeType;
  /** 相对植物基点的三轴半径/长度，单位：格（方块边长） */
  size: [number, number, number];
  /** 相对基点的中心位置 */
  pos?: [number, number, number];
  color: PaletteKey;
  material?: PlantMaterial;
  /** 绕 Y 轴旋转（弧度） */
  rotY?: number;
  /** 朝 X / Z 方向的倾斜（弧度） */
  tiltX?: number;
  tiltZ?: number;
  /** 棱柱 / 棱锥 / 球的径向分段数，低模默认 5 */
  sides?: number;
  /** ico 的纬线层数，默认 1（上下各一层） */
  rings?: number;
  /** 半径抖动 0~1 */
  jitter?: number;
  /** 附着的径向方向（弧度 */
  bearing?: number;
  /** petal 花瓣的上扬角（弧度） */
  pitch?: number;
  /** petal 花瓣中部上凸量（相对宽度） */
  cup?: number;
  /** 是否参与随机形变（叶片/草丛打散用） */
  variant?: boolean;
}

/** 原型参数 */
export interface PlantParams {
  // ---- 通用 ----
  /** 整体高度，单位：格 */
  height?: number;
  /** 树干 / 主茎高度，单位：格 */
  trunk?: number;
  /** 树干贴地半径，单位：格 */
  trunkRadius?: number;
  /** 树干顶端半径，单位：格（默认取贴地半径的一半） */
  trunkTopRadius?: number;
  /** 树干弯曲 / 倾斜量，0 表示笔直 */
  lean?: number;
  /** 枝干条数 */
  limbCount?: number;
  /** 枝干长度，单位：格 */
  limbLength?: number;

  // ---- 冠层 / 叶团 ----
  /** 冠层半径，单位：格 */
  canopy?: number;
  /** 冠层高度比例（相对 height），默认 0.45 */
  canopyRatio?: number;
  /** 叶团 / 伞盖层数 */
  layers?: number;
  /** 第一层伞盖的半径 */
  layerRadius?: number;
  /** 层与层之间的垂直重叠系数 */
  layerStep?: number;
  /** 叶团数量 */
  blobs?: number;

  // ---- 针叶 / 棕榈 / 竹 / 特殊形态
  /** 棕榈叶片数 */
  fronds?: number;
  /** 棕榈叶片长度，单位：格 */
  frondLength?: number;
  /** 棕榈叶下垂量，0~1 */
  droop?: number;
  /** 叶片分节数（棕榈 / 竹叶） */
  segments?: number;
  /** 尖顶数量（针叶树顶部的针尖） */
  spikeCount?: number;
  /** 枝条上扬角（弧度），针叶树 / 金合欢用 */
  branchAngle?: number;
  /** 竹类秆数（bamboo 原型） */
  culms?: number;
  /** 竹节间距，单位：格（bamboo 原型） */
  nodeStep?: number;
  /** 状态形态 */
  stateForm?: StateForm;

  // ---- 花 / 果 / 雪 ----
  /** 花簇数量（每簇 = 一朵花瓣花 */
  bloomCount?: number;
  /** 花簇半径，单位：格 */
  bloomRadius?: number;
  /** 每朵花的**花瓣数**（4 = 四瓣花 */
  petals?: number;
  /** 额外铺在树冠 / 幼苗上的**绿色花瓣叶片**数 */
  leafPetals?: number;
  /** 果实数量 */
  fruitCount?: number;
  /** 果实半径，单位：格 */
  fruitRadius?: number;
  /** 果实大小倍率（结实时放大果实），默认 1 */
  fruitScale?: number;
  /** 本物种**天生没有开花形态**（雪线地衣、蕨这类 */
  flowerless?: boolean;
  /** 本物种天生没有结实形态（同上 */
  fruitless?: boolean;
  /** 积雪厚度（0 = 不积雪），单位：格 */
  snow?: number;
  /** 积雪盖在哪些部位 */
  snowOn?: SnowTarget[];

  // ---- 灌木 / 草丛 ----
  /** 草丛交叉面片数 / 窄叶片数（grassClum */
  blades?: number;
  /** 茎干数量（多茎灌木） */
  stems?: number;
  /** 丛生半径，单位：格 */
  spread?: number;
  /** 灌木整体宽高比，默认 1.1 */
  aspect?: number;

  // ---- 低模细分 / 抖动 ----
  /** 径向分段数（棱柱 / 锥 / 球的边数），默认 5 */
  sides?: number;
  /** 半径抖动 0~1，默认 0.15 */
  jitter?: number;
  /** 叶团半径抖动，默认 0.2 */
  blobJitter?: number;
  /** 整体尺寸倍率（作用于主干与冠层），默认 1 */
  sizeScale?: number;
  /** 是否有刺（荆棘、多肉），0 表示无 */
  thorns?: number;
}

export type SnowTarget = 'canopy' | 'top' | 'ground';

/** 状态派生 */
export interface PlantStateDef {
  /** 中文名（界面 / 文档用），如 '开花' */
  label: string;
  /** 英文名（导出文件名 / id 后缀用） */
  latin: string;
  /** 状态编号 0~6 */
  code?: number;
  /** 只做接口、不做模型的状态（目前只有 0 种植） */
  modeless?: boolean;
  /** modeless 状态的呈现回退目标 */
  fallback?: PlantState;
  /** 强制切换的原型（干枯 -> withered、幼 */
  archetype?: string;
  /** 写入 params.stateForm 的形态档位 */
  stateForm?: StateForm;
  /** 花开关 */
  bloomMode?: 'none' | 'force' | 'species';
  /** 果开关 */
  fruitMode?: 'none' | 'force' | 'species';
  /** 参数覆盖（在物种参数之上叠加） */
  paramPatch?: PlantParams;
  /** 数值型尺寸参数统一乘算（尺寸、叶团、枝干等） */
  paramScale?: number;
  /** 色板补充（并入该状态的色表键集合） */
  palettePatch?: Record<string, PaletteKey>;
}

export type PlantStateTable = Record<PlantState, PlantStateDef>;

/** 参数表里的一株植物 */
export interface PlantVariantDef {
  id: string;
  /** 原型名 */
  archetype: string;
  climate: ClimateZone;
  /** 中文名（界面 / 文档用） */
  name: string;
  /** 拉丁名（导出文件名 / 多语言用） */
  latin?: string;
  /** 同原型下区分形态的编号 */
  seed?: number;
  params?: PlantParams;
  /** 逐物种色板覆盖 */
  palette?: Record<string, PaletteKey>;
  /** 覆盖生长带默认尺寸（乘算） */
  scale?: number;
  tags?: string[];
  /** 本条记录代表哪一段生命周期 */
  state?: PlantState;
  /** 该物种支持的状态清单 */
  states?: PlantState[];
  /** 参数表文件名（注册表自动回填，便于报错定位） */
  source?: string;
}

export interface PlantFile {
  plants: PlantVariantDef[];
}

/** 色板：一组具名色阶，每个色阶给出四季颜色 */
export interface PaletteEntry {
  default: string;
  spring?: string;
  summer?: string;
  autumn?: string;
  winter?: string;
}

export interface PaletteFile {
  /** 树干、土壤、雪、刺、干枯等非叶色板 */
  bark?: Record<string, PaletteEntry>;
  /** 叶、花、果色板 */
  greenery?: Record<string, PaletteEntry>;
}

/** 按 key 解析后的实际颜色表 */
export type ResolvedPalette = Partial<Record<PaletteKey, string>>;

/** 生成所需的全部输入（原型函数只认这个） */
export interface PlantBuildOptions {
  seed: number;
  season: Season;
  /** 整体缩放（1 = 参数表原始尺寸 */
  scale?: number;
  /** 开花版本 */
  bloom?: boolean;
  /** 花朵颜色（#rrggbb） */
  bloomColor?: string;
  /** 生命周期状态；'normal'（默认）表示成熟健株 */
  state?: PlantState;
}

/** 生成接口 */
export interface PlantBloomOptions {
  /** 生命周期状态；缺省视为 'normal' */
  state?: PlantState;
  /** 是否生成开花版本（等价于 state: 'flo */
  bloom?: boolean;
  /** 花朵颜色（#rrggbb），不传时按上述优先级回退 */
  bloomColor?: string;
}

export interface PlantDef {
  id: string;
  archetype: string;
  climate: ClimateZone;
  name: string;
  latin?: string;
  seed: number;
  tags: readonly string[];
  params: PlantParams;
  palette: ResolvedPalette;
  /** 该变体的确定性种子（字符串哈希 + seed 混 */
  buildSeed: number;
  /** 本次生成的生命周期状态 */
  state: PlantState;
  /** 当前季节是否允许积雪 */
  snow: boolean;
  /** 本次生成是否为"开花版本"（针叶树恒为 fals */
  bloom: boolean;
  /** 花朵颜色覆盖（#rrggbb） */
  bloomColor?: string;
  /** 本次生成是否结果（由状态或物种参数决定） */
  fruit: boolean;
}
