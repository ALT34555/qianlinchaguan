import { fontFamily } from '../modding_api/Resources';
import { TEXT_SIZE_BASIS, type GameSettings } from '../core/GameSettings';

export function applyAppearance(settings: GameSettings): void {
  const style = document.documentElement.style;
  const font = fontFamily(settings.font);
  style.setProperty('--ql-font-body', font);
  style.setProperty('--ql-font-display', font);
  style.setProperty('--ql-font-mono', settings.font === 'system' ? 'ui-monospace, monospace' : font);
  style.setProperty('--safe-x', `${settings.safeX ?? 0}vw`);
  style.setProperty('--safe-y', `${settings.safeY ?? 0}dvh`);
  style.setProperty('--safe-inline-ratio', String(1 - 2 * (settings.safeX ?? 0) / 100));
  style.setProperty('--text-scale', String((settings.textScale ?? 1) * TEXT_SIZE_BASIS));
  style.setProperty('--ui-scale', String(settings.uiScale ?? 1));
  const crosshair = document.getElementById('crosshair');
  if (crosshair) crosshair.dataset.kind = settings.crosshair ?? 'dot';
}
