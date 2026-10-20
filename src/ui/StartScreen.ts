import { parsePlayMode, type PlayMode } from '../core/PlayMode';
import { MAX_ARCHIVE_BYTES } from '../systems/world/SaveArchive';
import teacupUrl from '../../content/assets/ui/icons/qianlin_teacup.png?url';
import navHomeSvg from '../../content/assets/ui/icons/nav-home.svg?raw';
import navStorySvg from '../../content/assets/ui/icons/nav-story.svg?raw';
import navSettingsSvg from '../../content/assets/ui/icons/nav-settings.svg?raw';
import { randomSeed } from '../core/math/Random';
import { renName } from '../entities/PlayerIdentity';
import { DEFAULT_PLAYER_FACTION } from '../systems/world/artificial/PlayerFaction';
import { type ClimateWeights, type WorldGeneration } from '../systems/world/WorldSettings';
import { downloadWorldSave, type LocalSave, type SaveStore } from '../systems/world/LocalSaveStore';
import type { GameSettings } from '../core/GameSettings';
import { WorldCreator, type StartOptions } from './WorldCreator';
import { SettingsPanel } from './SettingsPanel';
import { dayTimeFromUnixMs, jdnToGregorian } from '../systems/calendar/JulianDay';
import { yuanCalendar } from '../systems/calendar/YuanCalendar';
import { calendarLabel } from '../i18n/content';
import { formatDateTime, onLocaleChange, t, tp } from '../i18n';
import { paintAll, resolvedTheme, toggleTheme } from './adapt/runtime';
import { startPetals } from './adapt/petals';
import { fitBookmarks } from './adapt/bookmarks';
export type { StartOptions } from './WorldCreator';

interface MenuServices {
  store: SaveStore;
  getSettings: () => GameSettings;
  applySettings: (settings: GameSettings, persist?: boolean) => void;
}

type TabKey = 'home' | 'create' | 'saves' | 'settings';

const navIcon = (source: string): string => source
  .replace(/^<\?xml[^>]*\?>\s*/, '')
  .replace('<svg ', '<svg class="q-tab-icon" aria-hidden="true" focusable="false" ');

const TABS: ReadonlyArray<[TabKey, string, string]> = [
  ['home', 'nav.home', navIcon(navHomeSvg)],
  ['saves', 'nav.saves', navIcon(navStorySvg)],
  ['settings', 'nav.settings', navIcon(navSettingsSvg)],
];

const SUN_GLYPH = '<svg class="tp-sun" viewBox="0 0 24 24" aria-hidden="true"><circle cx="12" cy="12" r="4.1"/><path d="M12 2.8v2M12 19.2v2M2.8 12h2M19.2 12h2M5.5 5.5l1.4 1.4M17.1 17.1l1.4 1.4M18.5 5.5l-1.4 1.4M6.9 17.1l-1.4 1.4"/></svg>';
const MOON_GLYPH = '<svg class="tp-moon" viewBox="0 0 24 24" aria-hidden="true"><path d="M20 14.4A8.4 8.4 0 0 1 9.6 4a8.4 8.4 0 1 0 10.4 10.4Z"/></svg>';

