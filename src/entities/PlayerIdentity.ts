export const DEFAULT_REN_MING = '赤';

export function renName(value: unknown, fallback = DEFAULT_REN_MING): string {
  if (value === undefined) return fallback;
  if (typeof value !== 'string' || value.length > 40 || /[\x00-\x1f\x7f]/.test(value)) throw new Error('人名无效（最多 40 个字符）。');
  return value.trim() || fallback;
}
