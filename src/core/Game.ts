import { defaultMysterySettings } from './MysterySettings';
import { parsePlayerState, type PlayerState } from '../entities/PlayerState';
import { renName } from '../entities/PlayerIdentity';
import { playerFlagSpawn, type PlayerFactionOptions } from '../systems/world/artificial/PlayerFaction';
import { applyAppearance } from '../ui/Appearance';
import * as THREE from 'three';
import { CHUNK_SIZE } from './config';
import { Input } from './Input';
import { TouchControls } from '../ui/TouchControls';
import { Player } from '../entities/Player';
import { World } from '../systems/world/World';
import { Sky } from '../systems/world/Sky';
import { WorldGenerator } from '../systems/world/WorldGenerator';
import { type ClimateWeights, type WorldGeneration } from '../systems/world/WorldSettings';
import { GENERATOR_VERSION } from './version';
import type { PlayerPosition, WorldSave } from '../systems/world/WorldSave';
import { downloadWorldSave, type SaveStore } from '../systems/world/LocalSaveStore';
import { ChatPanel, type ChatMessage } from '../ui/ChatPanel';
import { DebugOverlay } from '../ui/DebugOverlay';
import { AtlasView } from '../ui/AtlasView';
import { SettingsPanel } from '../ui/SettingsPanel';
import { icon, iconUrl } from '../ui/Icons';
import { actionCodes, keyLabel, validateSettings, type GameSettings } from './GameSettings';
import { CalendarSystem, CalendarClock } from '../systems/calendar/CalendarSystem';
import type { CalendarSnapshot } from '../systems/calendar/types';
import { formatChunkId } from '../systems/world/ChunkTypes';
import { calendarLabel, chunkNameById, seasonName } from '../i18n/content';
import { applyDomI18n, onLocaleChange, t } from '../i18n';
import { ArtificialWorld } from '../systems/world/artificial/ArtificialWorld';

export interface GameOptions {
  ren_ming?: string;
  playerFaction?: PlayerFactionOptions;
  seed: number;
  climateWeights: ClimateWeights;
  generation: WorldGeneration;
  calendarType: 'real' | 'yuan';
  unixMs?: number;
  utcOffsetMinutes?: number;
  worldName?: string;
  saveId?: string;
  save?: WorldSave;
  initialPlayer?: PlayerPosition;
  showOverlay: boolean;
  settings: GameSettings;
  saveStore: SaveStore;
  persistSettings: (settings: GameSettings) => void;
}
const SKY_COLOR = new THREE.Color('#a9d3ff');
const UNDERWATER_COLOR = new THREE.Color('#164b60');
const UNDERWATER_NIGHT = new THREE.Color('#081523');

export class Game {
  private renderer: THREE.WebGLRenderer | null = null;
  private readonly scene = new THREE.Scene();
  private readonly camera: THREE.PerspectiveCamera;
  private readonly fog: THREE.Fog;
  private input: Input | null = null;
  private touch: TouchControls | null = null;
  private world: World | null = null;
  private player: Player | null = null;
  private overlay: DebugOverlay | null = null;
  private readonly chat: ChatPanel;
  private gamingInfo = false;
  private sky: Sky | null = null;
  private atlas: AtlasView | null = null;
  private settingsPanel: SettingsPanel | null = null;
  private settings: GameSettings;
  readonly calendarSystem: CalendarSystem;
  readonly calendarClock: CalendarClock;
  readonly calendarType: 'real' | 'yuan';
  currentSeason = 0;
  currentDayRatio = 0;
  currentSnapshot!: CalendarSnapshot;
  private readonly generator: WorldGenerator;
  private readonly artificial: ArtificialWorld;
  private atlasTerritoryRevision = -1;
  private lastTerritoryRender = 0;
  private readonly position: PlayerPosition;
  private readonly mapRoot: HTMLElement;
  private readonly bar: HTMLElement;
  private readonly corner: HTMLElement;
  readonly ren = '人';
  readonly ren_ming: string;
  private readonly playerState: PlayerState;
  private worldName: string;
  private saveId?: string;
  private paused = false;
  private saving = false;
  private menuPage: 'pause' | 'settings' | null = null;
  private spawned = false;
  private previousFrame = 0;
  private lastCalendarMs = -Infinity;
  private fogNear: number;
  private fogFar: number;
  /** 暂停面板存档名草稿 */
  private pauseNameDraft = '';
  private pauseMessage = '';
  private readonly stopLocaleWatch: () => void;

