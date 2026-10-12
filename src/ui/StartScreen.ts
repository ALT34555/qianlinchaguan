import { parsePlayMode, type PlayMode } from '../core/PlayMode';
import { MAX_ARCHIVE_BYTES } from '../systems/world/SaveArchive';
import teacupUrl from '../../content/assets/ui/icons/qianlin_teacup.png?url';
import { randomSeed } from '../core/math/Random';
import { renName } from '../entities/PlayerIdentity';
import { type ClimateWeights, type WorldGeneration } from '../systems/world/WorldSettings';
import { downloadWorldSave, type LocalSave, type SaveStore } from '../systems/world/LocalSaveStore';
import type { GameSettings } from '../core/GameSettings';
import { WorldCreator, type StartOptions } from './WorldCreator';
import { SettingsPanel } from './SettingsPanel';
import { dayTimeFromUnixMs, jdnToGregorian } from '../systems/calendar/JulianDay';
import { yuanCalendar } from '../systems/calendar/YuanCalendar';
import { calendarLabel } from '../i18n/content';
import { formatDateTime, onLocaleChange, t, tp } from '../i18n';
export type { StartOptions } from './WorldCreator';

interface MenuServices {
  store: SaveStore;
  getSettings: () => GameSettings;
  applySettings: (settings: GameSettings, persist?: boolean) => void;
}
export class StartScreen {
  private creator: WorldCreator | null = null;
  private settings: SettingsPanel | null = null;
  private events = new AbortController();
  private readonly saveModes: Record<PlayMode, boolean> = { survival: true, leisure: true };
  /** 当前页面的重绘闭包，用于切换语言后就地刷新。 */
  private rerender: (() => void) | null = null;
  private readonly stopLocaleWatch: () => void;
  constructor(private readonly root: HTMLElement, _seed: string, private readonly weights: ClimateWeights,
    private readonly enter: (options: StartOptions) => void, private readonly generation: WorldGeneration,
    private readonly services: MenuServices) {
    this.stopLocaleWatch = onLocaleChange(this.onLocaleChange);
    this.home();
  }
  /** 语言变更后就地重绘 */
  private onLocaleChange = (): void => {
    if (this.settings) return;
    this.rerender?.();
  };
  private clear(): void {
    this.creator?.destroy(); this.creator = null;
    this.settings?.destroy(); this.settings = null;
    this.events.abort(); this.events = new AbortController();
    this.root.classList.remove('creator-page'); this.root.classList.add('landing-page'); this.root.scrollTop = 0;
  }
  /** 顶栏只保留工程名 */
  private header(): string {
    return `<header class="menu-header"><a class="wordmark" href="./">${t('app.name')}</a></header>`;
  }
  private on(selector: string, fn: () => void): void {
    this.root.querySelector(selector)!.addEventListener('click', fn, { signal: this.events.signal });
  }
  home(): void {
    this.clear();
    this.rerender = () => this.home();
    this.root.innerHTML = `<section class="simple-home"><span class="home-player-name" aria-hidden="true"></span><img class="home-teacup" src="${teacupUrl}" alt="" draggable="false"><h1>${t('app.name')}</h1><nav aria-label="${t('menu.navAria')}"><button data-create>${t('menu.create')}</button><button data-read>${t('menu.read')}</button><button data-settings>${t('menu.settings')}</button></nav><p id="menu-status" class="menu-status" role="status" aria-live="polite"></p></section>`;
    this.root.querySelector('.home-player-name')!.textContent = renName(this.services.getSettings().ren_ming);
    this.on('[data-create]', () => this.createChoices()); this.on('[data-read]', () => this.read()); this.on('[data-settings]', () => this.preferences());
  }
  private createChoices(): void {
    this.clear();
    this.rerender = () => this.createChoices();
    this.root.innerHTML = `${this.header()}<section class="menu-page"><div class="panel-heading"><h1>${t('create.title')}</h1><button class="menu-back" data-back>${t('create.back')}</button></div><p class="panel-intro">${t('mode.choose')}</p><div class="creation-options play-mode-options">
      ${(['survival', 'leisure'] as const).map(mode => `<button class="creation-card" data-play-mode="${mode}"><span class="creation-symbol">${mode === 'survival' ? '山' : '雲'}</span><strong>${t(`mode.${mode}`)}</strong><p>${t(`mode.${mode}.desc`)}</p></button>`).join('')}</div></section>`;
    this.on('[data-back]', () => this.home());
    this.root.querySelectorAll<HTMLButtonElement>('[data-play-mode]').forEach(button => this.on(`[data-play-mode="${button.dataset.playMode}"]`, () => this.create(button.dataset.playMode as PlayMode)));
  }
  private create(mode: PlayMode, seed = String(randomSeed())): void {
    this.clear(); this.root.classList.remove('landing-page'); this.root.classList.add('creator-page');
    this.rerender = () => this.create(mode, seed);
    this.creator = new WorldCreator(this.root, seed, this.weights, this.enter, this.generation, () => this.createChoices(), renName(this.services.getSettings().ren_ming), mode);
  }
  private preferences(): void {
    this.clear(); this.rerender = () => this.preferences();
    this.root.innerHTML = `${this.header()}<section class="menu-page preferences-page"></section>`;
    this.settings = new SettingsPanel(this.root.querySelector('.menu-page')!, this.services.getSettings(), this.services.applySettings, () => this.home());
  }
  private async read(message = ''): Promise<void> {
    this.clear();
    this.rerender = () => { void this.read(); };
    this.root.innerHTML = `${this.header()}<section class="menu-page"><div class="panel-heading"><h1>${t('saves.title')}</h1><button class="menu-back" data-back>${t('saves.back')}</button></div><div class="saves-intro"><p class="panel-intro" data-save-location></p><button class="primary" data-import>${t('saves.import')}</button><input data-import-file type="file" accept=".sav,application/zip" class="hidden"></div><fieldset class="save-filters"><legend>${t('saves.filterModes')}</legend>${(['survival', 'leisure'] as const).map(mode => `<label><input type="checkbox" data-save-mode="${mode}" ${this.saveModes[mode] ? 'checked' : ''}><span>${t(`mode.${mode}`)}</span></label>`).join('')}</fieldset><div class="save-list">${t('saves.loading')}</div><p id="menu-status" class="menu-status" role="status" aria-live="polite"></p></section>`;
    this.root.querySelector('[data-save-location]')!.textContent = t('saves.location', { path: this.services.store.locationLabel });
    this.on('[data-back]', () => this.home());
    const status = this.root.querySelector<HTMLElement>('#menu-status')!; status.textContent = message;
    const signal = this.events.signal;
    const input = this.root.querySelector<HTMLInputElement>('[data-import-file]')!;
    const importButton = this.root.querySelector<HTMLButtonElement>('[data-import]')!;
    this.on('[data-import]', () => input.click());
    input.addEventListener('change', async () => {
      const file = input.files?.[0]; if (!file) return;
      importButton.disabled = true; status.textContent = t('saves.importing');
      const signal = this.events.signal;
      try {
        if (file.size > MAX_ARCHIVE_BYTES) throw new Error(t('saves.importTooLarge'));
        const bytes = new Uint8Array(await file.arrayBuffer()); if (signal.aborted) return;
        const item = await this.services.store.import(bytes, file.name.replace(/\.sav$/i, ''));
        if (!signal.aborted) void this.read(t('saves.imported', { name: item.name }));
      } catch (error) { if (!signal.aborted) status.textContent = error instanceof Error ? error.message : t('saves.importFailed'); }
      input.value = '';
      importButton.disabled = false;
    }, { signal: this.events.signal });
    try {
      const { saves, unreadable, message: migration } = await this.services.store.list();
      if (signal.aborted) return;
      status.textContent = [message, migration, unreadable ? tp('saves.unreadable', unreadable) : ''].filter(Boolean).join(' ');
      const list = this.root.querySelector('.save-list')!;
      const renderSaves = () => {
        list.replaceChildren();
        const visible = saves.filter(item => this.saveModes[parsePlayMode(item.save.playMode)]);
        if (!saves.length) list.innerHTML = `<div class="empty-saves"><span>${t('saves.emptyTitle')}</span><p>${t('saves.emptyText')}</p></div>`;
        else if (!visible.length) {
          const empty = document.createElement('p'); empty.className = 'empty-saves'; empty.textContent = t('saves.noMatchingModes'); list.append(empty);
        }
        visible.forEach(item => list.appendChild(this.saveCard(item, status)));
      };
      this.root.querySelectorAll<HTMLInputElement>('[data-save-mode]').forEach(input => {
        input.addEventListener('change', () => { this.saveModes[input.dataset.saveMode as PlayMode] = input.checked; renderSaves(); }, { signal });
      });
      renderSaves();
    } catch (error) {
      if (!signal.aborted) { this.root.querySelector('.save-list')!.textContent = t('saves.listFailed'); status.textContent = error instanceof Error ? error.message : t('saves.accessFailed'); }
    }
  }
  private saveCard(item: LocalSave, status: HTMLElement): HTMLElement {
    const card = document.createElement('article'); card.className = 'save-card';
    const info = document.createElement('div'); info.className = 'save-info';
    const title = document.createElement('h2'); title.textContent = item.name;
    const details = document.createElement('p'); const generation = item.save.generation;
    const mode = generation.mode === 'plane' ? t('saves.mode.plane') : generation.planet.map === 'earth' ? t('saves.mode.earth') : t('saves.mode.planet');
    const time = dayTimeFromUnixMs(item.save.unixMs ?? Date.now(), item.save.utcOffsetMinutes ?? 480);
    const date = item.save.calendarType === 'yuan' ? yuanCalendar.fromDayTime(time) : jdnToGregorian(time.jdn);
    details.textContent = t('saves.meta', { mode, seed: item.save.seed, calendar: calendarLabel(item.save.calendarType === 'yuan' ? 'yuan' : 'real'), year: date.year, month: date.month, day: date.day });
    const updated = document.createElement('small'); updated.textContent = t('saves.savedAt', { time: formatDateTime(item.updatedAt) });
    const badge = document.createElement('span'); badge.className = 'save-mode';
    badge.textContent = t(`mode.${parsePlayMode(item.save.playMode)}`);
    info.append(title, badge, details, updated);
    const actions = document.createElement('div'); actions.className = 'save-actions';
    const read = document.createElement('button'); read.className = 'primary'; read.textContent = t('saves.read');
    read.addEventListener('click', () => {
      try { this.enter({ ...item.save, worldName: item.name, save: item.save, saveId: item.id }); }
      catch (error) { status.textContent = error instanceof Error ? error.message : t('saves.readFailed'); }
    }, { signal: this.events.signal });
    const download = document.createElement('button'); download.className = 'secondary'; download.textContent = t('saves.export');
    download.addEventListener('click', () => {void downloadWorldSave(item.save,item.name,item.id).catch(error=>{status.textContent=String(error);});}, { signal: this.events.signal });
    actions.append(read, download);
    if (parsePlayMode(item.save.playMode) === 'survival') {
      const convert = document.createElement('button'); convert.className = 'secondary'; convert.textContent = t('saves.convertLeisure');
      convert.addEventListener('click', async () => {
        convert.disabled = true;
        const signal = this.events.signal;
        try {
          await this.services.store.put({ ...item.save, playMode: 'leisure' }, item.name, item.id);
          if (!signal.aborted) void this.read(t('saves.convertedLeisure', { name: item.name }));
        } catch (error) {
          if (!signal.aborted) { status.textContent = error instanceof Error ? error.message : t('saves.convertFailed'); convert.disabled = false; }
        }
      }, { signal: this.events.signal });
      actions.append(convert);
    }
    card.append(info, actions); return card;
  }
  destroy(): void { this.stopLocaleWatch(); this.rerender = null; this.clear(); }
}
