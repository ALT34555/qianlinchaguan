/** 缓存表公共操作：超限时按插入顺序淘汰最旧条目，避免整表清空。 */

/** 淘汰后保留的比例 */
export const CACHE_KEEP_RATIO = .75;

/** 超限时淘汰最旧条目到 keep 比例；Map 迭代顺序即插入顺序 */
export function trimMap<T>(table: Map<string, T>, keep = CACHE_KEEP_RATIO): void {
  if (table.size <= 1) return;
  const target = Math.floor(table.size * keep);
  while (table.size > target) table.delete(table.keys().next().value!);
}

/** Set 版的同类淘汰 */
export function trimSet(table: Set<string>, keep = CACHE_KEEP_RATIO): void {
  if (table.size <= 1) return;
  const target = Math.floor(table.size * keep);
  while (table.size > target) table.delete(table.keys().next().value!);
}
