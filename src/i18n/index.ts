/** 多语言（i18n）核心 */
import { zhCN } from './locales/zh-CN';
import { en } from './locales/en';
import type { LocaleCode, LocaleListener, LocaleLoader, LocaleMeta, MessageParams, Messages, StorageLike } from './types';

export type { LocaleCode, LocaleListener, LocaleLoader, LocaleMeta, MessageParams, Messages, StorageLike };

/** 基准语言：所有缺键最终回退到这里。 */
export const BASE_LOCALE: LocaleCode = 'zh-CN';

/** 语言偏好的存储键。 */
export const LOCALE_STORAGE_KEY = 'qianlin.locale.v1';

/** URL 查询参数名 */
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

/** 语言切换器候选语言代码。 */
function codes(): LocaleCode[] {
  return [...entries.keys()];
}

function normalize(code: string): string {
  return code.trim().toLowerCase().replace(/_/g, '-');
}

function lookup(code: LocaleCode, key: string): string | undefined {
  return entries.get(code)?.messages?.[key];
}

/** `{name}` 形式的占位符替换 */
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

// 语言注册（扩展点）

/** 注册或覆盖一种语言 */
export function registerLocale(meta: LocaleMeta, messages: Messages): void {
  entries.set(meta.code, { meta, messages });
}

/** 注册"按需加载"的语言 */
export function registerLocaleLoader(meta: LocaleMeta, loader: LocaleLoader): void {
  entries.set(meta.code, { meta, messages: null, loader });
}

/** 确保某语言文案已加载。 */
export async function ensureLocale(code: LocaleCode): Promise<LocaleCode> {
  const resolved = resolveLocale(code);
  const entry = entries.get(resolved)!;
  if (!entry.messages && entry.loader) entry.messages = await entry.loader();
  return resolved;
}

/** 已注册的语言元数据列表。 */
export function availableLocales(): LocaleMeta[] {
  return codes().map(code => entries.get(code)!.meta);
}

/** 当前语言的元信息。 */
export function localeMeta(code: LocaleCode = current): LocaleMeta {
  return entries.get(code)?.meta ?? { code, label: code, htmlLang: code };
}

// 语言解析与切换

/** 解析语言代码，回退至基准语言。 */
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

/** 探测系统偏好语言。 */
export function browserLocale(): LocaleCode {
  const candidates = typeof navigator === 'undefined'
    ? []
    : (navigator.languages?.length ? navigator.languages : [navigator.language]);
  for (const candidate of candidates) {
    if (!candidate) continue;
    const resolved = resolveLocale(candidate);
    // 无法识别时回退基准语言
    const base = normalize(candidate).split('-')[0];
    if (codes().some(code => normalize(code).split('-')[0] === base)) return resolved;
  }
  return BASE_LOCALE;
}

/** 当前语言代码。 */
export function getLocale(): LocaleCode {
  return current;
}

/** 写入 html lang 属性值。 */
export function localeTag(code: LocaleCode = current): string {
  return localeMeta(code).htmlLang || code;
}

/** 切换语言 */
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

// 初始化

export interface InitOptions {
  /** 文案存储；默认 localStorage。 */
  storage?: StorageLike | null;
  /** 高优先级语言偏好，如 URL 参数。 */
  preferred?: string | null;
  /** 关闭时忽略已保存的偏好与浏览器语言 */
  autoDetect?: boolean;
}

/** 初始化语言 */
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

// 取文案

/** 取一条文案 */
export function t(key: string, params?: MessageParams): string {
  const template = lookup(current, key) ?? lookup(BASE_LOCALE, key) ?? key;
  return interpolate(template, params);
}

/** 取一条带复数的文案 */
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

/** 当前语言缺失的文案键名。 */
export function missingKeys(): string[] {
  const base = entries.get(BASE_LOCALE)?.messages ?? {};
  return Object.keys(base).filter(key => lookup(current, key) === undefined);
}

/** 就地刷新 DOM 里标记过的文案 */
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

// 本地化格式化工具

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

// 内置语言包注册

registerLocale({ code: 'zh-CN', label: '简体中文', englishLabel: 'Simplified Chinese', htmlLang: 'zh-CN' }, zhCN);
registerLocale({ code: 'en', label: 'English', englishLabel: 'English', htmlLang: 'en' }, en);
