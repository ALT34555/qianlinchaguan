/**
 * 多语言（i18n）核心。
 *
 * 三层结构，界面代码只依赖第一层：
 *   1. `t()` / `tp()`      —— 取文案（带插值、复数、缺键回退）
 *   2. `setLocale()` / `onLocaleChange()` —— 切换语言并通知界面重绘
 *   3. `registerLocale()` / `registerLocaleLoader()` —— 注册新语言的扩展点
 *
 * 新增一种语言的完整流程：
 *   a. 复制 `locales/zh-CN.ts` 为 `locales/xx.ts`，翻译其中的值；
 *   b. 在文件底部调用一次 `registerLocale({ code: 'xx', ... }, messages)`
 *      （或对大型语言包用 `registerLocaleLoader` 做按需加载）；
 *   c. 不需要改动任何界面代码 —— 语言切换器会自动列出它。
 *
 * 缺键时依次回退：当前语言 → 基准语言（zh-CN）→ 键名本身，
 * 因此"只翻译了一半"的语言包也能安全上线。
 */
import { zhCN } from './locales/zh-CN';
import { en } from './locales/en';
import type { LocaleCode, LocaleListener, LocaleLoader, LocaleMeta, MessageParams, Messages, StorageLike } from './types';

export type { LocaleCode, LocaleListener, LocaleLoader, LocaleMeta, MessageParams, Messages, StorageLike };

/** 基准语言：所有缺键最终回退到这里。 */
export const BASE_LOCALE: LocaleCode = 'zh-CN';

/** 语言偏好的存储键。 */
export const LOCALE_STORAGE_KEY = 'qianlin.locale.v1';

/** URL 查询参数名，便于用 `?lang=en` 直接分享某个语言的链接。 */
export const LOCALE_QUERY_KEY = 'lang';

interface LocaleEntry {
  meta: LocaleMeta;
  messages: Messages | null;
  loader?: LocaleLoader;
}

const entries = new Map<LocaleCode, LocaleEntry>();
const listeners = new Set<LocaleListener>();
const pluralRulesCache = new Map<string, Intl.PluralRules>();

let current: LocaleCode = BASE_LOCALE;
let storage: StorageLike | null = null;
let documentRef: Document | null = typeof document === 'undefined' ? null : document;

/** 按注册顺序排列的语言代码，供语言切换器使用。 */
function codes(): LocaleCode[] {
  return [...entries.keys()];
}

function normalize(code: string): string {
  return code.trim().toLowerCase().replace(/_/g, '-');
}

function lookup(code: LocaleCode, key: string): string | undefined {
  return entries.get(code)?.messages?.[key];
}

/** `{name}` 形式的占位符替换；缺失参数保持原样，便于发现漏传。 */
function interpolate(template: string, params?: MessageParams): string {
  if (!params) return template;
  return template.replace(/\{(\w+)\}/g, (whole, name: string) =>
    Object.prototype.hasOwnProperty.call(params, name) ? String(params[name]) : whole);
}

function pluralRulesFor(tag: string): Intl.PluralRules {
  let rules = pluralRulesCache.get(tag);
  if (!rules) {
    try { rules = new Intl.PluralRules(tag); }
    catch { rules = new Intl.PluralRules('en'); }
    pluralRulesCache.set(tag, rules);
  }
  return rules;
}

// ---------------------------------------------------------------------------
// 语言注册（扩展点）
// ---------------------------------------------------------------------------

/** 注册或覆盖一种语言。重复注册同一语言会替换其元信息与文案。 */
export function registerLocale(meta: LocaleMeta, messages: Messages): void {
  entries.set(meta.code, { meta, messages });
}

/**
 * 注册"按需加载"的语言：元信息立即可见（会出现在语言切换器里），
 * 文案在首次切换过去时才真正下载。
 * 例：`registerLocaleLoader({ code: 'ja', label: '日本語' }, () => import('./locales/ja').then(m => m.ja))`
 */
export function registerLocaleLoader(meta: LocaleMeta, loader: LocaleLoader): void {
  entries.set(meta.code, { meta, messages: null, loader });
}

/** 确保某语言的文案已加载（对已注册的语言是空操作）。 */
export async function ensureLocale(code: LocaleCode): Promise<LocaleCode> {
  const resolved = resolveLocale(code);
  const entry = entries.get(resolved)!;
  if (!entry.messages && entry.loader) entry.messages = await entry.loader();
  return resolved;
}

