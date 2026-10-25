import { fontFamily } from '../modding_api/Resources';
import { TEXT_SIZE_BASIS, type GameSettings } from '../core/GameSettings';
import { adoptPalette } from './adapt/theme';

/** 档案与独立界面配色分开应用 */
export function applySettingsPalette(settings: GameSettings): void {
  const palette = settings.applyPaletteToInterface !== false ? settings.profile : settings.interfacePalette;
  adoptPalette(palette?.mainColor ?? '#CB3A56', palette?.trimColor ?? '#5D3F51');
}

export function applyAppearance(settings: GameSettings): void {
  applySettingsPalette(settings);
  const style = document.documentElement.style;
  const font = fontFamily(settings.font);
  style.setProperty('--ql-font-body', font);
  style.setProperty('--ql-font-display', font);
  style.setProperty('--ql-font-mono', settings.font === 'system' ? 'ui-monospace, monospace' : font);
  style.setProperty('--q-font-body', font);
  style.setProperty('--q-font-display', font);
  style.setProperty('--safe-x', `${settings.safeX ?? 0}vw`);
  style.setProperty('--safe-y', `${settings.safeY ?? 0}dvh`);
  style.setProperty('--safe-inline-ratio', String(1 - 2 * (settings.safeX ?? 0) / 100));
  style.setProperty('--text-scale', String((settings.textScale ?? 1) * TEXT_SIZE_BASIS));
  style.setProperty('--ui-scale', String(settings.uiScale ?? 1));
  // --q-scale 用设计基准倍率
  style.setProperty('--q-safe-x', `${settings.safeX ?? 0}vw`);
  style.setProperty('--q-safe-y', `${settings.safeY ?? 0}dvh`);
  style.setProperty('--q-scale', String(settings.textScale ?? 1));
  style.setProperty('--q-size', String(settings.uiScale ?? 1));
  const crosshair = document.getElementById('crosshair');
  if (crosshair) crosshair.dataset.kind = settings.crosshair ?? 'dot';
}
