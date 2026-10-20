/** 世界规则身份，详见同名文档 */

/** 三层版本号：骨架、细节、区块 */
export interface WorldRuleId {
  generator: string;
  skeleton: number;
  detail: number;
  chunk: number;
}

/** 宏观星球当前规则身份 */
export const MACRO_RULE_ID: WorldRuleId = Object.freeze({ generator: 'planet-macro', skeleton: 2, detail: 3, chunk: 4 });

const PATTERN = /^([a-z][a-z0-9-]*)\.(\d+)\.(\d+)\.(\d+)$/;

/** 序列化规则身份 */
export function formatRuleId(id: WorldRuleId): string {
  return `${id.generator}.${id.skeleton}.${id.detail}.${id.chunk}`;
}

/** 解析规则身份，非法返回 null */
export function parseRuleId(value: unknown): WorldRuleId | null {
  if (typeof value !== 'string') return null;
  const m = PATTERN.exec(value);
  if (!m) return null;
  const [, generator, skeleton, detail, chunk] = m;
  const nums = [Number(skeleton), Number(detail), Number(chunk)];
  if (nums.some(n => !Number.isSafeInteger(n) || n < 0)) return null;
  return { generator, skeleton: nums[0], detail: nums[1], chunk: nums[2] };
}

/** 当前版本的规则身份字符串 */
export const CURRENT_RULE_ID = formatRuleId(MACRO_RULE_ID);

export type RuleVerdict =
  | { kind: 'match' }
  | { kind: 'detail-stale'; saved: WorldRuleId }
  | { kind: 'chunk-stale'; saved: WorldRuleId }
  | { kind: 'reject'; reason: string };

/** 单次抓取判定，支持按层降级 */
export function checkRuleId(saved: unknown): RuleVerdict {
  if (saved === undefined || saved === null) {
    return { kind: 'reject', reason: '此存档来自旧版生成规则（无版本标识），当前版本不再载入。请使用种子新建世界。' };
  }
  const id = parseRuleId(saved);
  if (!id) return { kind: 'reject', reason: `存档的生成规则版本无法识别：${String(saved)}` };
  if (id.generator !== MACRO_RULE_ID.generator) {
    return { kind: 'reject', reason: `存档使用 ${id.generator} 生成器，当前版本仅支持 ${MACRO_RULE_ID.generator}。` };
  }
  if (id.skeleton !== MACRO_RULE_ID.skeleton) {
    return { kind: 'reject', reason: `全球骨架版本不符（存档 ${id.skeleton}，当前 ${MACRO_RULE_ID.skeleton}）。请使用种子新建世界。` };
  }
  if (id.detail > MACRO_RULE_ID.detail || id.chunk > MACRO_RULE_ID.chunk) {
    return { kind: 'reject', reason: `存档由更新的版本生成（${formatRuleId(id)}），请更新游戏后再载入。` };
  }
  if (id.detail !== MACRO_RULE_ID.detail) return { kind: 'detail-stale', saved: id };
  if (id.chunk !== MACRO_RULE_ID.chunk) return { kind: 'chunk-stale', saved: id };
  return { kind: 'match' };
}

/** 能否载入（降级也算可载入） */
export function isLoadable(saved: unknown): boolean {
  return checkRuleId(saved).kind !== 'reject';
}
