import { mountedFonts } from '../modding_api/Resources';
import { icon } from './Icons';
import { renderJoystickMarkup, renderActionMarkup } from './TouchControls';
import { colorControl, bindColorControls } from './ColorControls';
import { playerProfile, profileHeightRange } from '../entities/PlayerProfile';
import { SettingsConfirmation } from './SettingsConfirmation';
import { colorName, setThemeMode, themeState } from './adapt/runtime';
import { applySettingsPalette } from './Appearance';
import { fitBookmarks } from './adapt/bookmarks';
import { ACTIONS, TOUCH_ITEMS, GRAPHICS_PRESETS, DENSE_VEGETATION_MULTIPLIERS, actionLabel, defaultSettings, graphicsPreset, frameRateStops, keyLabel, validateSettings, validBinding, type GameAction, type GameSettings, type TouchItem, type TouchPoint } from '../core/GameSettings';
import { availableLocales, getLocale, onLocaleChange, setLocale, t } from '../i18n';
import { CHUNK_SIZE } from '../core/config';
import { MIN_RENDER_DISTANCE, MAX_RENDER_DISTANCE, renderChunkOffsets } from '../core/RenderDistance';
import { displayRefreshRate } from './DisplayRefresh';

/** 开始界面与 ESC 菜单共用 */
export class SettingsPanel {
  private events = new AbortController();
  private layoutEvents: AbortController | null = null;
  private stopColors = () => {};
  private stopBookmarks = () => {};
  private draft: GameSettings;
  private saved: GameSettings;
  private confirmation: SettingsConfirmation | null = null;
  private pending: GameAction | null = null;
  private activeTab = 0;
  private status!: HTMLElement;
  private readonly stopLocaleWatch: () => void;
  private get mysteryAvailable(): boolean { return !!this.options.worldActive && !this.options.hideMystery; }
  constructor(private readonly root: HTMLElement, current: GameSettings, private readonly apply: (value: GameSettings, persist?: boolean) => void, private readonly back: () => void, private readonly options: { headingBack?: boolean; worldActive?: boolean; hideMystery?: boolean; profileLocked?: boolean; playMode?: string } = {}) {
    this.draft = validateSettings(current);
    if (this.draft.font !== 'system' && !mountedFonts().some(font => font.id === this.draft.font)) this.draft.font = 'system';
    this.saved = validateSettings(current);
    // 语言一变就整块重绘
    this.stopLocaleWatch = onLocaleChange(() => this.render());
    this.render();
  }

