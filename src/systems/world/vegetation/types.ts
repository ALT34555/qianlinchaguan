/**
 * 植被（植物）系统数据模型。
 *
 * 分层与工程既有风格一致，坚持"数据驱动 + 代码解耦"：
 *   1. 参数表  content/data/world/plants/*.json —— 策划/美术工作区，不含代码；
 *   2. 原型    archetypes.ts —— 每个原型是一个纯函数，参数 -> 低模几何；
 *   3. 注册表  Plants.ts —— 校验参数表 id 与代码常量一致，并对外提供查询；
 *   4. 几何    geometry.ts —— 低模面构建与合并（只产出 position/normal/color，无贴图）。
 *
 * 颜色一律走"色板键 + 四季色阶"（palettes.json），原型只声明语义
 * （bark / foliage / bloom / fruit / snow），这样同一套原型能跨气候带与季节复用。
 */

/** 生长带（气候 + 地貌），决定默认尺寸与默认色板 */
export type ClimateZone = 'tropical' | 'subtropical' | 'temperate' | 'cold';

export const CLIMATE_ZONES: readonly ClimateZone[] = ['tropical', 'subtropical', 'temperate', 'cold'];

/** 四季：春 夏 秋 冬（与日历系统 season 0..3 一致） */
export type Season = 'spring' | 'summer' | 'autumn' | 'winter';

export const SEASONS: readonly Season[] = ['spring', 'summer', 'autumn', 'winter'];

/** 色板键：参数表只能引用这些键，避免拼写漂移 */
export type PaletteKey =
  | 'bark'
  | 'barkLight'
  | 'foliage'
  | 'foliageDark'
  | 'canopy'
  | 'palmFrond'
  | 'bloom'
  | 'fruit'
  | 'snow'
  | 'soil'
  | 'thorn';

/** 几何材质槽：导出 glTF 时为同一槽聚成一个 primitive */
export type PlantMaterial = 'solid' | 'snow' | 'bloom' | 'fruit';

export const PLANT_MATERIALS: readonly PlantMaterial[] = ['solid', 'snow', 'bloom', 'fruit'];

/** 花朵默认色：未指定开花色、且该物种色板没有 bloom 时使用 */
export const DEFAULT_BLOOM_COLOR = '#a8456b';

/** 低模形状类型 */
export type ShapeType =
  /** 三角锥（针叶树的层叠伞盖、尖顶） */
  | 'cone'
  /** 棱柱（树干、枝干、石块） */
  | 'prism'
  /** 方块（棕榈树冠基座、支撑物） */
  | 'box'
  /** 点 + 一圈等分顶点构成的穹顶（灌木），floor 可封底 */
  | 'dome'
  /** 完整低面球（浆果、叶团、椰子），rings 为纬线层数 */
  | 'ico'
  /** 交叉面片（草丛、蕨叶、幼苗），法线统一朝上以避免交叉面全黑 */
  | 'crossQuad';

export interface ShapeSpec {
  type: ShapeType;
  /** 相对植物基点的三轴半径/长度，单位：格（方块边长） */
  size: [number, number, number];
  /** 相对基点的中心位置，单位：格；省略时按形状类型取默认（柱体取 size.y/2） */
  pos?: [number, number, number];
  color: PaletteKey;
  material?: PlantMaterial;
  /** 绕 Y 轴旋转（弧度） */
  rotY?: number;
  /** 朝 X / Z 方向的倾斜（弧度）：正值使顶端偏向 +X / +Z */
  tiltX?: number;
  tiltZ?: number;
  /** 棱柱 / 棱锥 / 球的径向分段数，低模默认 5 */
  sides?: number;
  /** ico 的纬线层数，默认 1（上下各一层） */
  rings?: number;
  /** 半径抖动 0~1，制造不规则的低模轮廓，默认 0.12 */
  jitter?: number;
  /** 附着的径向方向（弧度，0 指向 +X，逆时针为 +Z），用于叶团、花序定位 */
  bearing?: number;
  /** 是否参与随机形变（叶片/草丛打散用），默认 true */
  variant?: boolean;
}