  constructor(private readonly container: HTMLElement,
    private readonly ui: { start: HTMLElement; loading: HTMLElement; waterTint: HTMLElement },
    private readonly options: GameOptions) {
    this.gamingInfo = options.showOverlay;
    this.settings = validateSettings({ ...options.settings, ...(options.save?.mystery ?? defaultMysterySettings()), viewMode: '2d' });
    this.generator = new WorldGenerator(options.seed, options.climateWeights, options.generation);
    this.worldName = options.worldName ?? t('game.defaultWorldName'); this.saveId = options.saveId;
    this.calendarType = options.calendarType;
    this.calendarSystem = new CalendarSystem({ mode: this.calendarType, utcOffsetMinutes: options.utcOffsetMinutes ?? 480 });
    this.calendarClock = new CalendarClock({ epochUnixMs: options.unixMs ?? Date.now(), dayLengthSeconds: 1200 });
    this.artificial = new ArtificialWorld(this.generator, this.calendarClock.unixMs, this.calendarType,
      this.calendarSystem.utcOffsetMinutes, options.save?.artificialState);
    this.ren_ming = renName(options.save?.ren_ming ?? options.ren_ming, options.save ? '赤' : renName(this.settings.ren_ming));
    const spawn = options.initialPlayer ? null : options.playerFaction && this.generator.generation.artificial?.enabled ? playerFlagSpawn(this.artificial, options.playerFaction) : this.generator.findSpawnChunk();
    const owned = this.artificial.playerFactionId ? this.artificial.territories.get(this.artificial.playerFactionId) : undefined;
    this.playerState = parsePlayerState(options.save?.playerState ?? { name: this.ren_ming,
      factions: owned ? [{ id: owned.id, relationship: 'owned', mainColor: owned.mainColor, trimColor: owned.trimColor }] : [],
      ...(owned ? { activeFactionId: owned.id } : {}) });
    const x = spawn ? spawn.cx * CHUNK_SIZE + CHUNK_SIZE / 2 + .5 : options.initialPlayer!.x;
    const z = spawn ? spawn.cz * CHUNK_SIZE + CHUNK_SIZE / 2 + .5 : options.initialPlayer!.z;
    this.position = options.initialPlayer ? { ...options.initialPlayer } : { x, z,
      y: Math.max(this.generator.getHeight(x, z), this.generator.getWaterLevel(x, z)) + .01, yaw: 0, pitch: 0 };
    this.syncClock();
    const distance = this.settings.renderDistance * CHUNK_SIZE;
    this.fogNear = distance * .55; this.fogFar = distance * .95;
    this.fog = new THREE.Fog(SKY_COLOR.clone(), this.fogNear, this.fogFar); this.scene.fog = this.fog;
    this.camera = new THREE.PerspectiveCamera(this.settings.fov, innerWidth / innerHeight, .1, distance * 1.5 + 512);
    this.camera.rotation.order = 'YXZ';
    // 在创建 DOM 和监听器前检查 3D 支持
    if (this.settings.viewMode === '3d') this.ensure3D();
    this.bar = document.createElement('div'); this.bar.className = 'game-bar';
    this.bar.innerHTML = `<div class="game-identity"><strong></strong><span data-ren-name></span><span data-date></span></div>`;
    const playerName = this.bar.querySelector<HTMLElement>('[data-ren-name]')!;
    playerName.textContent = this.ren_ming;
    const faction = this.playerState.factions.find(f => f.id === this.playerState.activeFactionId);
    if (faction) playerName.style.color = faction.mainColor;
    this.chat = new ChatPanel(container, this.ren_ming, faction?.mainColor, value => this.runCommand(value), editing => {
      if (this.input) this.input.enabled = !editing && !this.paused && this.settings.viewMode === '3d'
        && (!!this.settings.touchControls || document.pointerLockElement === this.renderer?.domElement);
    });
    window.addEventListener('keydown', event => {
      if (event.isComposing || event.repeat) return;
      if (event.target instanceof HTMLElement && event.target.closest('input, select, textarea, button, [contenteditable]')) return;
      if (actionCodes('debug', this.settings.keyBindings).includes(event.code)) {
        event.preventDefault(); event.stopImmediatePropagation(); this.setGamingInfo(!this.gamingInfo);
      } else if (!event.altKey && !event.ctrlKey && !event.metaKey && (event.code === 'Enter' || event.code === 'Slash')) {
        event.preventDefault(); event.stopImmediatePropagation();
        if (!this.menuPage) this.showPause();
        this.chat.focus(event.code === 'Slash' ? '/' : '');
      }
    }, { capture: true });
    this.bar.querySelector('strong')!.textContent = this.worldName; container.appendChild(this.bar);
    this.corner = document.createElement('div'); this.corner.className = 'game-corner';
    this.corner.innerHTML = `<button class="menu-back corner-back hidden" data-back>${t('settings.back')}</button><button class="corner-menu" data-menu aria-label="${t('game.menu')}" title="${t('game.menu')}">${icon('menu-open')}</button>`;
    container.appendChild(this.corner);
    this.corner.querySelector('[data-menu]')!.addEventListener('click', () => { if (this.menuPage) this.continueGame(); else this.showPause(); });
    this.corner.querySelector('[data-back]')!.addEventListener('click', () => this.showPause());
    this.mapRoot = document.createElement('section'); this.mapRoot.className = 'game-map hidden';
    container.appendChild(this.mapRoot);
    this.updateCalendar(true); this.setViewMode(); this.syncClock();
    document.addEventListener('keydown', event => {
      if (event.code !== 'Escape' || event.repeat) return;
      event.preventDefault();
      if (this.menuPage === 'settings') this.showPause();
      else if (this.paused) this.continueGame(); else this.showPause();
    });
    document.addEventListener('pointerlockchange', () => {
      if (!this.renderer) return;
      const locked = document.pointerLockElement === this.renderer.domElement;
      if (this.input) this.input.enabled = !this.chat.editing && (locked || !!this.settings.touchControls) && !this.paused && this.settings.viewMode === '3d';
      if (!locked && !this.settings.touchControls && this.settings.viewMode === '3d' && !this.paused) this.showPause();
    });
    window.addEventListener('resize', () => {
      this.camera.aspect = innerWidth / innerHeight; this.camera.updateProjectionMatrix(); this.resizeRenderer();
      if (this.renderer) this.world?.setMistProjection(this.camera.fov, this.renderer.domElement.height, this.renderer.getPixelRatio());
      this.syncTopBar();
    });
    // 语言切换后就地刷新界面文案
    this.stopLocaleWatch = onLocaleChange(() => this.applyLocale());
  }