  /** 重建面板 DOM 并重新绑定事件（构造与语言切换 */
  private render(): void {
    this.root.classList.add('settings-panel');
    this.stopBookmarks();
    this.stopColors();
    this.events.abort();
    this.events = new AbortController();
    const escape = (value: string) => value.replace(/[&<>"']/g, char => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[char]!));
    const fontOptions = mountedFonts().map(font => `<option value="${escape(font.id)}">${escape(font.id === 'song' || font.id === 'round' ? t(`settings.font.${font.id}`) : font.label)}</option>`).join('');
    const localeOptions = availableLocales()
      .map(meta => `<option value="${meta.code}"${meta.code === getLocale() ? ' selected' : ''}>${meta.label}</option>`)
      .join('');
    this.root.innerHTML = `<div class="panel-heading"><h1>${t('settings.title')}</h1>${this.options.headingBack === false ? '' : `<button class="menu-back" data-back>${t(this.options.worldActive ? 'settings.back' : 'menu.backToStart')}</button>`}</div>
      <div class="settings-scroll"><div class="settings-grid">
      <section class="settings-group"><h2>${t('settings.profile')}</h2>${this.options.worldActive ? '' : `<p class="setting-note">${t('profile.defaultHint')}</p>`}<fieldset class="profile-fields" ${this.options.profileLocked ? 'disabled' : ''}>
        <label class="setting-row"><span>${t('profile.name')}</span><input data-default-name maxlength="40" autocomplete="off"></label>
        <label class="setting-row"><span>${t('profile.height')}</span><input data-profile-height type="number" min="${profileHeightRange(this.options.playMode).min}" max="${profileHeightRange(this.options.playMode).max}" step="1" aria-label="${t('profile.height')}"></label>
        ${colorControl('profile-main-color', t('profile.mainColor'), this.draft.profile!.mainColor, this.options.profileLocked)}
        ${colorControl('profile-trim-color', t('profile.trimColor'), this.draft.profile!.trimColor, this.options.profileLocked)}
      </fieldset>${!this.options.worldActive ? `<label class="setting-row"><span>${t('settings.profilePaletteToInterface')}</span><input type="checkbox" data-apply-palette></label>` : ''}</section>
      <section class="settings-group"><h2>${t('settings.interface')}</h2>
        <label class="setting-row"><span>${t('settings.language')}</span><select data-locale>${localeOptions}</select></label>
        <label class="setting-row"><span>${t('settings.font')}</span><select data-extra="font"><option value="system">${t('settings.font.system')}</option>${fontOptions}</select></label>
        <label class="setting-row"><span>${t('settings.theme')}</span><select data-theme-mode><option value="light">${t('settings.theme.light')}</option><option value="dark">${t('settings.theme.dark')}</option><option value="auto">${t('settings.theme.auto')}</option></select></label>
        ${!this.options.worldActive ? `<div class="palette-lab">
          <div class="palette-head"><span class="spec-label">${t('settings.palette')}</span></div>
          ${colorControl('interface-main-color', t('settings.interfaceMainColor'), this.draft.interfacePalette!.mainColor)}
          <span class="palette-color-name" data-palette-name="main"></span>
          ${colorControl('interface-trim-color', t('settings.interfaceTrimColor'), this.draft.interfacePalette!.trimColor)}
          <span class="palette-color-name" data-palette-name="trim"></span>
        </div>` : ''}
        ${this.slider('safeX', 'settings.safeX', 0, 35, 1, '%')}
        ${this.slider('safeY', 'settings.safeY', 0, 35, 1, '%')}
        ${this.slider('textScale', 'settings.textScale', .25, 4, .05, '×')}
        ${this.slider('uiScale', 'settings.uiScale', .25, 4, .05, '×')}
      </section>
      <section class="settings-group"><h2>${t('settings.graphics')}</h2>
        <label class="setting-row"><span>${t('settings.graphicsPreset')}</span><select data-graphics-preset>${GRAPHICS_PRESETS.map(p=>`<option value="${p}">${t('settings.graphicsPreset.'+p)}</option>`).join('')}</select></label>
        <p class="setting-note">${t('settings.graphicsPreset.hint')}</p>
        <label class="setting-row"><span>${t('settings.antialias')}</span><input data-antialias type="checkbox"></label>
        <label class="setting-row"><span>${t('settings.fov')}</span><input data-fov type="number" min="30" max="120" step="1"></label>
        <div class="distance-pair">
          <label class="distance-heading distance-heading-world" for="settings-render-distance"><span>${t('settings.renderDistance')}</span><output data-distance-value></output></label>
          <input id="settings-render-distance" aria-label="${t('settings.renderDistance')}" data-render-distance data-range-stops="step" type="range" min="${MIN_RENDER_DISTANCE}" max="${MAX_RENDER_DISTANCE}" step="1">
          <span class="distance-connector" aria-hidden="true"></span>
          <label class="distance-heading distance-heading-vegetation" for="settings-vegetation-distance"><span>${t('settings.vegetation')}</span><output data-output="vegetationDistance" data-suffix="" data-multiplier="1"></output></label>
          <div class="distance-vegetation-track"><input id="settings-vegetation-distance" aria-label="${t('settings.vegetation')}" data-extra="vegetationDistance" data-range-stops="step" type="range" min="${MIN_RENDER_DISTANCE}" max="${MAX_RENDER_DISTANCE}" step="1"><span class="distance-stop" aria-hidden="true"></span></div>
        </div>
        <p class="setting-note" data-render-budget aria-live="polite"></p>
        <label class="setting-row"><span>${t('settings.showTopBar')}</span><input data-top-bar type="checkbox"></label>
        <details class="fold-group" id="fold-advanced-visuals">
          <summary><span class="fold-name">${t('settings.advancedVisuals')}</span><span class="fold-mark" aria-hidden="true"></span></summary>
          <div class="fold-body">
            <label class="setting-row"><span>${t('settings.particles')}</span><input data-particles type="checkbox"></label>
            <label class="setting-row"><span>${t('settings.waterReflections')}</span><input data-water-reflections type="checkbox"></label>
            <label class="setting-row"><span>${t('settings.waterWaves')} <small>${t('settings.waterWaves.hint')}</small></span><input data-water-waves type="checkbox"></label>
            <label class="setting-row"><span>${t('settings.mirrorReflections')} <small>${t('settings.comingSoon')}</small></span><input data-mirror-reflections type="checkbox" disabled></label>
            <label class="setting-row"><span>${t('settings.weatherAtmosphere')} <small>${t('settings.weatherAtmosphere.hint')}</small></span><input data-weather-atmosphere type="checkbox"></label>
            <label class="setting-row"><span>${t('settings.decorativeClouds')}</span><input data-decorative-clouds type="checkbox"></label>
            <label class="setting-row"><span>${t('settings.naturalPhysics')}</span><input data-natural-physics type="checkbox"></label>
            ${this.slider('denseVegetation', 'settings.denseVegetation', 0, 5, 1)}
            <p class="setting-note">${t('settings.denseVegetation.hint')}</p>
            <label class="setting-row"><span>${t('settings.statusFeedback')}</span><input data-status-feedback type="checkbox"></label>
          </div>
        </details>
        <label class="setting-row"><span>${t('settings.resolution')}</span><select data-extra="resolution"><option value="auto">${t('settings.resolution.auto')}</option>${['320x240', '640x480', '960x540', '1280x720', '1920x1080', '2560x1440'].map(n => `<option value="${n}">${n.replace('x', ' × ')}</option>`).join('')}</select></label>
        <label class="setting-row slider-row"><span>${t('settings.frameRate')} <output data-fps-output></output></span><input aria-label="${t('settings.frameRate')}" data-max-fps data-range-stops="step" type="range" min="0" step="1"></label>
        <div class="fps-stops" data-fps-stops aria-hidden="true"></div>
      </section>
      <section class="settings-group"><h2>${t('settings.controls')}</h2>
        <label class="setting-row"><span>${t('settings.touch')}</span><input data-touch type="checkbox"></label>
        <details class="fold-group" id="fold-touch-settings">
          <summary>
            <span class="fold-name">${t('settings.touchGroup')}</span>
            <span class="fold-lock" aria-hidden="true">${icon('menu-lock', 'fold-lock-icon')}</span>
            <span class="fold-mark" aria-hidden="true"></span>
          </summary>
          <div class="fold-body">
            <div class="setting-row"><span id="joystick-choice-label">${t('settings.joystickStyle')}</span></div>
            <div class="joystick-choices" role="group" aria-labelledby="joystick-choice-label">${['jxqy','bagua','luopan','lotus','minimal','cross','octagon','classic'].map(style => `<button type="button" data-joystick-choice="${style}" aria-label="${t('settings.joystickStyle.'+style)}"><span class="joystick-choice-art" inert aria-hidden="true">${renderJoystickMarkup(style as NonNullable<GameSettings['joystickStyle']>).replaceAll('<button ', '<span ').replaceAll('</button>', '</span>')}</span><span>${t('settings.joystickStyle.'+style)}</span></button>`).join('')}</div>
            ${this.slider('touchOpacity', 'settings.touchOpacity', .1, 1, .05, '%', 100)}
            <div class="setting-row layout-adjust-row">
              <span>${t('settings.touchLayout')}</span>
              <button type="button" class="primary" data-open-layout-editor>${t('settings.adjustTouchLayout')}</button>
            </div>
            <p class="setting-note">${t('settings.touchLayout.hint')}</p>
          </div>
        </details>
        <div class="setting-row"><span id="settings-crosshair-label">${t('settings.crosshair')}</span></div>
        <div class="crosshair-samples" role="group" aria-labelledby="settings-crosshair-label">${['none','dot','cross','aim','ring'].map(kind => `<button type="button" data-crosshair="${kind}" aria-pressed="${this.draft.crosshair === kind}" aria-label="${t(`settings.crosshair.${kind}`)}" title="${t(`settings.crosshair.${kind}`)}">${kind === 'none' ? t('settings.crosshair.none') : kind === 'dot' ? '·' : kind === 'cross' ? '＋' : icon(kind === 'aim' ? 'crosshair' : 'crosshair-ring')}</button>`).join('')}</div>
        <h3>${t('settings.keys')}</h3><p class="setting-note">${t('settings.keys.note')}</p>
        ${ACTIONS.map(action => `<div class="key-row"><span>${actionLabel(action)}</span><button class="key-binding" data-bind="${action}"></button></div>`).join('')}


      </section>
      ${this.mysteryAvailable ? `<section class="settings-group" data-mystery-group><h2>${t('settings.mystery')}</h2><p class="setting-note">${t('settings.mystery.current')}</p><fieldset data-mystery-fields style="border:0;padding:0;margin:0;min-width:0">
        <label class="setting-row"><span>${t('settings.speed')} <small>${t('settings.speed.hint')}</small></span><input data-speed type="number" min="0" max="86400" step="0.5"></label>
        <div class="speed-presets">${[0, 1, 72, 360].map(n => `<button data-speed-preset="${n}">${n === 0 ? t('settings.speed.pause') : `${n}×`}</button>`).join('')}</div>
        <label class="setting-row"><span>${t('settings.menuAutoPause')}</span><input data-menu-auto-pause type="checkbox"></label></fieldset>
      </section>` : ''}</div></div><p class="menu-status" role="status" hidden></p><div class="settings-actions"><p class="settings-footer-note" role="status" aria-live="polite">${t(this.mysteryAvailable ? 'settings.intro.world' : 'settings.intro')}</p><button class="secondary" data-reset>${t('settings.reset')}</button><button class="primary" data-apply>${t('settings.apply')}</button></div></div>`;
    this.status = this.root.querySelector('.settings-footer-note')!;
    this.renderTabs();
    this.renderValues();
    this.stopColors = bindColorControls(this.root);
    const signal = this.events.signal;
    this.root.querySelector('[data-back]')?.addEventListener('click', () => this.back(), { signal });
    this.root.querySelector('[data-reset]')!.addEventListener('click', () => {
      this.pending = null;
      this.draft = validateSettings({ ...defaultSettings(), viewMode: this.draft.viewMode, ...(this.options.profileLocked ? { profile: this.saved.profile, ren_ming: this.saved.ren_ming } : {}), ...(!this.mysteryAvailable ? { timeSpeed: this.saved.timeSpeed, menuAutoPause: this.saved.menuAutoPause } : {}) });
      this.renderValues();
      this.status.textContent = t('settings.status.reset');
    }, { signal });
    this.root.querySelector('[data-apply]')!.addEventListener('click', () => this.submit(), { signal });
    this.field<HTMLSelectElement>('[data-graphics-preset]').addEventListener('change', event => {
      this.readDraft();
      this.draft = { ...this.draft, ...graphicsPreset((event.currentTarget as HTMLSelectElement).value as NonNullable<GameSettings['graphicsPreset']>) };
      this.renderValues();
    }, { signal });
    this.field<HTMLInputElement>('[data-render-distance]').addEventListener('input', () => { this.updateDistanceLimit(); this.renderBudget(); }, { signal });
    this.field<HTMLInputElement>('[data-max-fps]').addEventListener('input', () => {
      this.field<HTMLOutputElement>('[data-fps-output]').value = this.frameRateText(this.readFrameRate());
    }, { signal });
    this.root.querySelector<HTMLSelectElement>('[data-locale]')!.addEventListener('change', event => {
      this.readDraft();
      // setLocale 会触发 onLocaleCh
      setLocale((event.currentTarget as HTMLSelectElement).value);
    }, { signal });
    const themeSelect = this.root.querySelector<HTMLSelectElement>('[data-theme-mode]');
    themeSelect?.addEventListener('change', () => setThemeMode(themeSelect.value as 'light' | 'dark' | 'auto'), { signal });
    this.bindPalette();
    this.root.querySelectorAll<HTMLButtonElement>('[data-crosshair]').forEach(button => {
      button.addEventListener('click', () => {
        this.draft.crosshair = button.dataset.crosshair as GameSettings['crosshair'];
        this.syncCrosshairChoices();
      }, { signal });
    });
    this.root.querySelectorAll<HTMLButtonElement>('[data-bind]').forEach(button => button.addEventListener('click', () => {
      this.readDraft();
      this.pending = button.dataset.bind as GameAction; this.renderKeys(); this.status.textContent = t('settings.status.capturing');
    }, { signal }));
    this.root.querySelectorAll<HTMLButtonElement>('[data-speed-preset]').forEach(button => button.addEventListener('click', () => {
      this.field<HTMLInputElement>('[data-speed]').value = button.dataset.speedPreset!;
    }, { signal }));
    this.root.querySelectorAll<HTMLInputElement>('[data-extra]').forEach(field => field.addEventListener('input', () => this.updateOutputs(), { signal }));
    this.field<HTMLButtonElement>('[data-open-layout-editor]').addEventListener('click', () => this.openScreenLayoutEditor(), { signal });
    this.root.querySelectorAll<HTMLButtonElement>('[data-joystick-choice]').forEach(button => {
      button.addEventListener('click', () => {
        this.readDraft();
        this.draft.joystickStyle = button.dataset.joystickChoice as GameSettings['joystickStyle'];
        this.syncJoystickChoices();
        this.apply(this.draft, false);
      }, { signal });
    });
    const foldTouch = this.field<HTMLDetailsElement>('#fold-touch-settings');
    foldTouch.querySelector('summary')!.addEventListener('click', event => {
      if (foldTouch.classList.contains('locked')) event.preventDefault();
    }, { signal });
    this.field<HTMLInputElement>('[data-touch]').addEventListener('change', event => {
      const checked = (event.currentTarget as HTMLInputElement).checked;
      this.draft.touchControls = checked;
      foldTouch.classList.toggle('locked', !checked);
      if (checked) foldTouch.open = true;
      else foldTouch.open = false;
      this.apply(this.draft, false);
    }, { signal });
    window.addEventListener('keydown', this.captureKey, { capture: true, signal });
  }

  private field<T extends HTMLElement>(selector: string): T { return this.root.querySelector<T>(selector)!; }
  /** 配色取色与界面预览 */
  private bindPalette(): void {
    if (this.options.profileLocked) { this.syncPalette(); return; }
    const signal = this.events.signal;
    const profileInput = (kind: 'main' | 'trim') => this.field<HTMLInputElement>(kind === 'main' ? '#profile-main-color' : '#profile-trim-color');
    const commit = (kind: 'main' | 'trim', hex: string) => {
      this.readDraft();
      this.draft.interfacePalette = { ...this.draft.interfacePalette!, [kind === 'main' ? 'mainColor' : 'trimColor']: hex };
      this.draft.applyPaletteToInterface = false;
      applySettingsPalette(this.draft);
      this.syncPalette();
    };
    for (const kind of ['main', 'trim'] as const) {
      const group = this.root.querySelector<HTMLElement>(`[data-color="interface-${kind}-color"]`);
      group?.addEventListener('input', () => {
        const input = group.querySelector<HTMLInputElement>('.color-hex input')!;
        if (input.validity.valid) commit(kind, input.value);
      }, { signal });
    }
    this.root.querySelectorAll<HTMLInputElement>('[data-apply-palette]').forEach(field => {
      field.addEventListener('change', () => {
        this.draft.applyPaletteToInterface = field.checked;
        this.root.querySelectorAll<HTMLInputElement>('[data-apply-palette]').forEach(other => { other.checked = field.checked; });
        applySettingsPalette(this.draft);
      }, { signal });
    });
    this.root.querySelectorAll('[data-color="profile-main-color"], [data-color="profile-trim-color"]').forEach(group => {
      group.addEventListener('input', () => {
        if (!profileInput('main').validity.valid || !profileInput('trim').validity.valid) return;
        this.draft.profile = playerProfile({ ...this.draft.profile, mainColor: profileInput('main').value, trimColor: profileInput('trim').value }, this.options.playMode);
        applySettingsPalette(this.draft);
        this.syncPalette();
      }, { signal });
    });
    this.syncPalette();
  }
  private syncPalette(): void {
    const pairs: Array<['main' | 'trim', string]> = [
      ['main', this.draft.interfacePalette?.mainColor ?? '#CB3A56'],
      ['trim', this.draft.interfacePalette?.trimColor ?? '#5D3F51'],
    ];
    for (const [kind, hex] of pairs) {
      const input = this.root.querySelector<HTMLInputElement>(`#interface-${kind}-color`);
      const name = this.root.querySelector<HTMLElement>(`[data-palette-name="${kind}"]`);
      if (input) { input.value = hex; input.dispatchEvent(new window.Event('input')); }
      if (name) name.textContent = `${colorName(hex)} · ${hex.toUpperCase()}`;
    }
    this.root.querySelectorAll<HTMLInputElement>('[data-apply-palette]').forEach(field => { field.checked = this.draft.applyPaletteToInterface !== false; });
  }

  private frameRateOptions(): number[] { return frameRateStops(displayRefreshRate()); }
  private frameRateIndex(options: number[], value: number): number {
    const exact = options.indexOf(value);
    if (exact >= 0) return exact;
    for (let i = options.length - 1; i >= 0; i--) if (options[i] !== 0 && options[i] <= value) return i;
    return options.length - 1;
  }
  private frameRateText(value: number): string {
    return value === 0 ? t('settings.frameRate.unlimited') : `${value} fps`;
  }
  private renderFrameRate(): void {
    const options = this.frameRateOptions();
    const slider = this.field<HTMLInputElement>('[data-max-fps]');
    slider.max = String(options.length - 1);
    slider.value = String(this.frameRateIndex(options, this.draft.frameRate ?? 0));
    this.field<HTMLOutputElement>('[data-fps-output]').value = this.frameRateText(options[Number(slider.value)]);
    const stops = this.field<HTMLElement>('[data-fps-stops]');
    stops.innerHTML = options.map((value, index) => `<span style="--fps-stop-ratio:${index / (options.length - 1)}">${value === 0 ? t('settings.frameRate.unlimited') : value}</span>`).join('');
  }
  private readFrameRate(): number {
    const options = this.frameRateOptions();
    const index = Number(this.field<HTMLInputElement>('[data-max-fps]').value);
    return options[Math.max(0, Math.min(options.length - 1, index))];
  }
  private renderTabs(): void {
    const tabs = document.createElement('div'); tabs.className = 'creator-tabs settings-tabs world-bookmark-tabs';
    tabs.setAttribute('role', 'tablist'); tabs.setAttribute('aria-label', t('settings.title'));
    const groups = Array.from(this.root.querySelectorAll<HTMLElement>('.settings-group'));
    const names = ['profile', 'interface', 'graphics', 'controls', 'mystery'];
    const select = (index: number) => {
      this.activeTab = index;
      groups.forEach((group, i) => { group.hidden = i !== index; });
      Array.from(tabs.children).forEach((button, i) => {
        button.setAttribute('aria-selected', String(i === index)); (button as HTMLElement).tabIndex = i === index ? 0 : -1;
      });
      this.field<HTMLElement>('.settings-scroll').scrollTop = 0;
    };
    groups.forEach((group, i) => {
      const button = document.createElement('button');
      const label = document.createElement('span'); label.className = 'bookmark-label'; label.textContent = t(`settings.${names[i]}`);
      button.append(label);
      button.id = `settings-tab-${names[i]}`; button.setAttribute('role', 'tab');
      group.id = `settings-section-${names[i]}`; group.setAttribute('role', 'tabpanel');
      group.setAttribute('aria-labelledby', button.id); button.setAttribute('aria-controls', group.id);
      button.onclick = () => select(i);
      button.onkeydown = event => {
        const vertical = tabs.getAttribute('aria-orientation') === 'vertical';
        const [previous, nextKey] = vertical ? ['ArrowUp', 'ArrowDown'] : ['ArrowLeft', 'ArrowRight'];
        if (![previous, nextKey, 'Home', 'End'].includes(event.code)) return;
        event.preventDefault(); event.stopPropagation();
        const next = event.code === 'Home' ? 0 : event.code === 'End' ? groups.length - 1 : (i + (event.code === nextKey ? 1 : groups.length - 1)) % groups.length;
        select(next); (tabs.children[next] as HTMLElement).focus();
      };
      tabs.append(button);
    });
    const scroll = this.field<HTMLElement>('.settings-scroll');
    const panel = document.createElement('div'); panel.className = 'settings-bookmark-panel bookmark-panel';
    const frame = document.createElement('div'); frame.className = 'bookmark-content-frame';
    scroll.before(panel); frame.append(scroll); panel.append(tabs, frame);
    this.stopBookmarks = fitBookmarks(panel);
    select(this.activeTab);
  }
  private renderValues(): void {
    this.field<HTMLSelectElement>('[data-graphics-preset]').value = this.draft.graphicsPreset!;
    this.field<HTMLInputElement>('[data-antialias]').checked = this.draft.antialias !== false;
    this.field<HTMLInputElement>('[data-default-name]').value = this.draft.ren_ming ?? '';
    this.field<HTMLInputElement>('[data-profile-height]').value = String(this.draft.profile!.heightCm);
    for (const key of ['mainColor', 'trimColor'] as const) {
      const input = this.field<HTMLInputElement>(key === 'mainColor' ? '#profile-main-color' : '#profile-trim-color');
      if (input) { input.value = this.draft.profile![key]; input.dispatchEvent(new window.Event('input')); }
    }
    this.field<HTMLInputElement>('[data-fov]').value = String(this.draft.fov);
    this.field<HTMLInputElement>('[data-render-distance]').value = String(this.draft.renderDistance);
    this.field<HTMLInputElement>('[data-extra="vegetationDistance"]').max = String(this.draft.renderDistance);
    const speed = this.root.querySelector<HTMLInputElement>('[data-speed]');
    if (speed) speed.value = String(this.draft.timeSpeed);
    const touchEnabled = !!this.draft.touchControls;
    this.field<HTMLInputElement>('[data-touch]').checked = touchEnabled;
    const foldTouch = this.field<HTMLDetailsElement>('#fold-touch-settings');
    foldTouch.classList.toggle('locked', !touchEnabled);
    if (!touchEnabled) foldTouch.open = false;
    this.syncJoystickChoices();
    this.field<HTMLInputElement>('[data-particles]').checked = this.draft.particles !== false;
    this.field<HTMLInputElement>('[data-water-reflections]').checked = this.draft.waterReflections !== false;
    this.field<HTMLInputElement>('[data-water-waves]').checked = this.draft.waterWaves === true;
    this.field<HTMLInputElement>('[data-weather-atmosphere]').checked = this.draft.weatherAtmosphere !== false;
    this.field<HTMLInputElement>('[data-decorative-clouds]').checked = this.draft.decorativeClouds !== false;
    this.field<HTMLInputElement>('[data-natural-physics]').checked = this.draft.naturalPhysics !== false;
    this.field<HTMLInputElement>('[data-status-feedback]').checked = this.draft.statusFeedback !== false;
    this.field<HTMLInputElement>('[data-top-bar]').checked = this.draft.showTopBar !== false;
    const autoPause = this.root.querySelector<HTMLInputElement>('[data-menu-auto-pause]');
    if (autoPause) autoPause.checked = !!this.draft.menuAutoPause;
    this.root.querySelectorAll<HTMLInputElement | HTMLSelectElement>('[data-extra]').forEach(field => {
      field.value = String(this.draft[field.dataset.extra as keyof GameSettings]);
    });
    this.updateDistanceLimit(); this.renderBudget();
    this.renderFrameRate();
    this.updateOutputs();
    this.renderKeys();
    const themeSelect = this.root.querySelector<HTMLSelectElement>('[data-theme-mode]');
    if (themeSelect) themeSelect.value = themeState.theme;
    this.syncPalette();
    this.syncCrosshairChoices();
  }
  private syncJoystickChoices(): void {
    this.root.querySelectorAll<HTMLButtonElement>('[data-joystick-choice]').forEach(button => {
      button.setAttribute('aria-pressed', String(button.dataset.joystickChoice === this.draft.joystickStyle));
    });
  }
  private syncCrosshairChoices(): void {
    this.root.querySelectorAll<HTMLButtonElement>('[data-crosshair]').forEach(button => {
      button.setAttribute('aria-pressed', String(button.dataset.crosshair === this.draft.crosshair));
    });
  }
  private slider(key: string, label: string, min: number, max: number, step: number, suffix = '', multiplier = 1): string {
    return `<label class="setting-row slider-row"><span>${t(label)} <output data-output="${key}" data-suffix="${suffix}" data-multiplier="${multiplier}"></output></span><input aria-label="${t(label)}" data-extra="${key}" ${key === 'denseVegetation' ? 'data-range-stops="step"' : ''} type="range" min="${min}" max="${max}" step="${step}"></label>`;
  }
  private updateOutputs(): void {
    this.root.querySelectorAll<HTMLOutputElement>('[data-output]').forEach(output => {
      const value = Number(this.field<HTMLInputElement>(`[data-extra="${output.dataset.output}"]`).value);
      output.value = output.dataset.output === 'denseVegetation'
        ? `${value} · ${DENSE_VEGETATION_MULTIPLIERS[value]}×`
        : `${Number((value * Number(output.dataset.multiplier)).toFixed(2))}${output.dataset.suffix}`;
    });
  }
  private updateDistanceLimit(): void {
    const radius = this.field<HTMLInputElement>('[data-render-distance]').valueAsNumber;
    const vegetation = this.field<HTMLInputElement>('[data-extra="vegetationDistance"]');
    const distance = Math.min(Number(vegetation.value), radius);
    // 植被视距上限同步为渲染视距
    vegetation.max = String(radius);
    vegetation.value = String(distance);
    this.field<HTMLElement>('.distance-pair').style.setProperty('--distance-limit', String((radius - MIN_RENDER_DISTANCE) / (MAX_RENDER_DISTANCE - MIN_RENDER_DISTANCE)));
    this.field<HTMLOutputElement>('[data-distance-value]').value = String(radius);
    this.updateOutputs();
  }
  private readDraft(): void {
    for (const kind of ['main', 'trim']) {
      const input = this.root.querySelector<HTMLInputElement>(`#interface-${kind}-color`);
      if (input && !input.validity.valid) throw new Error(t('player.colorError'));
    }
    const profile = this.options.profileLocked ? this.saved.profile! : playerProfile({ name: this.field<HTMLInputElement>('[data-default-name]').value, heightCm: this.field<HTMLInputElement>('[data-profile-height]').valueAsNumber, mainColor: this.field<HTMLInputElement>('#profile-main-color').value, trimColor: this.field<HTMLInputElement>('#profile-trim-color').value }, this.options.playMode);
    this.updateDistanceLimit();
    const extra: Record<string, string | number> = {};
    this.root.querySelectorAll<HTMLInputElement | HTMLSelectElement>('[data-extra]').forEach(field => {
      extra[field.dataset.extra!] = field instanceof HTMLInputElement ? field.valueAsNumber : field.value;
    });
    this.draft = { ...this.draft, ...extra, ren_ming: profile.name, profile,
      fov: this.field<HTMLInputElement>('[data-fov]').valueAsNumber,
      renderDistance: this.field<HTMLInputElement>('[data-render-distance]').valueAsNumber,
      frameRate: this.readFrameRate(),
      graphicsPreset: this.field<HTMLSelectElement>('[data-graphics-preset]').value as NonNullable<GameSettings['graphicsPreset']>,
      antialias: this.field<HTMLInputElement>('[data-antialias]').checked,
      timeSpeed: this.root.querySelector<HTMLInputElement>('[data-speed]')?.valueAsNumber ?? this.draft.timeSpeed,
      touchControls: this.field<HTMLInputElement>('[data-touch]').checked,
      showTopBar: this.field<HTMLInputElement>('[data-top-bar]').checked,
      particles: this.field<HTMLInputElement>('[data-particles]').checked,
      waterReflections: this.field<HTMLInputElement>('[data-water-reflections]').checked,
      waterWaves: this.field<HTMLInputElement>('[data-water-waves]').checked,
      weatherAtmosphere: this.field<HTMLInputElement>('[data-weather-atmosphere]').checked,
      decorativeClouds: this.field<HTMLInputElement>('[data-decorative-clouds]').checked,
      naturalPhysics: this.field<HTMLInputElement>('[data-natural-physics]').checked,
      statusFeedback: this.field<HTMLInputElement>('[data-status-feedback]').checked,
      menuAutoPause: this.root.querySelector<HTMLInputElement>('[data-menu-auto-pause]')?.checked ?? this.draft.menuAutoPause };
  }
  // 唤起屏幕等大布局调整器
  private openScreenLayoutEditor(): void {
    this.readDraft();
    const cloneLayout = (layout: NonNullable<GameSettings["touchLayout"]>) => Object.fromEntries(TOUCH_ITEMS.map(key => [key, { ...layout[key]! }])) as Record<TouchItem, TouchPoint>;
    const origLayout = cloneLayout(this.draft.touchLayout!);
    let currentLayout = cloneLayout(origLayout);

    this.root.classList.add('layout-editor-active');
    const overlay = document.createElement('div');
    overlay.className = 'touch-layout-overlay';
    overlay.id = 'touch-layout-overlay';

    overlay.innerHTML = `<div class="touch-layout-mask">
${TOUCH_ITEMS.map(key => `<div class="layout-screen-item ${key === 'move' ? 'layout-screen-move' : 'layout-screen-action'}" data-screen-layout="${key}" tabindex="0" role="slider" aria-label="${t(`settings.touchLayout.${key}`)}"><span class="layout-item-badge">${t(`settings.touchLayout.${key}`)}</span>${key === 'move' ? renderJoystickMarkup(this.draft.joystickStyle ?? 'jxqy') : renderActionMarkup(key, this.draft.joystickStyle ?? 'jxqy')}</div>`).join('')}
    </div>
    <div class="layout-center-dialog">
      <div class="layout-center-heading">
        <span class="layout-center-title">${t('settings.adjustTouchLayout')}</span>
        <span class="layout-center-hint">${t('settings.touchLayoutPrompt')}</span>
      </div>
      <div class="layout-size-controls">${TOUCH_ITEMS.map(key => `<label><span>${t(`settings.touchLayout.${key}Size`)} <output data-layout-size-output="${key}"></output></span><input type="range" data-layout-size="${key}" min="0.5" max="2" step="0.05" aria-label="${t(`settings.touchLayout.${key}Size`)}"></label>`).join('')}</div>
      <div class="layout-center-actions">
        <button type="button" class="primary" data-layout-confirm>${t('settings.touchLayoutConfirm')}</button>
        <button type="button" class="secondary" data-layout-cancel>${t('settings.touchLayoutCancel')}</button>
        <button type="button" class="secondary" data-layout-reset>${t('settings.touchLayoutReset')}</button>
      </div>
    </div>`;

    document.body.appendChild(overlay);

    const updateHandles = () => {
      overlay.querySelectorAll<HTMLElement>('[data-screen-layout]').forEach(handle => {
        const key = handle.dataset.screenLayout as TouchItem;
        const point = currentLayout[key];
        handle.style.setProperty('--touch-size', String(point.scale ?? 1));
        handle.style.left = `${point.x}%`;
        handle.style.top = `${point.y}%`;
        handle.style.transform = `translate(-${point.x}%, -${point.y}%)`;
      });
      overlay.querySelectorAll<HTMLInputElement>('[data-layout-size]').forEach(input => {
        const key = input.dataset.layoutSize as TouchItem;
        input.value = String(currentLayout[key].scale ?? 1);
        overlay.querySelector<HTMLOutputElement>(`[data-layout-size-output="${key}"]`)!.value = `${Math.round((currentLayout[key].scale ?? 1) * 100)}%`;
      });
    };
    updateHandles();

    const abort = new AbortController();
    this.layoutEvents = abort;
    const signal = abort.signal;
    overlay.querySelectorAll<HTMLInputElement>('[data-layout-size]').forEach(input => {
      input.addEventListener('input', () => {
        const key = input.dataset.layoutSize as TouchItem;
        currentLayout[key] = { ...currentLayout[key], scale: input.valueAsNumber };
        updateHandles();
      }, { signal });
    });

    const close = () => {
      abort.abort();
      this.layoutEvents = null;
      overlay.remove();
      this.root.classList.remove('layout-editor-active');
    };

    overlay.querySelectorAll<HTMLElement>('[data-screen-layout]').forEach(handle => {
      let drag: { id: number; x: number; y: number } | null = null;
      handle.addEventListener('pointerdown', event => {
        const rect = handle.getBoundingClientRect();
        drag = { id: event.pointerId, x: event.clientX - rect.left, y: event.clientY - rect.top };
        handle.setPointerCapture(event.pointerId);
        event.preventDefault();
      }, { signal });

      handle.addEventListener('pointermove', event => {
        if (drag?.id !== event.pointerId) return;
        const area = handle.parentElement!.getBoundingClientRect();
        const rect = handle.getBoundingClientRect();
        const x = Math.max(0, Math.min(100, (event.clientX - area.left - drag.x) / Math.max(1, area.width - rect.width) * 100));
        const y = Math.max(0, Math.min(100, (event.clientY - area.top - drag.y) / Math.max(1, area.height - rect.height) * 100));
        const key = handle.dataset.screenLayout as TouchItem;
        currentLayout[key] = { ...currentLayout[key], x, y };
        updateHandles();
      }, { signal });

      for (const type of ['pointerup', 'pointercancel', 'lostpointercapture']) {
        handle.addEventListener(type, () => { drag = null; }, { signal });
      }

      handle.addEventListener('keydown', event => {
        if (!['ArrowLeft', 'ArrowRight', 'ArrowUp', 'ArrowDown'].includes(event.code)) return;
        event.preventDefault();
        event.stopPropagation();
        const key = handle.dataset.screenLayout as TouchItem;
        const point = currentLayout[key];
        currentLayout[key] = {
          ...point,
          x: Math.max(0, Math.min(100, point.x + (event.code === 'ArrowLeft' ? -2 : event.code === 'ArrowRight' ? 2 : 0))),
          y: Math.max(0, Math.min(100, point.y + (event.code === 'ArrowUp' ? -2 : event.code === 'ArrowDown' ? 2 : 0))),
        };
        updateHandles();
      }, { signal });
    });

    overlay.querySelector('[data-layout-confirm]')!.addEventListener('click', () => {
      this.draft.touchLayout = cloneLayout(currentLayout);
      this.apply(this.draft, false);
      close();
    }, { signal });

    overlay.querySelector('[data-layout-cancel]')!.addEventListener('click', () => {
      this.draft.touchLayout = { ...origLayout };
      this.apply(this.draft, false);
      close();
    }, { signal });

    overlay.querySelector('[data-layout-reset]')!.addEventListener('click', () => {
      const def = defaultSettings().touchLayout!;
      currentLayout = cloneLayout(def);
      updateHandles();
    }, { signal });

    window.addEventListener('keydown', event => {
      if (event.code === 'Escape') {
        event.preventDefault();
        this.draft.touchLayout = { ...origLayout };
        this.apply(this.draft, false);
        close();
      }
    }, { signal });
  }
  private renderBudget(): void {
    const radius = this.field<HTMLInputElement>('[data-render-distance]').valueAsNumber;
    const valid = Number.isInteger(radius) && radius >= MIN_RENDER_DISTANCE && radius <= MAX_RENDER_DISTANCE;
    this.field<HTMLElement>('[data-render-budget]').textContent = valid
      ? t('settings.renderDistance.budget', { count: renderChunkOffsets(radius).length, distance: radius * CHUNK_SIZE })
      : t('error.renderDistance');
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
      if (this.confirmation) return;
      if (this.pending) throw new Error(t('settings.status.pending'));
      this.readDraft();
      const value = validateSettings(this.draft);
      const guarded: (keyof GameSettings)[] = ['font', 'textScale', 'safeX', 'safeY', 'uiScale', 'resolution', 'renderDistance', 'vegetationDistance', 'denseVegetation', 'antialias', 'graphicsPreset'];
      if (guarded.some(key => value[key] !== this.saved[key])) {
        this.confirmation = new SettingsConfirmation(() => {
          this.confirmation = null;
          try { this.commit(value); }
          catch (error) { this.restore(); this.status.textContent = error instanceof Error ? error.message : t('settings.status.failed'); }
        }, () => { this.confirmation = null; this.restore(); });
        try { this.apply(value, false); }
        catch (error) { this.confirmation?.cancel(); throw error; }
      } else this.commit(value);
    } catch (error) { this.status.textContent = error instanceof Error ? error.message : t('settings.status.failed'); }
  }
  private commit(value: GameSettings): void {
    this.apply(value, true); this.saved = validateSettings(value); this.draft = validateSettings(value);
    this.status.textContent = t('settings.status.applied');
  }
  private restore(): void {
    try { this.apply(this.saved, false); }
    finally {
      this.draft = validateSettings(this.saved); this.pending = null; this.renderValues();
      this.status.textContent = t('settings.confirm.reverted');
    }
  }
  destroy(): void {
    this.stopBookmarks();
    this.confirmation?.cancel();
    this.stopLocaleWatch();
    this.events.abort();
    this.stopColors();
    this.layoutEvents?.abort();
    this.layoutEvents = null;
    this.root.classList.remove('layout-editor-active');
    document.getElementById('touch-layout-overlay')?.remove();
  }
}