/**
 * 原型参数：JSON 里的 params 全部按此名字取值，全部可省略（缺省即默认值）。
 * 新增原型时，只需在 archetypes.ts 里读需要的字段。
 */
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
  /** 第一层伞盖的半径，单位：格（默认等于 canopy） */
  layerRadius?: number;
  /** 层与层之间的垂直重叠系数，>1 表示更密实，默认 0.82 */
  layerStep?: number;
  /** 叶团数量 */
  blobs?: number;

  // ---- 针叶 / 棕榈 / 特殊形态 ----
  /** 棕榈叶片数 */
  fronds?: number;
  /** 棕榈叶片长度，单位：格 */
  frondLength?: number;
  /** 棕榈叶下垂量，0~1 */
  droop?: number;
  /** 叶片分节数（棕榈） */
  segments?: number;
  /** 尖顶数量（针叶树顶部的针尖） */
  spikeCount?: number;
  /** 枝条上扬角（弧度），针叶树 / 金合欢用 */
  branchAngle?: number;

  // ---- 花 / 果 / 雪 ----
  /** 花簇数量 */
  bloomCount?: number;
  /** 花簇半径，单位：格 */
  bloomRadius?: number;
  /** 果实数量 */
  fruitCount?: number;
  /** 果实半径，单位：格 */
  fruitRadius?: number;
  /** 积雪厚度（0 = 不积雪），单位：格 */
  snow?: number;
  /** 积雪盖在哪些部位：canopy / top / ground，默认 ['canopy'] */
  snowOn?: SnowTarget[];

  // ---- 灌木 / 草丛 ----
  /** 草丛交叉面片数（crossQuad 专用） */
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

/** 参数表里的一株植物 */
export interface PlantVariantDef {
  id: string;
  /** 原型名，必须是 archetypes.ts 注册表中的键 */
  archetype: string;
  climate: ClimateZone;
  /** 中文名（界面 / 文档用） */
  name: string;
  /** 英文名（导出文件名 / 多语言用） */
  latin?: string;
  /** 同原型下区分形态的编号，默认 0；需保证同一原型 + 编号的随机形态稳定 */
  seed?: number;
  params?: PlantParams;
  /** 逐物种色板覆盖；键支持 'foliage.spring' 形式定点覆盖季节色 */
  palette?: Record<string, PaletteKey>;
  /** 覆盖生长带默认尺寸（乘算） */
  scale?: number;
  tags?: string[];
  /** 参数表文件名（注册表自动回填） */
  source?: string;
}

export interface PlantFile {
  /** 参数表格式版本，目前为 1 */
  version?: number;
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
  version?: number;
  /** 树干、土壤、雪、刺等非叶色板 */
  bark?: Record<string, PaletteEntry>;
  /** 叶、花、果色板 */
  greenery?: Record<string, PaletteEntry>;
}

/** 按 key 解析后的实际颜色表：paletteKey -> '#rrggbb' */
export type ResolvedPalette = Partial<Record<PaletteKey, string>>;

/** 生成所需的全部输入（原型函数只认这个） */
export interface PlantBuildOptions {
  seed: number;
  season: Season;
  /** 整体缩放（1 = 参数表原始尺寸，1 格 ≈ 1 方块） */
  scale?: number;
  /**
   * 开花版本：开启后**非针叶**植物会绽放花簇。
   * 针叶树（裸子植物）不参与开花，开关对它们无效。
   */
  bloom?: boolean;
  /** 花朵颜色（#rrggbb）；不传时依次回退到色板 bloom、#a8456b */
  bloomColor?: string;
}

/**
 * 开花版本的开关与花色接口。
 *
 * 生成时按 `buildPlantById(id, season, { bloom: true, bloomColor })` 使用；
 * 花色优先级：`bloomColor` > 物种色板 `palette.bloom` > 默认 #a8456b。
 * 针叶树（裸子植物）不参与开花，开关对它们无效。
 */
export interface PlantBloomOptions {
  /** 是否生成开花版本 */
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
  /** 该变体的确定性种子（字符串哈希 + seed 混合），供原型内部抖动使用 */
  buildSeed: number;
  /**
   * 当前季节是否允许积雪：参数表写了 snow 且季节为秋冬时才铺雪。
   * 这样同一份参数表在春/夏不会出现"夏天还顶着雪帽子"的冷带树。
   */
  snow: boolean;
  /** 本次生成是否为"开花版本"（针叶树恒为 false） */
  bloom: boolean;
  /** 花朵颜色覆盖（#rrggbb）；未设置时回退到色板 bloom / 默认花色 */
  bloomColor?: string;
}
