export interface ArtificialSettings {
  enabled: boolean;
  initialCivilizationLimit?: number;
  expansion: number;
  evolution: number;
  independence: boolean;
}

export const DEFAULT_ARTIFICIAL: Readonly<Required<ArtificialSettings>> = Object.freeze({
  enabled: false, initialCivilizationLimit: 8, expansion: 1, evolution: 1, independence: false,
});

export function normalizeArtificial(value: unknown = DEFAULT_ARTIFICIAL): Required<ArtificialSettings> {
  if (!value || typeof value !== 'object') throw new Error('人工区块设置无效。');
  const v = value as ArtificialSettings;
  if (typeof v.enabled !== 'boolean' || typeof v.independence !== 'boolean' ||
      [v.expansion, v.evolution].some(n => !Number.isFinite(n) || n < 0 || n > 4 || !Number.isInteger(n * 4)) ||
      v.evolution > v.expansion) throw new Error('人工区块速度须为 0 或 ×0.25～×4，进化不可快于扩张。');
  const initialCivilizationLimit=v.initialCivilizationLimit===undefined?DEFAULT_ARTIFICIAL.initialCivilizationLimit:v.initialCivilizationLimit;
  if(!Number.isInteger(initialCivilizationLimit)||initialCivilizationLimit<0||initialCivilizationLimit>256)throw new Error('初始文明上限须为0～256的整数。');
  return { enabled: v.enabled, initialCivilizationLimit, expansion: v.expansion, evolution: v.evolution,
    independence: v.enabled && v.evolution > 0 && v.independence };
}