  /** 语言变更：刷新所有由本类负责的文案。 */
  private applyLocale(): void {
    this.refreshWorldName();
    this.syncTopBar();
    this.syncCorner();
    this.updateDocumentTitle();
    this.updateCalendar(true);
    this.touch?.applyLocale();
    this.overlay?.applyLocale();
    this.overlay?.setKeyLabel(keyLabel(this.settings.keyBindings.debug));
    // 舆图工具条用 data-i18n 标记
    applyDomI18n(this.mapRoot);
    if (this.menuPage === 'pause') this.showPause(this.pauseMessage);
  }

  private syncCorner(): void {
    const open = this.menuPage !== null;
    const toggle = this.corner.querySelector<HTMLButtonElement>('[data-menu]')!;
    toggle.querySelector('img')!.src = iconUrl(open ? 'menu-close' : 'menu-open');
    const label = t(open ? 'game.menu.close' : 'game.menu');
    toggle.setAttribute('aria-label', label); toggle.title = label;
    toggle.setAttribute('aria-expanded', String(open));
    this.chat.setSidebar(open);
    this.bar.classList.toggle('menu-open', open);
    this.syncTopBar();
    this.corner.querySelector('[data-back]')!.classList.toggle('hidden', this.menuPage !== 'settings');
    const backButton = this.corner.querySelector('[data-back]')!;
    backButton.textContent = t('settings.back');
  }

  /** 窗口标题跟着视图走 */
  private updateDocumentTitle(): void {
    document.title = `${t('app.name')} · ${t(this.settings.viewMode === '2d' ? 'view.atlas' : 'view.world')}`;
  }

  /** 顶栏显示的世界名（读档 / 改名后刷新） */
  private refreshWorldName(): void {
    const strong = this.bar.querySelector('strong');
    if (strong) strong.textContent = this.worldName;
  }

