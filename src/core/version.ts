/**
 * 版本常量单一真源。
 * 产品版本不在此处，唯一真源为 package.json 的 version。
 * 规范见根目录《版本管理规范.txt》，结构说明见同名文档。
 */

/** 世界生成算法与存档兼容版本，算法变更即 +1 */
export const GENERATOR_VERSION = 25;

/** 仍可读入的旧生成算法版本，升序且只增不减 */
export const LEGACY_GENERATOR_VERSIONS: readonly number[] = [14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24];

/** 生成算法版本能否被当前版本读入 */
export function isSupportedGeneratorVersion(value: unknown): boolean {
  return value === GENERATOR_VERSION || LEGACY_GENERATOR_VERSIONS.includes(value as number);
}

/** 内容数据格式版本：参数表与派生清单共用 */
export const CONTENT_DATA_VERSION = 3;

/** 本地存档封装版本：_localSave 元数据格式 */
export const SAVE_FORMAT_VERSION = 1;

/** 内容数据一律不自写 version，统一由此处声明 */
export function assertNoTableVersion(file: string, table: object): void {
  if ((table as { version?: unknown }).version !== undefined) {
    throw new Error(`[版本] ${file} 不应写 version，改由 src/core/version.ts 的 CONTENT_DATA_VERSION 统一声明`);
  }
}
