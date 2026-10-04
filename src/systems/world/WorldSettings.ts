/** 顺序固定为由热到冷；基础地形编号和气候归属互相独立。 */
export const CLIMATES = [
  { name: '热带', zone: 100, color: '#e7ab64' },
  { name: '亚热带', zone: 200, color: '#aac76c' },
  { name: '常规', zone: 0, color: '#68b698' },
  { name: '温带', zone: 300, color: '#7caec4' },
  { name: '寒带', zone: 400, color: '#c6dbea' },
] as const;

export type ClimateWeights = [number, number, number, number, number];
export const DEFAULT_CLIMATE_WEIGHTS: ClimateWeights = [20, 20, 20, 20, 20];
export const GENERATOR_VERSION = 3;

export function normalizeClimateWeights(value: unknown): ClimateWeights {
  if (!Array.isArray(value) || value.length !== 5 ||
      value.some(v => typeof v !== 'number' || !Number.isFinite(v) || v < 0)) {
    throw new Error('请填写五个非负的气候权重。');
  }
  const sum = value.reduce((a, b) => a + b, 0);
  if (!Number.isFinite(sum) || sum <= 0) throw new Error('至少一种气候的权重必须大于 0。');
  return value.map(v => v / sum) as ClimateWeights;
}
