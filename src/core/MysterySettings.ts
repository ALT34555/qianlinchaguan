export interface MysterySettings { timeSpeed: number; menuAutoPause: boolean }

export function defaultMysterySettings(): MysterySettings {
  return { timeSpeed: 72, menuAutoPause: false };
}

export function parseMysterySettings(value: unknown): MysterySettings {
  if (value === undefined) return defaultMysterySettings();
  if (!value || typeof value !== 'object') throw new Error('玄虚设置无效。');
  const settings = value as MysterySettings;
  if (!Number.isFinite(settings.timeSpeed) || settings.timeSpeed < 0 || settings.timeSpeed > 86400 ||
      typeof settings.menuAutoPause !== 'boolean') throw new Error('玄虚设置无效。');
  return { timeSpeed: settings.timeSpeed, menuAutoPause: settings.menuAutoPause };
}
