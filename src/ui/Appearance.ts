import { fontFamily } from '../modding_api/Resources';
import type { GameSettings } from '../core/GameSettings';

export function applyAppearance(settings: GameSettings): void {
  const style = document.documentElement.style;
  const font = fontFamily(settings.font);
  style.setProperty('--ql-font-body', font);
  style.setProperty('--ql-font-display', font);
  style.setProperty('--ql-font-mono', settings.font === 'system' ? 'ui-monospace, monospace' : font);
  style.setProperty('--safe-x', `${settings.safeX ?? 0}vw`);
  style.setProperty('--safe-y', `${settings.safeY ?? 0}dvh`);
  style.setProperty('--text-scale', String(settings.textScale ?? 1));
  style.setProperty('--ui-scale', String(settings.uiScale ?? 1));
  const crosshair = document.getElementById('crosshair');
  if (crosshair) crosshair.dataset.kind = settings.crosshair ?? 'dot';
}
