import { ACTIONS, actionLabel, defaultSettings, keyLabel, validateSettings, validBinding, type GameAction, type GameSettings } from '../core/GameSettings';
import { availableLocales, getLocale, onLocaleChange, setLocale, t } from '../i18n';

/** 开始界面与 ESC 菜单共用 */
export class SettingsPanel {
  private events = new AbortController();
  private draft: GameSettings;
  private pending: GameAction | null = null;
  private status!: HTMLElement;
  private readonly stopLocaleWatch: () => void;
  constructor(private readonly root: HTMLElement, current: GameSettings, private readonly apply: (value: GameSettings) => void, private readonly back: () => void) {
    this.draft = validateSettings(current);
    // 语言一变就整块重绘
    this.stopLocaleWatch = onLocaleChange(() => this.render());
    this.render();
  }

  /** 重建面板 DOM 并重新绑定事件（构造与语言切换 */
  private render(): void {
    this.events.abort();
    this.events = new AbortController();
    const localeOptions = availableLocales()
      .map(meta => `<option value="${meta.code}"${meta.code === getLocale() ? ' selected' : ''}>${meta.label}</option>`)
      .join('');
    this.root.innerHTML = `<div class="panel-heading"><h1>${t('settings.title')}</h1><button class="menu-back" data-back>${t('settings.back')}</button></div>
      <p class="panel-intro">${t('settings.intro')}</p>
      <div class="settings-grid"><section class="settings-group"><h2>${t('settings.display')}</h2>
        <label class="setting-row"><span>${t('settings.fov')} <small>${t('settings.fov.hint')}</small></span><input data-fov type="number" min="30" max="120" step="1"></label>
        <label class="setting-row"><span>${t('settings.speed')} <small>${t('settings.speed.hint')}</small></span><input data-speed type="number" min="0" max="86400" step="0.5"></label>
        <div class="speed-presets">${[0, 1, 72, 360].map(n => `<button data-speed-preset="${n}">${n === 0 ? t('settings.speed.pause') : `${n}×`}</button>`).join('')}</div>
        <label class="setting-row"><span>${t('settings.touch')} <small>${t('settings.touch.hint')}</small></span><input data-touch type="checkbox"></label>
        <label class="setting-row"><span>${t('settings.language')} <small>${t('settings.language.hint')}</small></span><select data-locale>${localeOptions}</select></label>
      </section><section class="settings-group"><h2>${t('settings.keys')}</h2><p class="setting-note">${t('settings.keys.note')}</p>
        ${ACTIONS.map(action => `<div class="key-row"><span>${actionLabel(action)}</span><button class="key-binding" data-bind="${action}"></button></div>`).join('')}
      </section></div><p class="menu-status" role="status" aria-live="polite"></p><div class="settings-actions"><button class="secondary" data-reset>${t('settings.reset')}</button><button class="primary" data-apply>${t('settings.apply')}</button></div>`;
    this.status = this.root.querySelector('.menu-status')!;
    this.renderValues();
    const signal = this.events.signal;
    this.root.querySelector('[data-back]')!.addEventListener('click', () => this.back(), { signal });
    this.root.querySelector('[data-reset]')!.addEventListener('click', () => {
      this.pending = null;
      this.draft = { ...defaultSettings(), viewMode: this.draft.viewMode };
      this.renderValues();
      this.status.textContent = t('settings.status.reset');
    }, { signal });
    this.root.querySelector('[data-apply]')!.addEventListener('click', () => this.submit(), { signal });
    this.root.querySelector<HTMLSelectElement>('[data-locale]')!.addEventListener('change', event => {
      // setLocale 会触发 onLocaleCh
      setLocale((event.currentTarget as HTMLSelectElement).value);
    }, { signal });
    this.root.querySelectorAll<HTMLButtonElement>('[data-bind]').forEach(button => button.addEventListener('click', () => {
      this.pending = button.dataset.bind as GameAction; this.renderKeys(); this.status.textContent = t('settings.status.capturing');
    }, { signal }));
    this.root.querySelectorAll<HTMLButtonElement>('[data-speed-preset]').forEach(button => button.addEventListener('click', () => {
      this.field<HTMLInputElement>('[data-speed]').value = button.dataset.speedPreset!;
    }, { signal }));
    window.addEventListener('keydown', this.captureKey, { capture: true, signal });
  }

  private field<T extends HTMLElement>(selector: string): T { return this.root.querySelector<T>(selector)!; }
  private renderValues(): void {
    this.field<HTMLInputElement>('[data-fov]').value = String(this.draft.fov);
    this.field<HTMLInputElement>('[data-speed]').value = String(this.draft.timeSpeed);
    this.field<HTMLInputElement>('[data-touch]').checked = !!this.draft.touchControls;
    this.renderKeys();
  }
  private renderKeys(): void {
    this.root.querySelectorAll<HTMLButtonElement>('[data-bind]').forEach(button => {
      const action = button.dataset.bind as GameAction;
      button.textContent = this.pending === action ? t('settings.pressKey') : keyLabel(this.draft.keyBindings[action]);
      button.classList.toggle('capturing', action === this.pending);
    });
  }
  private captureKey = (event: KeyboardEvent): void => {
    if (!this.pending) return;
    event.preventDefault(); event.stopImmediatePropagation();
    if (event.repeat) return;
    if (event.code === 'Escape') { this.pending = null; this.status.textContent = t('settings.status.cancelled'); }
    else if (!validBinding(event.code) || event.altKey || event.metaKey || (event.ctrlKey && !event.code.startsWith('Control'))) {
      this.status.textContent = t('settings.status.invalidKey'); return;
    } else {
      const next = { ...this.draft, keyBindings: { ...this.draft.keyBindings, [this.pending]: event.code } };
      try { this.draft = validateSettings(next); this.pending = null; this.status.textContent = t('settings.status.updated'); }
      catch (error) { this.status.textContent = (error as Error).message; }
    }
    this.renderKeys();
  };
  private submit(): void {
    try {
      if (this.pending) throw new Error(t('settings.status.pending'));
      const fov = this.field<HTMLInputElement>('[data-fov]').value.trim();
      const speed = this.field<HTMLInputElement>('[data-speed]').value.trim();
      const value = validateSettings({ ...this.draft, fov: fov ? Number(fov) : NaN, timeSpeed: speed ? Number(speed) : NaN,
        viewMode: this.draft.viewMode, touchControls: this.field<HTMLInputElement>('[data-touch]').checked });
      this.apply(value); this.draft = value; this.status.textContent = t('settings.status.applied');
    } catch (error) { this.status.textContent = error instanceof Error ? error.message : t('settings.status.failed'); }
  }
  destroy(): void { this.stopLocaleWatch(); this.events.abort(); }
}
