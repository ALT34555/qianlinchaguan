import { mountedFonts } from '../modding_api/Resources';
import { icon } from './Icons';
import { SettingsConfirmation } from './SettingsConfirmation';
import { ACTIONS, actionLabel, defaultSettings, keyLabel, validateSettings, validBinding, type GameAction, type GameSettings } from '../core/GameSettings';
import { availableLocales, getLocale, onLocaleChange, setLocale, t } from '../i18n';
import { CHUNK_SIZE } from '../core/config';
import { MIN_RENDER_DISTANCE, MAX_RENDER_DISTANCE, renderChunkOffsets } from '../core/RenderDistance';

/** 开始界面与 ESC 菜单共用 */
export class SettingsPanel {
  private events = new AbortController();
  private draft: GameSettings;
  private saved: GameSettings;
  private confirmation: SettingsConfirmation | null = null;
  private pending: GameAction | null = null;
  private activeTab = 0;
  private status!: HTMLElement;
  private readonly stopLocaleWatch: () => void;
  constructor(private readonly root: HTMLElement, current: GameSettings, private readonly apply: (value: GameSettings, persist?: boolean) => void, private readonly back: () => void, private readonly options: { headingBack?: boolean; worldActive?: boolean } = {}) {
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
    this.events.abort();
    this.events = new AbortController();
    const escape = (value: string) => value.replace(/[&<>"']/g, char => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[char]!));
    const fontOptions = mountedFonts().map(font => `<option value="${escape(font.id)}">${escape(font.id === 'song' || font.id === 'round' ? t(`settings.font.${font.id}`) : font.label)}</option>`).join('');
    const localeOptions = availableLocales()
      .map(meta => `<option value="${meta.code}"${meta.code === getLocale() ? ' selected' : ''}>${meta.label}</option>`)
      .join('');
    this.root.innerHTML = `<div class="panel-heading"><h1>${t('settings.title')}</h1>${this.options.headingBack === false ? '' : `<button class="menu-back" data-back>${t('settings.back')}</button>`}</div>
      <p class="panel-intro">${t('settings.intro')}</p>
      <div class="settings-scroll"><div class="settings-grid">
      <section class="settings-group"><h2>${t('settings.interface')}</h2>
        <label class="setting-row"><span>${t('settings.language')}</span><select data-locale>${localeOptions}</select></label>
        <label class="setting-row"><span>${t('player.defaultName')}</span><input data-default-name maxlength="40" autocomplete="off"></label>
        <label class="setting-row"><span>${t('settings.font')}</span><select data-extra="font"><option value="system">${t('settings.font.system')}</option>${fontOptions}</select></label>
        ${this.slider('safeX', 'settings.safeX', 0, 35, 1, '%')}
        ${this.slider('safeY', 'settings.safeY', 0, 35, 1, '%')}
        ${this.slider('textScale', 'settings.textScale', .25, 4, .05, '×')}
        ${this.slider('uiScale', 'settings.uiScale', .25, 4, .05, '×')}
      </section>
      <section class="settings-group"><h2>${t('settings.graphics')}</h2>
        <label class="setting-row"><span>${t('settings.fov')}</span><input data-fov type="number" min="30" max="120" step="1"></label>
        <div class="distance-pair">
          <label class="distance-heading distance-heading-world" for="settings-render-distance"><span>${t('settings.renderDistance')}</span><output data-distance-value></output></label>
          <input id="settings-render-distance" aria-label="${t('settings.renderDistance')}" data-render-distance type="range" min="${MIN_RENDER_DISTANCE}" max="${MAX_RENDER_DISTANCE}" step="1">
          <span class="distance-connector" aria-hidden="true"></span>
          <label class="distance-heading distance-heading-vegetation" for="settings-vegetation-distance"><span>${t('settings.vegetation')}</span><output data-output="vegetationDistance" data-suffix="" data-multiplier="1"></output></label>
          <div class="distance-vegetation-track"><input id="settings-vegetation-distance" aria-label="${t('settings.vegetation')}" data-extra="vegetationDistance" type="range" min="${MIN_RENDER_DISTANCE}" max="${MAX_RENDER_DISTANCE}" step="1"><span class="distance-stop" aria-hidden="true"></span></div>
        </div>
        <p class="setting-note" data-render-budget aria-live="polite"></p>
        <label class="setting-row"><span>${t('settings.showTopBar')}</span><input data-top-bar type="checkbox"></label>
        <label class="setting-row"><span>${t('settings.particles')}</span><input data-particles type="checkbox"></label>
        <label class="setting-row"><span>${t('settings.resolution')}</span><select data-extra="resolution"><option value="auto">${t('settings.resolution.auto')}</option>${['320x240', '640x480', '960x540', '1280x720', '1920x1080', '2560x1440'].map(n => `<option value="${n}">${n.replace('x', ' × ')}</option>`).join('')}</select></label>
      </section>
      <section class="settings-group"><h2>${t('settings.controls')}</h2>
        <label class="setting-row"><span>${t('settings.touch')}</span><input data-touch type="checkbox"></label>
        ${this.slider('touchOpacity', 'settings.touchOpacity', .1, 1, .05, '%', 100)}
        <label class="setting-row"><span>${t('settings.crosshair')}</span><select data-extra="crosshair">${['none','dot','cross','aim','ring'].map(n => `<option value="${n}">${t(`settings.crosshair.${n}`)}</option>`).join('')}</select></label>
        <div class="crosshair-samples"><span>·</span><span>＋</span>${icon('crosshair')}${icon('crosshair-ring')}</div>
        <h3>${t('settings.keys')}</h3><p class="setting-note">${t('settings.keys.note')}</p>
        ${ACTIONS.map(action => `<div class="key-row"><span>${actionLabel(action)}</span><button class="key-binding" data-bind="${action}"></button></div>`).join('')}
        <h3>${t('settings.touchLayout')}</h3><p class="setting-note">${t('settings.touchLayout.hint')}</p>
        <div class="touch-layout-editor"><button class="layout-handle layout-move" data-layout="move" aria-label="${t('settings.touchLayout.move')}">${icon('stick-base')}${icon('stick-knob')}</button><button class="layout-handle layout-actions" data-layout="actions" aria-label="${t('settings.touchLayout.actions')}">↑ ◇ ↓</button></div>
      </section>
      <section class="settings-group"><h2>${t('settings.mystery')}</h2><p class="setting-note">${t(this.options.worldActive ? 'settings.mystery.current' : 'settings.mystery.unavailable')}</p><fieldset data-mystery-fields style="border:0;padding:0;margin:0;min-width:0" ${this.options.worldActive ? '' : 'disabled'}>
        <label class="setting-row"><span>${t('settings.speed')} <small>${t('settings.speed.hint')}</small></span><input data-speed type="number" min="0" max="86400" step="0.5"></label>
        <div class="speed-presets">${[0, 1, 72, 360].map(n => `<button data-speed-preset="${n}">${n === 0 ? t('settings.speed.pause') : `${n}×`}</button>`).join('')}</div>
        <label class="setting-row"><span>${t('settings.menuAutoPause')}</span><input data-menu-auto-pause type="checkbox"></label></fieldset>
      </section></div></div><p class="menu-status" role="status" aria-live="polite"></p><div class="settings-actions"><button class="secondary" data-reset>${t('settings.reset')}</button><button class="primary" data-apply>${t('settings.apply')}</button></div>`;
    this.status = this.root.querySelector('.menu-status')!;
    this.renderTabs();
    this.renderValues();
    const signal = this.events.signal;
    this.root.querySelector('[data-back]')?.addEventListener('click', () => this.back(), { signal });
    this.root.querySelector('[data-reset]')!.addEventListener('click', () => {
      this.pending = null;
      this.draft = { ...defaultSettings(), viewMode: this.draft.viewMode, ...(!this.options.worldActive ? { timeSpeed: this.saved.timeSpeed, menuAutoPause: this.saved.menuAutoPause } : {}) };
      this.renderValues();
      this.status.textContent = t('settings.status.reset');
    }, { signal });
    this.root.querySelector('[data-apply]')!.addEventListener('click', () => this.submit(), { signal });
    this.field<HTMLInputElement>('[data-render-distance]').addEventListener('input', () => { this.updateDistanceLimit(); this.renderBudget(); }, { signal });
    this.root.querySelector<HTMLSelectElement>('[data-locale]')!.addEventListener('change', event => {
      this.readDraft();
      // setLocale 会触发 onLocaleCh
      setLocale((event.currentTarget as HTMLSelectElement).value);
    }, { signal });
    this.root.querySelectorAll<HTMLButtonElement>('[data-bind]').forEach(button => button.addEventListener('click', () => {
      this.readDraft();
      this.pending = button.dataset.bind as GameAction; this.renderKeys(); this.status.textContent = t('settings.status.capturing');
    }, { signal }));
    this.root.querySelectorAll<HTMLButtonElement>('[data-speed-preset]').forEach(button => button.addEventListener('click', () => {
      this.field<HTMLInputElement>('[data-speed]').value = button.dataset.speedPreset!;
    }, { signal }));
    this.root.querySelectorAll<HTMLInputElement>('[data-extra]').forEach(field => field.addEventListener('input', () => this.updateOutputs(), { signal }));
    this.bindLayout(signal);
    window.addEventListener('keydown', this.captureKey, { capture: true, signal });
  }

  private field<T extends HTMLElement>(selector: string): T { return this.root.querySelector<T>(selector)!; }
  private renderTabs(): void {
    const tabs = document.createElement('div'); tabs.className = 'creator-tabs settings-tabs';
    tabs.setAttribute('role', 'tablist'); tabs.setAttribute('aria-label', t('settings.title'));
    const groups = Array.from(this.root.querySelectorAll<HTMLElement>('.settings-group'));
    const names = ['interface', 'graphics', 'controls', 'mystery'];
    const select = (index: number) => {
      this.activeTab = index;
      groups.forEach((group, i) => { group.hidden = i !== index; });
      Array.from(tabs.children).forEach((button, i) => {
        button.setAttribute('aria-selected', String(i === index)); (button as HTMLElement).tabIndex = i === index ? 0 : -1;
      });
      this.field<HTMLElement>('.settings-scroll').scrollTop = 0;
    };
    groups.forEach((group, i) => {
      const button = document.createElement('button'); button.textContent = t(`settings.${names[i]}`);
      button.id = `settings-tab-${names[i]}`; button.setAttribute('role', 'tab');
      group.id = `settings-section-${names[i]}`; group.setAttribute('role', 'tabpanel');
      group.setAttribute('aria-labelledby', button.id); button.setAttribute('aria-controls', group.id);
      button.onclick = () => select(i);
      button.onkeydown = event => {
        if (!['ArrowLeft', 'ArrowRight', 'Home', 'End'].includes(event.code)) return;
        event.preventDefault(); event.stopPropagation();
        const next = event.code === 'Home' ? 0 : event.code === 'End' ? 3 : (i + (event.code === 'ArrowRight' ? 1 : 3)) % 4;
        select(next); (tabs.children[next] as HTMLElement).focus();
      };
      tabs.append(button);
    });
    this.field<HTMLElement>('.settings-scroll').before(tabs); select(this.activeTab);
  }
  private renderValues(): void {
    this.field<HTMLInputElement>('[data-default-name]').value = this.draft.ren_ming ?? '';
    this.field<HTMLInputElement>('[data-fov]').value = String(this.draft.fov);
    this.field<HTMLInputElement>('[data-render-distance]').value = String(this.draft.renderDistance);
    this.field<HTMLInputElement>('[data-extra="vegetationDistance"]').max = String(this.draft.renderDistance);
    this.field<HTMLInputElement>('[data-speed]').value = String(this.draft.timeSpeed);
    this.field<HTMLInputElement>('[data-touch]').checked = !!this.draft.touchControls;
    this.field<HTMLInputElement>('[data-particles]').checked = this.draft.particles !== false;
    this.field<HTMLInputElement>('[data-top-bar]').checked = this.draft.showTopBar !== false;
    this.field<HTMLInputElement>('[data-menu-auto-pause]').checked = !!this.draft.menuAutoPause;
    this.root.querySelectorAll<HTMLInputElement | HTMLSelectElement>('[data-extra]').forEach(field => {
      field.value = String(this.draft[field.dataset.extra as keyof GameSettings]);
    });
    this.updateDistanceLimit(); this.renderBudget();
    this.updateOutputs(); this.renderLayoutHandles();
    this.renderKeys();
  }
  private slider(key: string, label: string, min: number, max: number, step: number, suffix = '', multiplier = 1): string {
    return `<label class="setting-row slider-row"><span>${t(label)} <output data-output="${key}" data-suffix="${suffix}" data-multiplier="${multiplier}"></output></span><input aria-label="${t(label)}" data-extra="${key}" type="range" min="${min}" max="${max}" step="${step}"></label>`;
  }
  private updateOutputs(): void {
    this.root.querySelectorAll<HTMLOutputElement>('[data-output]').forEach(output => {
      const value = Number(this.field<HTMLInputElement>(`[data-extra="${output.dataset.output}"]`).value);
      output.value = `${Number((value * Number(output.dataset.multiplier)).toFixed(2))}${output.dataset.suffix}`;
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
    this.updateDistanceLimit();
    const extra: Record<string, string | number> = {};
    this.root.querySelectorAll<HTMLInputElement | HTMLSelectElement>('[data-extra]').forEach(field => {
      extra[field.dataset.extra!] = field instanceof HTMLInputElement ? field.valueAsNumber : field.value;
    });
    this.draft = { ...this.draft, ...extra, ren_ming: this.field<HTMLInputElement>('[data-default-name]').value,
      fov: this.field<HTMLInputElement>('[data-fov]').valueAsNumber,
      renderDistance: this.field<HTMLInputElement>('[data-render-distance]').valueAsNumber,
      timeSpeed: this.field<HTMLInputElement>('[data-speed]').valueAsNumber,
      touchControls: this.field<HTMLInputElement>('[data-touch]').checked,
      showTopBar: this.field<HTMLInputElement>('[data-top-bar]').checked,
      particles: this.field<HTMLInputElement>('[data-particles]').checked,
      menuAutoPause: this.field<HTMLInputElement>('[data-menu-auto-pause]').checked };
  }
  private renderLayoutHandles(): void {
    this.root.querySelectorAll<HTMLElement>('[data-layout]').forEach(handle => {
      const point = this.draft.touchLayout![handle.dataset.layout as 'move' | 'actions'];
      handle.style.left = `${point.x}%`; handle.style.top = `${point.y}%`;
      handle.style.transform = `translate(-${point.x}%, -${point.y}%)`;
    });
  }
  private bindLayout(signal: AbortSignal): void {
    this.root.querySelectorAll<HTMLElement>('[data-layout]').forEach(handle => {
      let drag: { id: number; x: number; y: number } | null = null;
      handle.addEventListener('pointerdown', event => {
        const rect = handle.getBoundingClientRect();
        drag = { id: event.pointerId, x: event.clientX - rect.left, y: event.clientY - rect.top };
        handle.setPointerCapture(event.pointerId); event.preventDefault();
      }, { signal });
      handle.addEventListener('pointermove', event => {
        if (drag?.id !== event.pointerId) return;
        const area = handle.parentElement!.getBoundingClientRect(), rect = handle.getBoundingClientRect();
        const x = Math.max(0, Math.min(100, (event.clientX - area.left - drag.x) / Math.max(1, area.width - rect.width) * 100));
        const y = Math.max(0, Math.min(100, (event.clientY - area.top - drag.y) / Math.max(1, area.height - rect.height) * 100));
        this.draft.touchLayout = { ...this.draft.touchLayout!, [handle.dataset.layout!]: { x, y } };
        this.renderLayoutHandles();
      }, { signal });
      for (const type of ['pointerup', 'pointercancel', 'lostpointercapture']) handle.addEventListener(type, () => { drag = null; }, { signal });
      handle.addEventListener('keydown', event => {
        if (!['ArrowLeft','ArrowRight','ArrowUp','ArrowDown'].includes(event.code)) return;
        event.preventDefault(); event.stopPropagation();
        const key = handle.dataset.layout as 'move' | 'actions', point = this.draft.touchLayout![key];
        this.draft.touchLayout = { ...this.draft.touchLayout!, [key]: {
          x: Math.max(0, Math.min(100, point.x + (event.code === 'ArrowLeft' ? -2 : event.code === 'ArrowRight' ? 2 : 0))),
          y: Math.max(0, Math.min(100, point.y + (event.code === 'ArrowUp' ? -2 : event.code === 'ArrowDown' ? 2 : 0))) } };
        this.renderLayoutHandles();
      }, { signal });
    });
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
      const fov = this.field<HTMLInputElement>('[data-fov]').value.trim();
      const speed = this.field<HTMLInputElement>('[data-speed]').value.trim();
      const renderDistance = this.field<HTMLInputElement>('[data-render-distance]').valueAsNumber;
      const value = validateSettings({ ...this.draft, fov: fov ? Number(fov) : NaN, timeSpeed: speed ? Number(speed) : NaN,
        renderDistance,
        viewMode: this.draft.viewMode, touchControls: this.field<HTMLInputElement>('[data-touch]').checked,
        showTopBar: this.field<HTMLInputElement>('[data-top-bar]').checked,
      particles: this.field<HTMLInputElement>('[data-particles]').checked,
      menuAutoPause: this.field<HTMLInputElement>('[data-menu-auto-pause]').checked });
      const guarded: (keyof GameSettings)[] = ['font', 'textScale', 'safeX', 'safeY', 'uiScale', 'resolution', 'renderDistance', 'vegetationDistance'];
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
  destroy(): void { this.confirmation?.cancel(); this.stopLocaleWatch(); this.events.abort(); }
}