  private ensure3D(): void {
    if (this.renderer) return;
    THREE.ColorManagement.enabled = true;
    let renderer: THREE.WebGLRenderer;
    try { renderer = new THREE.WebGLRenderer({ antialias: true, powerPreference: 'high-performance' }); }
    catch { throw new Error(t('game.renderFailed')); }
    renderer.outputColorSpace = THREE.SRGBColorSpace;
    renderer.toneMapping = THREE.ACESFilmicToneMapping;
    renderer.toneMappingExposure = 1.1;
    renderer.domElement.tabIndex = -1;
    renderer.setClearColor(SKY_COLOR);
    this.renderer = renderer; this.resizeRenderer(); this.container.appendChild(renderer.domElement);
    this.input = new Input(renderer.domElement);
    this.touch = new TouchControls(this.input, renderer.domElement); this.input.setBindings(this.settings.keyBindings);
    this.world = new World(this.scene, this.options.seed, this.settings.renderDistance, this.options.climateWeights, this.options.generation, this.artificial);
    this.world.setMistEnabled(this.settings.particles !== false);
    this.world.setVegetationDistance(this.settings.vegetationDistance!);
    this.touch.applySettings(this.settings);
    this.world.setMistProjection(this.camera.fov, renderer.domElement.height, renderer.getPixelRatio());
    this.sky = new Sky(this.scene);
    this.player = new Player(this.world, this.input); this.player.ren_ming = this.ren_ming; Object.assign(this.player, this.position);
    this.player.y = Math.max(this.player.y, this.world.getGroundUnder(this.player.x, this.player.z, .3) + .01);
    this.player.flying = this.options.save?.flying ?? false;
    this.overlay = new DebugOverlay(document.body, this.world, this.player, this);
    this.overlay.setKeyLabel(keyLabel(this.settings.keyBindings.debug));
    this.syncOverlayMapStyle();
    this.input.onAction('debug', () => this.overlay?.toggle());
    renderer.domElement.addEventListener('click', () => { if (!this.paused && this.settings.viewMode === '3d') this.lockPointer(); });
  }
  private runCommand(value: string): string {
    const match = /^\/system\.gaming_info\s+(on|off)$/.exec(value);
    if (!match) return value.startsWith('/system.gaming_info')
      ? 'Usage: /system.gaming_info on|off' : 'Unknown command.';
    const enabled = match[1] === 'on';
    this.setGamingInfo(enabled);
    return 'Gaming info ' + (enabled ? 'enabled.' : 'disabled.');
  }
  addChatMessage(message: ChatMessage): void { this.chat.addMessage(message); }
  private setGamingInfo(enabled: boolean): void {
    if (enabled && !this.overlay) {
      this.ensure3D();
      if (this.settings.viewMode === '2d') this.renderer!.domElement.classList.add('hidden');
    }
    this.gamingInfo = enabled;
    this.overlay?.setVisible(enabled);
    if (enabled) this.overlay?.update(performance.now());
  }
  private lockPointer(): void {
    if (this.settings.touchControls) { if (this.input) this.input.enabled = !this.paused && !this.chat.editing; this.touch?.setVisible(!this.paused); return; }
    this.renderer?.domElement.focus({ preventScroll: true });
    try { void this.input?.requestPointerLock().catch(() => this.showPause(t('game.pointerLockFailed'))); }
    catch { this.showPause(t('game.pointerLockFailed')); }
  }
  private createMap(): void {
    // 工具条文案用 data-i18n 标记
    this.mapRoot.innerHTML = `<div class="game-map-tools"><div class="map-mode-label" data-i18n="game.map.modeLabel"></div><div class="map-buttons">${this.generator.generation.mode === 'planet' ? `<select data-planet-view aria-label="${t('game.map.viewAria')}"><option value="globe" data-i18n="atlas.view.globe"></option><option value="projection" data-i18n="atlas.view.projection"></option></select><button data-overview data-i18n="game.map.overview"></button>` : ''}<button data-enter data-i18n="game.map.enter3d"></button><button data-position data-i18n="game.map.position"></button><button data-zoom-in data-i18n="atlas.zoomInTitle" data-i18n-attr="aria-label">＋</button><button data-zoom-out data-i18n="atlas.zoomOutTitle" data-i18n-attr="aria-label">－</button><label><input data-info type="checkbox"><span data-i18n="game.map.chunkInfo"></span></label><label><input data-colors type="checkbox"><span data-i18n="atlas.toggleChunkColors"></span></label><label><input data-relief type="checkbox"><span data-i18n="game.map.relief"></span></label></div></div><div class="game-map-viewport"><canvas aria-label="${t('game.map.canvasAria')}"></canvas><div class="game-map-tip" data-i18n="game.map.tip"></div></div><footer class="game-map-detail" role="status" data-i18n="game.map.hint"></footer>`;
    applyDomI18n(this.mapRoot);
    this.atlas = new AtlasView({ canvas: this.mapRoot.querySelector('canvas')!, generator: this.generator, artificial: this.artificial,
      onChunkActivate: info => this.enter3D(info.cx, info.cz),
      onChunkSelect: info => {
        this.mapRoot.querySelector('footer')!.textContent = t('game.map.chunkFooter', {
          cx: info.cx, cz: info.cz, code: formatChunkId(info.type), name: chunkNameById(info.type),
          elevation: Math.round(info.elevation), temperature: info.temperature.toFixed(1),
        }) + (info.territory ? ` · ${t('territory.detail', { id: info.territory.name ?? info.territory.id, level: info.territory.level })}` : '');
      } });
    const on = (selector: string, fn: () => void) => this.mapRoot.querySelector(selector)?.addEventListener('click', fn);
    on('[data-enter]', () => this.enter3D());
    on('[data-position]', () => this.centerMapOnPlayer()); on('[data-zoom-in]', () => this.atlas?.zoomBy(1.4)); on('[data-zoom-out]', () => this.atlas?.zoomBy(1 / 1.4));
    const view = this.mapRoot.querySelector<HTMLSelectElement>('[data-planet-view]');
    view?.addEventListener('change', () => this.atlas?.setPlanetView(view.value as 'globe' | 'projection'));
    on('[data-overview]', () => { this.atlas?.setPlanetView(view!.value as 'globe' | 'projection'); this.atlas?.showOverview(); });
    const info = this.mapRoot.querySelector<HTMLInputElement>('[data-info]')!;
    info.addEventListener('change', () => { this.atlas?.setShowChunkInfo(info.checked); this.syncOverlayMapStyle(); });
    const colors=this.mapRoot.querySelector<HTMLInputElement>('[data-colors]')!;
    colors.addEventListener('change',()=>{this.atlas?.setShowChunkColors(colors.checked);this.syncOverlayMapStyle();});
    const relief = this.mapRoot.querySelector<HTMLInputElement>('[data-relief]')!;
    relief.addEventListener('change', () => { this.atlas?.setShowProjection(relief.checked); this.syncOverlayMapStyle(); });
    this.syncOverlayMapStyle();
    if (this.options.initialPlayer || this.artificial.playerFactionId) this.centerMapOnPlayer();
  }
  private syncOverlayMapStyle(): void {
    const checked = (selector: string) => !!this.mapRoot.querySelector<HTMLInputElement>(selector)?.checked;
    this.overlay?.setMapStyle({ info: checked('[data-info]'), colors: checked('[data-colors]'), relief: checked('[data-relief]') });
  }
  private enter3D(cx?: number, cz?: number): void {
    try {
      this.ensure3D();
      if (cx !== undefined && cz !== undefined) {
        const x = cx * CHUNK_SIZE + CHUNK_SIZE / 2 + .5, z = cz * CHUNK_SIZE + CHUNK_SIZE / 2 + .5;
        this.player!.spawnAt(x, z);
        this.player!.y = Math.max(this.generator.getHeight(x, z), this.generator.getWaterLevel(x, z)) + 24;
        this.player!.flying = false; this.spawned = false;
      }
      this.settings = { ...this.settings, viewMode: '3d' }; this.setViewMode(); this.lockPointer();
    } catch (error) { this.mapRoot.querySelector('footer')!.textContent = error instanceof Error ? error.message : t('game.start3dFailed'); }
  }
  private centerMapOnPlayer(): void {
    const p = this.player ?? this.position;
    this.atlas?.centerAt(Math.floor(p.x / CHUNK_SIZE), Math.floor(p.z / CHUNK_SIZE));
  }
  private setViewMode(): void {
    const is2D = this.settings.viewMode === '2d';
    if (is2D && document.pointerLockElement) document.exitPointerLock();
    if (this.input) this.input.enabled = !this.chat.editing && !is2D && !this.paused && (!!this.settings.touchControls || document.pointerLockElement === this.renderer?.domElement);
    this.touch?.setVisible(!is2D && !this.paused && !!this.settings.touchControls);
    this.renderer?.domElement.classList.toggle('hidden', is2D);
    this.mapRoot.classList.toggle('hidden', !is2D);
    if (is2D) {
      if (!this.atlas) this.createMap(); else { this.centerMapOnPlayer(); this.atlas.requestRender(); }
    }
    this.atlas?.setActive(is2D && !this.paused);
    this.player?.resetInputGestures();
    this.overlay?.setVisible(this.gamingInfo);
    this.ui.loading.classList.toggle('hidden', is2D || this.spawned);
    this.ui.waterTint.classList.add('hidden');
    this.syncCrosshair();
    this.syncTopBar();
    this.updateDocumentTitle();
  }
  private syncTopBar(): void {
    const hidden = this.menuPage === null && this.settings.viewMode === '3d' && this.settings.showTopBar === false;
    this.bar.classList.toggle('hidden', hidden);
    const height = hidden ? 60 : this.bar.offsetHeight || 68;
    document.documentElement.style.setProperty('--game-bar-h', `${height}px`);
  }
  private syncCrosshair(): void {
    document.getElementById('crosshair')!.classList.toggle('hidden', this.settings.viewMode === '2d' || this.paused);
  }
  private resizeRenderer(): void {
    if (!this.renderer) return;
    const resolution = this.settings.resolution ?? 'auto';
    const [width, height] = resolution === 'auto' ? [innerWidth, innerHeight] : resolution.split('x').map(Number);
    this.renderer.setPixelRatio(resolution === 'auto' ? Math.min(devicePixelRatio, 2) : 1);
    this.renderer.setSize(width, height, false);
    this.renderer.domElement.style.width = '100vw'; this.renderer.domElement.style.height = '100dvh';
    this.renderer.domElement.style.imageRendering = resolution === '320x240' ? 'pixelated' : 'auto';
    this.world?.setMistProjection(this.camera.fov, height, this.renderer.getPixelRatio());
  }
  applySettings(value: GameSettings, persist = true): void {
    const next = validateSettings(value);
    if (next.viewMode === '3d') this.ensure3D();
    const changedView = next.viewMode !== this.settings.viewMode;
    const changedDistance = next.renderDistance !== this.settings.renderDistance;
    const changedParticles = (next.particles !== false) !== (this.settings.particles !== false);
    this.settings = next;
    applyAppearance(next); this.resizeRenderer(); this.touch?.applySettings(next);
    this.world?.setVegetationDistance(next.vegetationDistance!);
    if (changedParticles) this.world?.setMistEnabled(next.particles !== false);
    if (changedDistance) {
      this.world?.setRenderDistance(next.renderDistance);
      if (this.world && this.player) this.world.update(this.player.x, this.player.z);
      const distance = next.renderDistance * CHUNK_SIZE;
      this.fogNear = distance * .55; this.fogFar = distance * .95;
      this.fog.near = this.fogNear; this.fog.far = this.fogFar;
      this.camera.far = distance * 1.5 + 512;
    }
    this.touch?.setVisible(next.viewMode === '3d' && !this.paused && !!next.touchControls);
    this.lastCalendarMs = -Infinity;
    this.camera.fov = next.fov; this.camera.updateProjectionMatrix();
    if (this.renderer) this.world?.setMistProjection(next.fov, this.renderer.domElement.height, this.renderer.getPixelRatio());
    this.input?.setBindings(next.keyBindings);
    this.overlay?.setKeyLabel(keyLabel(next.keyBindings.debug));
    if (changedView) this.setViewMode();
    this.syncTopBar();
    this.syncClock();
    this.updateCalendar(true);
    if (persist) this.options.persistSettings(next);
  }
  setTimeSpeed(speed: number): void { this.applySettings({ ...this.settings, timeSpeed: speed }); }
  get timeSpeed(): number { return this.settings.timeSpeed; }
  private get worldPaused(): boolean {
    return this.paused && !!this.settings.menuAutoPause;
  }
  private syncClock(): void {
    this.calendarClock.dayLengthSeconds = 86400 / (this.settings.timeSpeed || 1);
    this.calendarClock.paused = this.worldPaused || this.settings.timeSpeed === 0;
  }
  private showPause(message = ''): void {
    if (this.saving) return;
    this.settingsPanel?.destroy(); this.settingsPanel = null;
    this.touch?.setVisible(false);
    this.paused = true; this.menuPage = 'pause'; this.syncClock();
    this.pauseMessage = message;
    if (this.input) this.input.enabled = false;
    this.atlas?.setActive(false); this.player?.resetInputGestures();
    if (document.pointerLockElement) document.exitPointerLock();
    this.syncCrosshair(); this.syncCorner();
    this.ui.start.classList.remove('hidden');
    this.ui.start.innerHTML = `<section class="pause-panel" aria-label="${t('game.pause.title')}"><p class="pause-description">${t(this.settings.menuAutoPause ? 'game.pause.description' : 'game.pause.running')}</p><label class="field-title" for="save-world-name">${t('game.saveName')}</label><input id="save-world-name" maxlength="80"><div class="pause-actions"><button class="secondary" data-save>${t('game.pause.save')}</button><button class="secondary" data-export>${t('game.pause.export')}</button><button class="secondary" data-settings>${t('game.pause.settings')}</button>${this.settings.viewMode === '3d' ? `<button class="secondary" data-map>${t('game.pause.map')}</button>` : ''}<button class="text-button" data-home>${t('game.pause.home')}</button></div><p class="menu-status" role="status" aria-live="polite"></p></section>`;
    const name = this.ui.start.querySelector<HTMLInputElement>('#save-world-name')!;
    // 优先恢复用户已输入但未保存的名称
    name.value = this.pauseNameDraft || this.worldName;
    name.addEventListener('input', () => {
      this.pauseNameDraft = name.value;
      const strong = this.bar.querySelector('strong');
      if (strong) strong.textContent = name.value.trim() || this.worldName;
    });
    const status = this.ui.start.querySelector<HTMLElement>('.menu-status')!; status.textContent = message;
    this.ui.start.querySelector('[data-map]')?.addEventListener('click', () => { this.settings = { ...this.settings, viewMode: '2d' }; this.setViewMode(); this.continueGame(); });
    this.ui.start.querySelector('[data-settings]')!.addEventListener('click', () => this.showSettings());
    const saveButton = this.ui.start.querySelector<HTMLButtonElement>('[data-save]')!;
    saveButton.addEventListener('click', async () => {
      if (this.saving) return;
      this.saving = true;
      this.ui.start.querySelectorAll<HTMLButtonElement>('button').forEach(button => { button.disabled = true; });
      name.disabled = true;
      saveButton.disabled = true; status.textContent = t('game.pause.saving');
      try {
        const save = this.createSave(name.value);
        const item = await this.options.saveStore.put(save, save.worldName!, this.saveId);
        this.saveId = item.id; this.worldName = item.name; name.value = item.name; this.pauseNameDraft = '';
        this.refreshWorldName();
        status.textContent = t('game.pause.saved', { name: item.name });
      } catch (error) { status.textContent = error instanceof Error ? error.message : t('game.pause.saveFailed'); }
      finally {
        this.saving = false; name.disabled = false;
        this.ui.start.querySelectorAll<HTMLButtonElement>('button').forEach(button => { button.disabled = false; });
      }
    });
    this.ui.start.querySelector('[data-export]')!.addEventListener('click', () => {
      try { const save = this.createSave(name.value); downloadWorldSave(save, save.worldName!, this.saveId); status.textContent = t('game.pause.exported'); }
      catch (error) { status.textContent = error instanceof Error ? error.message : t('game.pause.exportFailed'); }
    });
    this.ui.start.querySelector('[data-home]')!.addEventListener('click', () => {
      const url = new URL(location.href); url.searchParams.delete('resume');
      url.searchParams.set('seed', String(this.generator.seed)); url.searchParams.set('climate', this.generator.climateWeights.join(','));
      url.searchParams.set('generation', JSON.stringify(this.generator.generation)); location.href = url.toString();
    });
  }
  private showSettings(): void {
    if (this.saving) return;
    this.menuPage = 'settings'; this.ui.start.innerHTML = '<section class="pause-panel pause-settings"></section>';
    this.settingsPanel = new SettingsPanel(this.ui.start.firstElementChild as HTMLElement, this.settings, (value, persist) => this.applySettings(value, persist), () => this.showPause(), { headingBack: false, worldActive: true });
    this.syncCorner();
  }
  private continueGame(): void {
    if (this.saving) return;
    this.settingsPanel?.destroy(); this.settingsPanel = null;
    this.menuPage = null; this.paused = false; this.syncClock(); this.ui.start.classList.add('hidden');
    this.syncCrosshair(); this.syncCorner();
    this.atlas?.setActive(this.settings.viewMode === '2d');
    if (this.settings.viewMode === '3d') this.lockPointer();
  }
  private createSave(name: string): WorldSave {
    const p = this.player ?? this.position;
    return { generatorVersion: GENERATOR_VERSION, seed: this.generator.seed, climateWeights: this.generator.climateWeights,
      generation: this.generator.generation, calendarType: this.calendarType, unixMs: this.calendarClock.unixMs,
      artificialState: this.artificial.snapshot(), ren: this.ren, ren_ming: this.ren_ming,
      playerState: this.playerState, mystery: { timeSpeed: this.settings.timeSpeed, menuAutoPause: !!this.settings.menuAutoPause },
      utcOffsetMinutes: this.calendarSystem.utcOffsetMinutes, worldName: name.trim().slice(0, 80) || this.worldName,
      flying: this.player?.flying ?? this.options.save?.flying ?? false,
      player: { x: p.x, y: p.y, z: p.z, yaw: p.yaw, pitch: p.pitch } };
  }
  start(): void { requestAnimationFrame(time => this.frame(time)); }
  private updateCalendar(force = false): void {
    if (!force && Math.abs(this.calendarClock.unixMs - this.lastCalendarMs) < 1000) return;
    this.lastCalendarMs = this.calendarClock.unixMs;
    this.currentSnapshot = this.calendarSystem.snapshotFromUnixMs(this.calendarClock.unixMs);
    this.currentSeason = this.currentSnapshot.season?.index ?? 0; this.currentDayRatio = this.currentSnapshot.instant.frac;
    const date = this.calendarType === 'yuan' ? this.currentSnapshot.yuan : this.currentSnapshot.gregorian;
    const pad = (value: number) => String(value).padStart(2, '0');
    this.bar.querySelector('[data-date]')!.textContent = t('game.date', {
      calendar: calendarLabel(this.calendarType),
      year: date.year, month: date.month, day: date.day,
      time: `${pad(date.time.hour)}:${pad(date.time.minute)}`,
      season: seasonName(this.currentSeason),
      speed: this.settings.timeSpeed,
    });
  }
  private updateLighting(): void {
    const sky = this.world!.updateLighting(this.currentDayRatio, this.currentSeason, this.camera.position, performance.now() / 1000);
    this.renderer!.setClearColor(sky); this.fog.color.copy(sky);
  }
  private frame(time: number): void {
    const dt = this.previousFrame ? Math.min((time - this.previousFrame) / 1000, .05) : 0;
    this.previousFrame = time; this.calendarClock.advance(dt); this.updateCalendar();
    if (!this.worldPaused) {
      const p = this.player ?? this.position;
      this.artificial.update(this.calendarClock.unixMs, Math.floor(p.x / CHUNK_SIZE), Math.floor(p.z / CHUNK_SIZE));
      if (this.artificial.revision !== this.atlasTerritoryRevision && time - this.lastTerritoryRender > 250) {
        this.atlasTerritoryRevision = this.artificial.revision; this.lastTerritoryRender = time;
        if (this.settings.viewMode === '2d') this.atlas?.requestRender();
      }
    }
    if (this.settings.viewMode === '3d') this.frame3D(dt);
    else this.overlay?.update(performance.now());
    requestAnimationFrame(next => this.frame(next));
  }
  private frame3D(dt: number): void {
    const p = this.player!, world = this.world!, renderer = this.renderer!;
    if (world.generator.generation.mode === 'planet') {
      const circumference = world.generator.generation.planet.equatorChunks * CHUNK_SIZE;
      p.x = ((p.x + circumference / 2) % circumference + circumference) % circumference - circumference / 2;
      const pole = circumference / 4;
      if (p.z < -pole || p.z > pole) {
        p.z = p.z < -pole ? -2 * pole - p.z : 2 * pole - p.z;
        p.x = ((p.x + circumference) % circumference + circumference) % circumference - circumference / 2;
        p.yaw += Math.PI; p.vx = -p.vx; p.vz = -p.vz;
      }
    }
    world.update(p.x, p.z);
    if (!this.spawned) {
      const cx = Math.floor(p.x / CHUNK_SIZE), cz = Math.floor(p.z / CHUNK_SIZE);
      let ready = true;
      for (let dz = -1; dz <= 1 && ready; dz++) for (let dx = -1; dx <= 1 && ready; dx++) ready = world.isLoaded(cx + dx, cz + dz);
      if (ready) { this.spawned = true; this.ui.loading.classList.add('hidden'); }
    } else if (!this.worldPaused && (this.paused || this.settings.touchControls || document.pointerLockElement === renderer.domElement)) p.update(dt);
    this.camera.position.set(p.x, p.eyeY, p.z); this.camera.rotation.set(p.pitch, p.yaw, 0);
    this.updateLighting();
    const underwater = p.eyeY < world.getWaterLevel(p.x, p.z) && world.getHeight(p.x, p.z) < p.eyeY;
    if (underwater) {
      const day = THREE.MathUtils.smoothstep(-Math.cos(this.currentDayRatio * Math.PI * 2), -.12, .3);
      this.fog.color.copy(UNDERWATER_NIGHT).lerp(UNDERWATER_COLOR, day);
      this.fog.near = 1; this.fog.far = 36; renderer.setClearColor(this.fog.color);
    }
    else { this.fog.near = this.fogNear; this.fog.far = this.fogFar; }
    this.sky?.setUnderwater(underwater);
    // 雾色先更新
    this.sky?.update((this.currentDayRatio + .5) % 1, this.camera.position, this.calendarClock.unixMs);
    this.ui.waterTint.classList.toggle('hidden', !underwater);
    this.overlay?.update(performance.now()); renderer.render(this.scene, this.camera);
  }
  /** 停止语言监听（页面销毁或返回开始界面时调用）。 */
  destroy(): void { this.settingsPanel?.destroy(); this.stopLocaleWatch(); }
}
