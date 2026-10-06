/** 多语言基础设施的公共类型 */

/** 语言标识（精简 BCP 47 */
export type LocaleCode = string;

/** 语言元信息 */
export interface LocaleMeta {
  /** 语言标识，如 'zh-CN'。 */
  code: LocaleCode;
  /** 语言自称（母语写法） */
  label: string;
  /** 写入 <html lang> 的值 */
  htmlLang: string;
  /** 该语言的英文名，供英文界面下的语言列表使用。 */
  englishLabel?: string;
}

/** 扁平键值表 */
export type Messages = Record<string, string>;

/** 插值参数，`{name}` 形式占位符会被替换。 */
export type MessageParams = Record<string, string | number>;

/** 惰性语言包加载器 */
export type LocaleLoader = () => Promise<Messages>;

/** 语言变更回调。 */
export type LocaleListener = (code: LocaleCode, meta: LocaleMeta) => void;

/** 最小存储接口 */
export interface StorageLike {
  getItem(key: string): string | null;
  setItem(key: string, value: string): void;
}
