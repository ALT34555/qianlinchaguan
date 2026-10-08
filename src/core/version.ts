/** 版本常量单一真源，规范详见同名文档。 */

/** 世界生成算法与存档兼容版本，算法变更即 +1 */
export const GENERATOR_VERSION = 30;

/** 兼容生成算法版本列表，升序递增。 */
export const LEGACY_GENERATOR_VERSIONS: readonly number[] = [14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29];

/** 生成算法版本能否被当前版本读入 */
export function isSupportedGeneratorVersion(value: unknown): boolean {
  return value === GENERATOR_VERSION || LEGACY_GENERATOR_VERSIONS.includes(value as number);
}

/** 内容数据格式版本：参数表与派生清单共用 */
export const CONTENT_DATA_VERSION = 3;

export const SAVE_FORMAT_VERSION = 2;

/** 内容数据一律不自写 version，统一由此处声明 */
export function assertNoTableVersion(file: string, table: object): void {
  if ((table as { version?: unknown }).version !== undefined) {
    throw new Error(`[版本] ${file} 不应写 version，改由 src/core/version.ts 的 CONTENT_DATA_VERSION 统一声明`);
  }
}