/** 已注册的语言列表（含尚未加载文案的惰性语言）。 */
export function availableLocales(): LocaleMeta[] {
  return codes().map(code => entries.get(code)!.meta);
}

/** 当前语言的元信息。 */
export function localeMeta(code: LocaleCode = current): LocaleMeta {
  return entries.get(code)?.meta ?? { code, label: code, htmlLang: code };
}

// ---------------------------------------------------------------------------
// 语言解析与切换
// ---------------------------------------------------------------------------

/**
 * 把任意用户输入（URL 参数、navigator.language、设置项）解析为已注册的语言。
 * 依次尝试：完全匹配 → 主语言完全匹配（'en'）→ 主语言前缀匹配（'en-GB'）→ 基准语言。
 */
export function resolveLocale(input: string | null | undefined): LocaleCode {
  const raw = normalize(String(input ?? ''));
  if (!raw) return BASE_LOCALE;
  const known = codes();
  for (const code of known) if (normalize(code) === raw) return code;
  const base = raw.split('-')[0];
  for (const code of known) if (normalize(code) === base) return code;
  for (const code of known) if (normalize(code).split('-')[0] === base) return code;
  return BASE_LOCALE;
}

/** 浏览器/系统偏好语言，逐项尝试直到命中已注册语言。 */
export function browserLocale(): LocaleCode {
  const candidates = typeof navigator === 'undefined'
    ? []
    : (navigator.languages?.length ? navigator.languages : [navigator.language]);
  for (const candidate of candidates) {
    if (!candidate) continue;
    const resolved = resolveLocale(candidate);
    // resolveLocale 在完全不认识时返回基准语言，这里需要区分"真命中"。
    const base = normalize(candidate).split('-')[0];
    if (codes().some(code => normalize(code).split('-')[0] === base)) return resolved;
  }
  return BASE_LOCALE;
}

/** 当前语言代码。 */
export function getLocale(): LocaleCode {
  return current;
}

/** 语言代码写入 <html lang> 的值，供 Intl 与 CSS :lang() 使用。 */
export function localeTag(code: LocaleCode = current): string {
  return localeMeta(code).htmlLang || code;
}

/** 切换语言；返回最终生效的语言代码。相同语言不重复触发通知。 */
export function setLocale(code: LocaleCode): LocaleCode {
  const next = resolveLocale(code);
  if (next === current) { applyDocumentLocale(); return current; }
  current = next;
  persistLocale();
  applyDocumentLocale();
  notify();
  return current;
}

/** 切换语言，必要时先加载惰性语言包。 */
export async function setLocaleAsync(code: LocaleCode): Promise<LocaleCode> {
  await ensureLocale(code);
  return setLocale(code);
}

/** 订阅语言变更，返回取消订阅函数。 */
export function onLocaleChange(listener: LocaleListener): () => void {
  listeners.add(listener);
  return () => { listeners.delete(listener); };
}

function notify(): void {
  const meta = localeMeta();
  listeners.forEach(listener => {
    try { listener(current, meta); }
    catch (error) { console.error('[i18n] locale listener failed', error); }
  });
  if (typeof window !== 'undefined' && typeof CustomEvent === 'function') {
    window.dispatchEvent(new CustomEvent('qianlin:localechange', { detail: { locale: current, meta } }));
  }
}

function persistLocale(): void {
  try { storage?.setItem(LOCALE_STORAGE_KEY, current); } catch { /* 存储不可用时静默降级 */ }
}

function applyDocumentLocale(): void {
  const root = documentRef?.documentElement;
  if (!root) return;
  const meta = localeMeta();
  root.lang = meta.htmlLang || current;
  root.setAttribute('data-locale', current);
}

// ---------------------------------------------------------------------------
// 初始化
// ---------------------------------------------------------------------------

export interface InitOptions {
  /** 文案存储；省略时尝试 localStorage。 */
  storage?: StorageLike | null;
  /** 优先级最高的语言偏好，通常来自 URL 参数。 */
  preferred?: string | null;
  /** 关闭时忽略已保存的偏好与浏览器语言，直接使用基准语言。 */
  autoDetect?: boolean;
}

/**
 * 初始化语言：URL 参数 → 已保存偏好 → 浏览器语言 → 基准语言。
 * 应在创建任何界面之前调用一次；重复调用是安全的。
 */
