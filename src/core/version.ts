/** 版本常量单一真源，详见同名文档 */

/** 内容数据格式版本：参数表与派生清单共用 */
export const CONTENT_DATA_VERSION = 3;

export const SAVE_FORMAT_VERSION = 3;

/** 内容数据一律不自写 version，统一由此处声明 */
export function assertNoTableVersion(file: string, table: object): void {
  if ((table as { version?: unknown }).version !== undefined) {
    throw new Error(`[版本] ${file} 不应写 version，改由 src/core/version.ts 的 CONTENT_DATA_VERSION 统一声明`);
  }
}
