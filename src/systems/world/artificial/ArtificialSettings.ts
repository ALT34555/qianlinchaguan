export interface ArtificialSettings {
  enabled: boolean;
  expansion: number;
  evolution: number;
  independence: boolean;
}

export const DEFAULT_ARTIFICIAL: Readonly<ArtificialSettings> = Object.freeze({
  enabled: false, expansion: 1, evolution: 1, independence: false,
});

export function normalizeArtificial(value: unknown = DEFAULT_ARTIFICIAL): ArtificialSettings {
  if (!value || typeof value !== 'object') throw new Error('人工区块设置无效。');
  const v = value as ArtificialSettings;
  if (typeof v.enabled !== 'boolean' || typeof v.independence !== 'boolean' ||
      [v.expansion, v.evolution].some(n => !Number.isFinite(n) || n < 0 || n > 4 || !Number.isInteger(n * 4)) ||
      v.evolution > v.expansion) throw new Error('人工区块速度须为 0 或 ×0.25～×4，进化不可快于扩张。');
  return { enabled: v.enabled, expansion: v.expansion, evolution: v.evolution,
    independence: v.enabled && v.evolution > 0 && v.independence };
}