export function initI18n(options: InitOptions = {}): LocaleCode {
  if (options.storage !== undefined) storage = options.storage;
  if (!storage) {
    try { storage = typeof localStorage === 'undefined' ? null : localStorage; } catch { storage = null; }
  }
  if (!documentRef && typeof document !== 'undefined') documentRef = document;

  const auto = options.autoDetect !== false;
  let preferred = options.preferred ?? null;
  if (!preferred && typeof location !== 'undefined') {
    try { preferred = new URLSearchParams(location.search).get(LOCALE_QUERY_KEY); } catch { preferred = null; }
  }
  let target: LocaleCode | null = null;
  if (preferred) target = resolveLocale(preferred);
  if (!target && auto) {
    try { target = resolveLocale(storage?.getItem(LOCALE_STORAGE_KEY)); } catch { target = null; }
  }
  if (!target && auto) target = browserLocale();
  current = target ?? current;
  persistLocale();
  applyDocumentLocale();
  return current;
}

// ---------------------------------------------------------------------------
// 取文案
// ---------------------------------------------------------------------------

/**
 * 取一条文案。
 * @param key 语言包中的键，如 'menu.createWorld'
 * @param params 插值参数，替换 `{name}` 形式的占位符
 */
export function t(key: string, params?: MessageParams): string {
  const template = lookup(current, key) ?? lookup(BASE_LOCALE, key) ?? key;
  return interpolate(template, params);
}

/**
 * 取一条带复数的文案。
 * 依次尝试 `{key}.{category}`（由 Intl.PluralRules 判定）与 `{key}.other`。
 */
export function tp(key: string, count: number, params?: MessageParams): string {
  const category = pluralRulesFor(localeTag()).select(count);
  const withCount: MessageParams = { count, ...params };
  const template = lookup(current, `${key}.${category}`) ?? lookup(current, `${key}.other`)
    ?? lookup(BASE_LOCALE, `${key}.${category}`) ?? lookup(BASE_LOCALE, `${key}.other`) ?? key;
  return interpolate(template, withCount);
}

/** 该键在当前语言或基准语言中是否存在。 */
export function hasMessage(key: string): boolean {
  return lookup(current, key) !== undefined || lookup(BASE_LOCALE, key) !== undefined;
}

/** 当前语言下缺失的键（以基准语言为参照）。 */
export function missingKeys(): string[] {
  const base = entries.get(BASE_LOCALE)?.messages ?? {};
  return Object.keys(base).filter(key => lookup(current, key) === undefined);
}

/**
 * 就地刷新 DOM 里标记过的文案，适合"不想整块重建"的界面（重建会丢状态）。
 *
 *   <button data-i18n="game.map.zoomIn"></button>
 *   <button data-i18n="atlas.zoomInTitle" data-i18n-attr="title"></button>
 *   <span data-i18n="game.date" data-i18n-params='{"year":2026}'></span>
 *
 * 只改文案、不动事件监听，因此可以在语言切换时安全调用。
 */
export function applyDomI18n(root: ParentNode = document): void {
  root.querySelectorAll<HTMLElement>('[data-i18n]').forEach(element => {
    const key = element.dataset.i18n;
    if (!key) return;
    let params: MessageParams | undefined;
    const raw = element.dataset.i18nParams;
    if (raw) { try { params = JSON.parse(raw) as MessageParams; } catch { params = undefined; } }
    const text = t(key, params);
    const attr = element.dataset.i18nAttr;
    if (attr) element.setAttribute(attr, text);
    else element.textContent = text;
  });
}

// ---------------------------------------------------------------------------
// 本地化格式化（数字 / 时间），随语言自动切换
// ---------------------------------------------------------------------------

/** 按当前语言格式化数字。 */
export function formatNumber(value: number, digits = 0): string {
  try { return new Intl.NumberFormat(localeTag(), { minimumFractionDigits: digits, maximumFractionDigits: digits }).format(value); }
  catch { return value.toFixed(digits); }
}

/** 按当前语言格式化本地时间。 */
export function formatDateTime(ms: number): string {
  try { return new Intl.DateTimeFormat(localeTag(), { dateStyle: 'medium', timeStyle: 'short' }).format(new Date(ms)); }
  catch { return new Date(ms).toLocaleString(); }
}

// ---------------------------------------------------------------------------
// 内置语言包注册
// ---------------------------------------------------------------------------

registerLocale({ code: 'zh-CN', label: '简体中文', englishLabel: 'Simplified Chinese', htmlLang: 'zh-CN' }, zhCN);
registerLocale({ code: 'en', label: 'English', englishLabel: 'English', htmlLang: 'en' }, en);