export class StartScreen {
  private creator: WorldCreator | null = null;
  private settings: SettingsPanel | null = null;
  private events = new AbortController();
  private readonly saveModes: Record<PlayMode, boolean> = { survival: true, leisure: true };
  private railCollapsed = false;
  /** 当前页面的重绘闭包，用于切换语言后就地刷新。 */
  private rerender: (() => void) | null = null;
  private readonly stopLocaleWatch: () => void;
  private stopPetals: () => void = () => {};
  private stopBookmarks: () => void = () => {};
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
    this.stopBookmarks(); this.stopBookmarks = () => {};
    this.stopPetals(); this.stopPetals = () => {};
    this.creator?.destroy(); this.creator = null;
    this.settings?.destroy(); this.settings = null;
    this.events.abort(); this.events = new AbortController();
    this.root.classList.remove('creator-page'); this.root.classList.add('landing-page'); this.root.scrollTop = 0;
  }
  /** 茶席外壳：左侧书签栏 + 一张卷面 */
  private chrome(active: TabKey, body: string): string {
    const dark = resolvedTheme() === 'dark';
    return `<div class="q-shell${this.railCollapsed ? ' is-rail-collapsed' : ''}"><nav class="q-rail" aria-label="${t('menu.navAria')}"><a class="q-rail-brand" href="#" data-home aria-label="${t('app.name')}" aria-expanded="${!this.railCollapsed}"><img class="q-rail-logo" src="${teacupUrl}" alt="" draggable="false"><span class="q-rail-word">${t('app.name')}</span></a><div class="q-rail-nav">${TABS.map(([key, label, graphic]) => `<button class="q-tab${key === active ? ' is-active' : ''}" data-tab="${key}" aria-label="${t(label)}">${graphic}<span class="q-tab-label">${t(label)}</span></button>`).join('')}</div><div class="q-rail-acts"><button class="theme-pill tip" data-theme-toggle data-tip="${t('settings.theme')}" aria-label="${t('settings.theme')}" aria-pressed="${dark}"><span class="tp-knob">${SUN_GLYPH}${MOON_GLYPH}</span></button></div><p class="q-rail-foot">${t('nav.foot')}</p></nav><main class="q-sheet"><div class="q-sheet-mark" data-decor="flower" aria-hidden="true"></div>${body}</main></div>`;
  }
  /** 外壳事件：书签页签、品牌回封面、日夜开关 */
  private bindChrome(): void {
    const signal = this.events.signal;
    const go = (key: TabKey) => {
      if (key === 'home') this.home();
      else if (key === 'create') this.createChoices();
      else if (key === 'saves') void this.read();
      else this.preferences();
    };
    this.root.querySelectorAll<HTMLElement>('[data-tab]').forEach(tab => {
      tab.addEventListener('click', () => go(tab.dataset.tab as TabKey), { signal });
    });
    this.root.querySelector('[data-home]')!.addEventListener('click', event => {
      event.preventDefault();
      this.railCollapsed = !this.railCollapsed;
      this.root.querySelector('.q-shell')?.classList.toggle('is-rail-collapsed', this.railCollapsed);
      event.currentTarget instanceof HTMLElement && event.currentTarget.setAttribute('aria-expanded', String(!this.railCollapsed));
    }, { signal });
    this.root.querySelector('[data-theme-toggle]')!.addEventListener('click', event => {
      const rect = (event.currentTarget as HTMLElement).getBoundingClientRect();
      toggleTheme(rect.left + rect.width / 2, rect.top + rect.height / 2);
      this.rerender?.();
    }, { signal });
    const page = this.root.querySelector('.q-page') ?? this.root;
    paintAll(page);
  }
  private on(selector: string, fn: () => void): void {
    this.root.querySelector(selector)?.addEventListener('click', fn, { signal: this.events.signal });
  }
  home(): void {
    this.clear();
    this.rerender = () => this.home();
    const player = renName(this.services.getSettings().ren_ming);
    this.root.innerHTML = this.chrome('home', `<section class="q-page is-active" data-page="home">
      <div class="q-hero">
        <div class="q-hero-copy">
          <h1>${t('home.title1')}<br>${t('home.title2')}<span class="hl">${t('home.titleHi')}</span>${t('home.title3')}</h1>
          <p class="lead">${t('home.lead')}</p>
          <div class="q-hero-actions">
            <button class="btn btn--primary" data-create>${t('menu.create')}</button>
            <button class="btn btn--ghost" data-read>${t('menu.read')}</button>
            <button class="btn btn--quiet" data-settings>${t('menu.settings')}</button>
          </div>
        </div>
        <div class="q-cup-stage">
          <div class="q-cup-halo"></div>
          <div class="q-steam" data-decor="steam" aria-hidden="true"></div>
          <img class="q-cup-img" src="${teacupUrl}" alt="" draggable="false">
          <canvas class="q-petal-canvas" aria-hidden="true"></canvas>
        </div>
      </div>
      <div class="q-hero-meta">
        <div><strong class="q-home-player">${player}</strong><span>${t('profile.name')}</span></div>
        <div><strong data-home-count>—</strong><span>${t('home.metaSaves')}</span></div>
      </div>
      <div class="sec-head"><h2 class="sec-title"><span data-decor="sprig"></span>${t('home.continue')}</h2><button class="btn btn--quiet btn--sm" data-read>${t('home.allSaves')}</button></div>
      <div class="q-save-list" data-home-saves><p class="q-status">${t('saves.loading')}</p></div>
      <p id="menu-status" class="q-status" role="status" aria-live="polite"></p>
    </section>`);
    this.bindChrome();
    const canvas = this.root.querySelector<HTMLCanvasElement>('.q-petal-canvas');
    if (canvas) this.stopPetals = startPetals(canvas);
    this.on('[data-create]', () => this.createChoices());
    this.root.querySelectorAll('[data-read]').forEach(btn => btn.addEventListener('click', () => void this.read(), { signal: this.events.signal }));
    this.on('[data-settings]', () => this.preferences());
    void this.fillContinue();
  }
  /** 首页继续区：列最近一个存档 */
  private async fillContinue(): Promise<void> {
    const host = this.root.querySelector<HTMLElement>('[data-home-saves]');
    if (!host) return;
    const signal = this.events.signal;
    try {
      const { saves } = await this.services.store.list();
      if (signal.aborted || !host.isConnected) return;
      const count = this.root.querySelector('[data-home-count]');
      if (count) count.textContent = String(saves.length);
      if (!saves.length) {
        host.innerHTML = `<div class="empty"><span data-decor="cloud"></span><p>${t('home.noSaves')}</p><p class="muted">${t('home.noSavesHint')}</p></div>`;
        paintAll(host);
        return;
      }
      host.replaceChildren(this.compactSave(saves[0]));
      paintAll(host);
    } catch {
      if (!signal.aborted) host.innerHTML = `<p class="q-status">${t('saves.listFailed')}</p>`;
    }
  }
  private compactSave(item: LocalSave): HTMLElement {
    const card = document.createElement('article'); card.className = 'q-save';
    const line = document.createElement('div'); line.className = 'q-save-line';
    const name = document.createElement('span'); name.className = 'q-save-name'; name.textContent = item.name;
    const badge = document.createElement('span'); badge.className = 'q-tag q-tag--primary';
    badge.textContent = t(`mode.${parsePlayMode(item.save.playMode)}`);
    const player = document.createElement('span'); player.className = 'save-player-name';
    player.textContent = renName(item.save.playerState?.name ?? item.save.ren_ming);
    player.style.color = item.save.profile?.mainColor ?? item.save.playerFaction?.mainColor ?? DEFAULT_PLAYER_FACTION.mainColor;
    line.append(name, badge, player);
    const acts = document.createElement('div'); acts.className = 'q-save-acts';
    const read = document.createElement('button'); read.className = 'btn btn--primary btn--sm'; read.textContent = t('saves.read');
    read.addEventListener('click', () => {
      try { this.enter({ ...item.save, worldName: item.name, save: item.save, saveId: item.id }); }
      catch (error) { this.root.querySelector<HTMLElement>('#menu-status')!.textContent = error instanceof Error ? error.message : t('saves.readFailed'); }
    }, { signal: this.events.signal });
    acts.append(read);
    const meta = document.createElement('div'); meta.className = 'q-save-meta';
    const time = dayTimeFromUnixMs(item.save.unixMs ?? Date.now(), item.save.utcOffsetMinutes ?? 480);
    const date = item.save.calendarType === 'yuan' ? yuanCalendar.fromDayTime(time) : jdnToGregorian(time.jdn);
    meta.textContent = t('saves.meta', { mode: t('creator.mode.planetMacro'), seed: item.save.seed, calendar: calendarLabel(item.save.calendarType === 'yuan' ? 'yuan' : 'real'), year: date.year, month: date.month, day: date.day });
    card.append(line, acts, meta);
    return card;
  }
  private createChoices(): void {
    this.clear();
    this.rerender = () => this.createChoices();
    this.root.innerHTML = this.chrome('create', `<section class="q-page is-active" data-page="create">
      <div class="q-page-head"><h1><span data-decor="seal"></span>${t('create.title')}</h1><span class="q-page-desc">${t('mode.choose')}</span></div>
      <div class="grid g2">${(['survival', 'leisure'] as const).map(mode => `<button class="card q-mode" data-play-mode="${mode}"><span class="q-mode-mark" data-decor="${mode === 'survival' ? 'arc' : 'wave'}"></span><strong>${t(`mode.${mode}`)}</strong><p>${t(`mode.${mode}.desc`)}</p></button>`).join('')}</div>
    </section>`);
    this.bindChrome();
    this.root.querySelectorAll<HTMLButtonElement>('[data-play-mode]').forEach(button => this.on(`[data-play-mode="${button.dataset.playMode}"]`, () => this.create(button.dataset.playMode as PlayMode)));
  }
  private create(mode: PlayMode, seed = String(randomSeed())): void {
    this.clear(); this.root.classList.remove('landing-page'); this.root.classList.add('creator-page');
    this.rerender = () => this.create(mode, seed);
    this.root.innerHTML = this.chrome('create', `<section class="q-page is-active creator-page" data-page="create"></section>`);
    this.bindChrome();
    const page = this.root.querySelector<HTMLElement>('.q-page')!;
    this.creator = new WorldCreator(page, seed, this.weights, this.enter, this.generation, () => this.createChoices(), renName(this.services.getSettings().ren_ming), mode, this.services.getSettings().profile);
    paintAll(this.root);
  }
  private preferences(): void {
    this.clear(); this.rerender = () => this.preferences();
    this.root.innerHTML = this.chrome('settings', `<section class="q-page is-active preferences-page" data-page="settings"></section>`);
    this.bindChrome();
    this.settings = new SettingsPanel(this.root.querySelector('.q-page')!, this.services.getSettings(), this.services.applySettings, () => this.home(), { headingBack: false });
  }
  private async read(message = ''): Promise<void> {
    this.clear();
    this.rerender = () => { void this.read(); };
    this.root.innerHTML = this.chrome('saves', `<section class="q-page is-active saves-page" data-page="saves"><div class="q-page-head"><h1><span data-decor="seal"></span>${t('saves.title')}</h1><span class="q-page-desc">${t('saves.filterModes')}</span></div><input data-import-file type="file" accept=".sav,application/zip" class="hidden"><div class="saves-bookmark-panel"><fieldset class="save-filters"><legend>${t('saves.filterModes')}</legend><label class="save-filter-bookmark"><input type="checkbox" data-save-all ${this.saveModes.survival && this.saveModes.leisure ? 'checked' : ''}><span>${t('saves.allModes')}</span></label>${(['leisure', 'survival'] as const).map(mode => `<label class="save-filter-bookmark"><input type="checkbox" data-save-mode="${mode}" ${this.saveModes[mode] ? 'checked' : ''}><span>${t(`mode.${mode}`)}</span></label>`).join('')}<button type="button" class="save-filter-bookmark save-import-bookmark" data-import><span>${t('saves.import')}</span></button></fieldset><div class="save-list-panel"><div class="q-save-list">${t('saves.loading')}</div></div></div><footer class="saves-footer"><p id="menu-status" class="q-status" role="status" aria-live="polite"></p><p class="save-location" data-save-location></p></footer></section>`);
    this.bindChrome();
    this.stopBookmarks = fitBookmarks(this.root.querySelector<HTMLElement>('.saves-bookmark-panel')!);
    this.root.querySelector('[data-save-location]')!.textContent = t('saves.directory', { path: this.services.store.locationLabel });
    const status = this.root.querySelector<HTMLElement>('#menu-status')!; status.textContent = message;
    const signal = this.events.signal;
    const input = this.root.querySelector<HTMLInputElement>('[data-import-file]')!;
    const importButton = this.root.querySelector<HTMLButtonElement>('[data-import]')!;
    const importState = (state: 'idle' | 'choosing' | 'importing') => {
      importButton.dataset.importState = state;
      importButton.setAttribute('aria-busy', String(state === 'importing'));
    };
    importState('idle');
    this.on('[data-import]', () => { importState('choosing'); input.click(); });
    input.addEventListener('cancel', () => importState('idle'), { signal });
    input.addEventListener('change', async () => {
      const file = input.files?.[0]; if (!file) { importState('idle'); return; }
      importState('importing');
      importButton.disabled = true; status.textContent = t('saves.importing');
      const signal = this.events.signal;
      try {
        if (file.size > MAX_ARCHIVE_BYTES) throw new Error(t('saves.importTooLarge'));
        const bytes = new Uint8Array(await file.arrayBuffer()); if (signal.aborted) return;
        const item = await this.services.store.import(bytes, file.name.replace(/\.sav$/i, ''));
        if (!signal.aborted) void this.read(t('saves.imported', { name: item.name }));
      } catch (error) { if (!signal.aborted) status.textContent = error instanceof Error ? error.message : t('saves.importFailed'); }
      finally {
        input.value = '';
        importButton.disabled = false;
        importState('idle');
      }
    }, { signal: this.events.signal });
    try {
      const { saves, unreadable, message: migration } = await this.services.store.list();
      if (signal.aborted) return;
      status.textContent = [message, migration, unreadable ? tp('saves.unreadable', unreadable) : ''].filter(Boolean).join(' ');
      const list = this.root.querySelector('.q-save-list')!;
      const all = this.root.querySelector<HTMLInputElement>('[data-save-all]')!;
      const modeInputs = this.root.querySelectorAll<HTMLInputElement>('[data-save-mode]');
      const renderSaves = () => {
        all.checked = this.saveModes.survival && this.saveModes.leisure;
        all.indeterminate = this.saveModes.survival !== this.saveModes.leisure;
        modeInputs.forEach(input => { input.checked = this.saveModes[input.dataset.saveMode as PlayMode]; });
        list.replaceChildren();
        const visible = saves.filter(item => this.saveModes[parsePlayMode(item.save.playMode)]);
        if (!saves.length) list.innerHTML = `<div class="empty"><span data-decor="cloud"></span><p>${t('saves.emptyTitle')}</p><p class="muted">${t('saves.emptyText')}</p></div>`;
        else if (!visible.length) {
          const empty = document.createElement('p'); empty.className = 'empty-saves'; empty.textContent = t('saves.noMatchingModes'); list.append(empty);
        }
        visible.forEach(item => list.appendChild(this.saveCard(item, status)));
        paintAll(list);
      };
      all.addEventListener('change', () => {
        this.saveModes.survival = all.checked; this.saveModes.leisure = all.checked; renderSaves();
      }, { signal });
      modeInputs.forEach(input => {
        input.addEventListener('change', () => { this.saveModes[input.dataset.saveMode as PlayMode] = input.checked; renderSaves(); }, { signal });
      });
      renderSaves();
    } catch (error) {
      if (!signal.aborted) { this.root.querySelector('.q-save-list')!.textContent = t('saves.listFailed'); status.textContent = error instanceof Error ? error.message : t('saves.accessFailed'); }
    }
  }
  private saveCard(item: LocalSave, status: HTMLElement): HTMLElement {
    const card = document.createElement('article'); card.className = 'q-save';
    const line = document.createElement('div'); line.className = 'q-save-line';
    const title = document.createElement('span'); title.className = 'q-save-name'; title.textContent = item.name;
    const badge = document.createElement('span'); badge.className = `q-tag ${parsePlayMode(item.save.playMode) === 'survival' ? 'q-tag--primary' : 'q-tag--tea'}`;
    badge.textContent = t(`mode.${parsePlayMode(item.save.playMode)}`);
    const player = document.createElement('span'); player.className = 'save-player-name';
    player.textContent = renName(item.save.playerState?.name ?? item.save.ren_ming);
    player.style.color = item.save.profile?.mainColor ?? item.save.playerFaction?.mainColor ?? DEFAULT_PLAYER_FACTION.mainColor;
    line.append(title, badge, player);
    const mode = t('creator.mode.planetMacro');
    const time = dayTimeFromUnixMs(item.save.unixMs ?? Date.now(), item.save.utcOffsetMinutes ?? 480);
    const date = item.save.calendarType === 'yuan' ? yuanCalendar.fromDayTime(time) : jdnToGregorian(time.jdn);
    const meta = document.createElement('div'); meta.className = 'q-save-meta';
    meta.textContent = `${t('saves.meta', { mode, seed: item.save.seed, calendar: calendarLabel(item.save.calendarType === 'yuan' ? 'yuan' : 'real'), year: date.year, month: date.month, day: date.day })} · ${t('saves.savedAt', { time: formatDateTime(item.updatedAt) })}`;
    const actions = document.createElement('div'); actions.className = 'q-save-acts';
    const read = document.createElement('button'); read.className = 'btn btn--primary btn--sm'; read.textContent = t('saves.read');
    read.addEventListener('click', () => {
      try { this.enter({ ...item.save, worldName: item.name, save: item.save, saveId: item.id }); }
      catch (error) { status.textContent = error instanceof Error ? error.message : t('saves.readFailed'); }
    }, { signal: this.events.signal });
    const download = document.createElement('button'); download.className = 'btn btn--ghost btn--sm'; download.textContent = t('saves.export');
    download.addEventListener('click', () => {void downloadWorldSave(item.save,item.name,item.id).catch(error=>{status.textContent=String(error);});}, { signal: this.events.signal });
    const rename = document.createElement('button');rename.className='btn btn--ghost btn--sm';rename.textContent=t('saves.rename');
    const form=document.createElement('form');form.className='q-save-rename hidden';
    const input=document.createElement('input');input.type='text';input.maxLength=80;input.required=true;input.value=item.name;input.setAttribute('aria-label',t('saves.newName'));
    const confirm=document.createElement('button');confirm.type='submit';confirm.className='btn btn--primary btn--sm';confirm.textContent=t('saves.renameConfirm');
    const cancel=document.createElement('button');cancel.type='button';cancel.className='btn btn--ghost btn--sm';cancel.textContent=t('saves.renameCancel');
    form.append(input,confirm,cancel);card.append(form);
    rename.addEventListener('click',()=>{form.classList.remove('hidden');input.value=item.name;input.focus();input.select();},{signal:this.events.signal});
    const closeRename=()=>{form.classList.add('hidden');rename.focus();};
    cancel.addEventListener('click',closeRename,{signal:this.events.signal});
    input.addEventListener('keydown',event=>{if(event.key==='Escape'){event.preventDefault();closeRename();}},{signal:this.events.signal});
    form.addEventListener('submit',async event=>{
      event.preventDefault();const name=input.value.trim();
      if(!name){status.textContent=t('saves.nameRequired');input.focus();return;}
      const signal=this.events.signal;confirm.disabled=true;cancel.disabled=true;rename.disabled=true;input.disabled=true;
      try{
        const renamed=await this.services.store.rename(item.id,name);
        if(!signal.aborted)void this.read(t('saves.renamed',{name:renamed.name}));
      }catch(error){
        if(!signal.aborted){status.textContent=error instanceof Error?error.message:t('saves.renameFailed');confirm.disabled=false;cancel.disabled=false;rename.disabled=false;input.disabled=false;}
      }
    },{signal:this.events.signal});
    actions.append(read, download, rename);
    if (parsePlayMode(item.save.playMode) === 'survival') {
      const convert = document.createElement('button'); convert.className = 'btn btn--ghost btn--sm'; convert.textContent = t('saves.convertLeisure');
      convert.addEventListener('click', async () => {
        convert.disabled = true;
        const signal = this.events.signal;
        try {
          const copy=await this.services.store.copyToLeisure(item.id,t('saves.leisureCopyName',{name:item.name.slice(0,60)}));
          if (!signal.aborted) {this.saveModes.leisure=true;void this.read(t('saves.convertedLeisure', { name: copy.name }));}
        } catch (error) {
          if (!signal.aborted) { status.textContent = error instanceof Error ? error.message : t('saves.convertFailed'); convert.disabled = false; }
        }
      }, { signal: this.events.signal });
      actions.append(convert);
    }
    card.append(line, actions, meta); return card;
  }
  destroy(): void { this.stopLocaleWatch(); this.rerender = null; this.clear(); }
}
