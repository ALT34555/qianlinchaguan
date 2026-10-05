/**
 * 多语言基础设施的公共类型。
 *
 * 设计目标：把"界面文案"从代码里彻底剥离出来，新增一种语言只需要
 * 写一个语言包 + 调用一次 registerLocale()，不必改动任何界面代码。
 */

/** 语言标识（精简 BCP 47，如 'zh-CN'、'en'、'ja'）。刻意不做联合类型，方便后续无限扩展。 */
export type LocaleCode = string;

/** 语言元信息，供语言切换器与 <html lang> 同步使用。 */
export interface LocaleMeta {
  /** 语言标识，如 'zh-CN'。 */
  code: LocaleCode;
  /** 语言自称（母语写法），如 "简体中文"、"English"。 */
  label: string;
  /** 写入 <html lang> 的值，决定 CSS :lang() 与断行规则。 */
  htmlLang: string;
  /** 该语言的英文名，供英文界面下的语言列表使用。 */
  englishLabel?: string;
}

/**
 * 扁平键值表：键用点号分组（如 'menu.createWorld'）。
 * 扁平结构便于比对缺失键，也便于后续接入翻译平台（PO/JSON/CSV 互转）。
 */
export type Messages = Record<string, string>;

/** 插值参数，`{name}` 形式占位符会被替换。 */
export type MessageParams = Record<string, string | number>;

/**
 * 惰性语言包加载器。
 * 大型语言包可以用 `() => import('./locales/xx').then(m => m.messages)` 注册，
 * 首次切换时才会真正下载。
 */
export type LocaleLoader = () => Promise<Messages>;

/** 语言变更回调。 */
export type LocaleListener = (code: LocaleCode, meta: LocaleMeta) => void;

/** 最小存储接口；与 core/GameSettings.ts 的 StorageLike 结构兼容。 */
export interface StorageLike {
  getItem(key: string): string | null;
  setItem(key: string, value: string): void;
}
