import { createWorldGenerator } from '../systems/world/GeneratorFactory';
import { performanceBudget } from './PerformanceBudget';
import { PlaySession, parsePlayMode, parseCreationTimeSpeed, type PlayMode } from './PlayMode';
import { AtlasMovement } from '../systems/world/AtlasMovement';
import { defaultMysterySettings } from './MysterySettings';
import { parsePlayerState, type PlayerState } from '../entities/PlayerState';
import { playerFlagSpawn, type PlayerFactionOptions } from '../systems/world/artificial/PlayerFaction';
import { applyAppearance, applySettingsPalette } from '../ui/Appearance';
import * as THREE from 'three';
import { CHUNK_SIZE } from './config';
import { Input } from './Input';
import { TouchControls } from '../ui/TouchControls';
import { StatusFeedback } from '../ui/StatusFeedback';
import { Player } from '../entities/Player';
import { World } from '../systems/world/World';
import { Sky } from '../systems/world/Sky';
import { WEATHER, WeatherSystem, weatherDayRatio, weatherPrecipitation, sampleChunkWeather, type ChunkWeather } from '../systems/world/Weather';
import { WeatherVisual } from '../systems/world/WeatherVisual';
import { Precipitation, precipitationKey } from '../systems/world/weather/Precipitation';
import { precipitationWorld } from '../systems/world/GeneratorFactory';
import type { WorldGeneratorBase as WorldGenerator } from '../systems/world/WorldContract';
import { type ClimateWeights, type WorldGeneration } from '../systems/world/WorldSettings';
import { CURRENT_RULE_ID } from './WorldRule';
import type { AtlasDisplayState, PlayerPosition, WorldSave } from '../systems/world/WorldSave';
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
import { playerProfile, profileHeightRange, type PlayerProfile } from '../entities/PlayerProfile';
import { Exploration } from '../systems/world/Exploration';
import { WorldDataStore } from '../systems/world/WorldDataStore';
import { encodeWorldData } from '../systems/world/WorldDataCodec';
import type { PreparedWorld } from '../systems/world/PrepareWorld';

export interface GameOptions {
  worldKey?:string;
  prepared?:PreparedWorld;
  playMode?: PlayMode;
  creationTimeSpeed?: number;
  ren_ming?: string;
  profile?: PlayerProfile;
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
const FRAME_RATE_SLACK_MS = 1;

export class Game {
  private atlasDisplay:AtlasDisplayState={info:false,colors:false,relief:false,rainClouds:false};
  private renderer: THREE.WebGLRenderer | null = null;
  private readonly scene = new THREE.Scene();
  private readonly camera: THREE.PerspectiveCamera;
  private readonly fog: THREE.Fog;
  private input: Input | null = null;
  private touch: TouchControls | null = null;
  private readonly statusFeedback: StatusFeedback;
  private atlasMovement?:AtlasMovement;
  private readonly events=new AbortController();
  private mapBars?:ResizeObserver;
  private world: World | null = null;
  private player: Player | null = null;
  private overlay: DebugOverlay | null = null;
  private readonly chat: ChatPanel;
  private gamingInfo = false;
  private readonly playSession: PlaySession;
  get allowsCheats(): boolean { return this.playSession.allowsCheats; }
  private sky: Sky | null = null;
  private readonly weather: WeatherSystem;
  private weatherVisual: WeatherVisual | null = null;
  private precipitationIntensity = 0;
  private readonly weatherCache = new Map<string, ChunkWeather>();
  private weatherCacheMinute = -Infinity;
  private lastWeatherMapRender = 0;
  private precipitation: Precipitation | null = null;
  private precipitationStore: WorldDataStore | null = null;
  get rainCloudsAllowed(): boolean { return true; }
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
  ren_ming: string;
  private readonly playerState: PlayerState;
  private worldName: string;
  private saveId?: string;
  private paused = false;
  private saving = false;
  private menuPage: 'pause' | 'settings' | null = null;
  private spawned = false;
  private previousFrame = 0;
  private destroyed=false;
  private lastFrame3DAt = 0;
  private frame3DBudget = 0;
  private lastCalendarMs = -Infinity;
  private fogNear: number;
  private fogFar: number;
  /** 暂停面板存档名草稿 */
  private pauseNameDraft = '';
  private pauseMessage = '';
  private readonly stopLocaleWatch: () => void;
  private readonly worldKey:string;
  private readonly exploration:Exploration;
  private discoveryWrite=Promise.resolve();
  private discoveryDirty=false;
  private lastDiscoveryWrite=0;

  constructor(private readonly container: HTMLElement,
    private readonly ui: { start: HTMLElement; loading: HTMLElement; waterTint: HTMLElement },
    private readonly options: GameOptions) {
    this.playSession = new PlaySession(parsePlayMode(options.save?.playMode ?? options.playMode), parseCreationTimeSpeed(options.save?.creationTimeSpeed ?? options.creationTimeSpeed));
    this.gamingInfo = this.allowsCheats && options.showOverlay;
    const mystery = options.save?.mystery ?? { ...defaultMysterySettings(), timeSpeed: this.playSession.creationTimeSpeed };
    const profile = playerProfile(options.save ? options.save.profile ?? { name: options.save.playerState?.name ?? options.save.ren_ming, mainColor: options.save.playerFaction?.mainColor, trimColor: options.save.playerFaction?.trimColor } : { ...options.settings.profile, ...options.profile, name: options.profile?.name ?? options.ren_ming ?? options.settings.ren_ming });
    if (!options.save && !options.profile) {
      const range = profileHeightRange(this.playSession.mode);
      profile.heightCm = Math.max(range.min, Math.min(range.max, profile.heightCm));
    }
    playerProfile(profile, this.playSession.mode);
    this.settings = validateSettings({ ...options.settings, profile, ren_ming: profile.name, ...this.playSession.saveMystery(mystery), viewMode: '2d' });
    this.generator = options.prepared?.generator ?? createWorldGenerator(options.seed, options.climateWeights, options.generation);
    this.generator.cacheLimit=performanceBudget(this.settings).generatorEntries;
    this.worldKey=options.worldKey??options.save?.worldKey??crypto.randomUUID();
    this.atlasDisplay={...this.atlasDisplay,...options.save?.atlasDisplay};
    this.exploration=new Exploration(options.generation.mode==='planet'?options.generation.planet.equatorChunks:undefined,options.prepared?.exploration??options.save?.exploration);
    this.weather = new WeatherSystem(options.seed);
    this.worldName = options.worldName ?? t('game.defaultWorldName'); this.saveId = options.saveId;
    this.calendarType = options.calendarType;
    this.calendarSystem = new CalendarSystem({ mode: this.calendarType, utcOffsetMinutes: options.utcOffsetMinutes ?? 480 });
    this.calendarClock = new CalendarClock({ epochUnixMs: options.unixMs ?? Date.now(), dayLengthSeconds: 1200 });
    this.generator.setWaterSeason(this.calendarSystem.snapshotFromUnixMs(this.calendarClock.unixMs).season?.phase??0);
    this.setupPrecipitation(options);
    this.artificial = new ArtificialWorld(this.generator, this.calendarClock.unixMs, this.calendarType,
      this.calendarSystem.utcOffsetMinutes, options.prepared?.artificialState??options.save?.artificialState);
    this.artificial.setPlayerColors(profile.mainColor, profile.trimColor);
    applySettingsPalette(this.settings);
    this.ren_ming = profile.name;
    const flagSpawn = options.prepared?options.prepared.spawn:!options.initialPlayer && options.playerFaction && this.generator.generation.artificial?.enabled
      ? playerFlagSpawn(this.artificial, options.playerFaction) : null;
    // 宏观出生点由全球骨架直接给出，同步可用
    const spawn = options.initialPlayer ? null : flagSpawn ?? (options.playerFaction && this.generator.generation.artificial?.enabled ? null : this.generator.findSpawnOrigin());
    const owned = this.artificial.playerFactionId ? this.artificial.territories.get(this.artificial.playerFactionId) : undefined;
    this.playerState = parsePlayerState(options.save?.playerState ?? { name: this.ren_ming,
      factions: owned ? [{ id: owned.id, relationship: 'owned', mainColor: owned.mainColor, trimColor: owned.trimColor }] : [],
      ...(owned ? { activeFactionId: owned.id } : {}) });
    this.playerState.name = this.ren_ming;
    const x = spawn ? spawn.cx * CHUNK_SIZE + CHUNK_SIZE / 2 + .5 : options.initialPlayer?.x ?? 0;
    const z = spawn ? spawn.cz * CHUNK_SIZE + CHUNK_SIZE / 2 + .5 : options.initialPlayer?.z ?? 0;
    // 出生点搜索已算过该点地表与水位，命中即省去首次采样
    const surface = spawn ? this.generator.spawnSurface(spawn.cx, spawn.cz) : null;
    const startY = surface ? Math.max(surface.height, surface.water)
      : Math.max(this.generator.getHeight(x, z), this.generator.getWaterLevel(x, z));
    this.position = options.initialPlayer ? { ...options.initialPlayer } : { x, z, y: startY + .01, yaw: 0, pitch: 0 };
    if(options.prepared&&this.artificial.settings.enabled)this.artificial.enableBackground(
      new Worker(new URL('../systems/world/artificial/artificial.worker.ts',import.meta.url),{type:'module'}),
      this.calendarClock.unixMs,Math.floor(x/CHUNK_SIZE),Math.floor(z/CHUNK_SIZE),this.generator.exposeHydrology());
    this.syncClock();
    const distance = this.settings.renderDistance * CHUNK_SIZE;
    this.fogNear = distance * .55; this.fogFar = distance * .95;
    this.fog = new THREE.Fog(SKY_COLOR.clone(), this.fogNear, this.fogFar); this.scene.fog = this.fog;
    this.camera = new THREE.PerspectiveCamera(this.settings.fov, innerWidth / innerHeight, Math.min(.1, profile.heightCm / 2000), distance * 1.5 + 512);
    this.camera.rotation.order = 'YXZ';
    // 在创建 DOM 和监听器前检查 3D 支持
    if (this.settings.viewMode === '3d') this.ensure3D();
    this.bar = document.createElement('div'); this.bar.className = 'game-bar';
    this.bar.innerHTML = `<div class="game-identity"><strong></strong><span data-ren-name></span><span data-date></span></div>`;
    const playerName = this.bar.querySelector<HTMLElement>('[data-ren-name]')!;
    playerName.textContent = this.ren_ming;
    playerName.style.color = profile.mainColor;
    this.statusFeedback = new StatusFeedback(container);
    this.chat = new ChatPanel(container, this.ren_ming, profile.mainColor, value => this.runCommand(value), editing => {
      if (this.input) this.input.enabled = !editing && !this.paused && (this.settings.viewMode === '2d'
        || !!this.settings.touchControls || document.pointerLockElement === this.renderer?.domElement);
    });
    window.addEventListener('keydown', event => {
      if (!this.allowsCheats && (event.code === 'F12' || actionCodes('debug', this.settings.keyBindings).includes(event.code))) {
        event.preventDefault(); event.stopImmediatePropagation(); return;
      }
      if (event.isComposing || event.repeat) return;
      if (event.target instanceof HTMLElement && event.target.closest('input, select, textarea, button, [contenteditable]')) return;
      if (actionCodes('atlas', this.settings.keyBindings).includes(event.code)) {
        if(this.paused||this.chat.editing)return;
        event.preventDefault();
        if(this.settings.viewMode==='2d')this.enter3D();
        else{this.settings={...this.settings,viewMode:'2d'};this.setViewMode();}
      } else if (actionCodes('debug', this.settings.keyBindings).includes(event.code)) {
        event.preventDefault(); event.stopImmediatePropagation(); this.setGamingInfo(!this.gamingInfo);
      } else if (!event.altKey && !event.ctrlKey && !event.metaKey && (event.code === 'Enter' || event.code === 'Slash')) {
        event.preventDefault(); event.stopImmediatePropagation();
        if (!this.menuPage) this.showPause();
        this.chat.focus(event.code === 'Slash' ? '/' : '');
      }
    }, { capture: true,signal:this.events.signal });
    this.bar.querySelector('strong')!.textContent = this.worldName; container.appendChild(this.bar);
    this.corner = document.createElement('div'); this.corner.className = 'game-corner';
    this.corner.innerHTML = `<button class="menu-back corner-back hidden" data-back>${t('settings.back')}</button><button class="corner-menu" data-menu aria-label="${t('game.menu')}" title="${t('game.menu')}">${icon('menu')}</button>`;
    document.body.appendChild(this.corner);
    this.corner.querySelector('[data-menu]')!.addEventListener('click', () => { if (this.menuPage) this.continueGame(); else this.showPause(); });
    this.corner.querySelector('[data-back]')!.addEventListener('click', () => this.showPause());
    this.mapRoot = document.createElement('section'); this.mapRoot.className = 'game-map hidden';
    container.appendChild(this.mapRoot);
    this.input=new Input(this.mapRoot);this.input.setBindings(this.settings.keyBindings);
    this.touch=new TouchControls(this.input,document.createElement('canvas'));this.touch.applySettings(this.settings);
    this.updateCalendar(true); this.setViewMode(); this.syncClock();
    document.addEventListener('keydown', event => {
      if (event.code !== 'Escape' || event.repeat) return;
      event.preventDefault();
      if (this.menuPage === 'settings') this.showPause();
      else if (this.paused) this.continueGame(); else this.showPause();
    },{signal:this.events.signal});
    document.addEventListener('pointerlockchange', () => {
      if (!this.renderer) return;
      const locked = document.pointerLockElement === this.renderer.domElement;
      if (this.input) this.input.enabled = !this.chat.editing && !this.paused && (this.settings.viewMode==='2d'||locked||!!this.settings.touchControls);
      if (!locked && !this.settings.touchControls && this.settings.viewMode === '3d' && !this.paused) this.showPause();
    },{signal:this.events.signal});
    window.addEventListener('resize', () => {
      this.camera.aspect = innerWidth / innerHeight; this.camera.updateProjectionMatrix(); this.resizeRenderer();
      if (this.renderer) this.world?.setMistProjection(this.camera.fov, this.renderer.domElement.height, this.renderer.getPixelRatio());
      this.syncTopBar();
    },{signal:this.events.signal});
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
    toggle.querySelector('img')!.src = iconUrl('menu');
    const label = t(open ? 'game.menu.close' : 'game.menu');
    toggle.setAttribute('aria-label', label); toggle.title = label;
    toggle.setAttribute('aria-expanded', String(open));
    this.corner.classList.toggle('menu-resting', this.settings.viewMode === '3d' && !open);
    this.chat.setSidebar(open);
    this.chat.setSettingsOpen(this.menuPage === 'settings');
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
    try { renderer = new THREE.WebGLRenderer({ antialias: this.settings.antialias !== false, powerPreference: 'default' }); }
    catch { throw new Error(t('game.renderFailed')); }
    renderer.outputColorSpace = THREE.SRGBColorSpace;
    renderer.toneMapping = THREE.ACESFilmicToneMapping;
    renderer.toneMappingExposure = 1.1;
    renderer.domElement.tabIndex = -1;
    renderer.setClearColor(SKY_COLOR);
    this.renderer = renderer; this.resizeRenderer(); this.container.appendChild(renderer.domElement);
    this.input??=new Input(renderer.domElement);this.input.setTarget(renderer.domElement);
    this.touch?.dispose();this.touch = new TouchControls(this.input, renderer.domElement); this.input.setBindings(this.settings.keyBindings);
    this.world = new World(this.scene, this.options.seed, this.settings.renderDistance, this.options.climateWeights, this.options.generation, this.artificial,
      this.options.prepared?this.generator:undefined,this.worldKey,this.settings);
    this.world.setAdvancedVisuals(this.settings.waterReflections !== false);
    this.world.setMistEnabled(this.settings.particles !== false);
    this.world.setVegetationDistance(this.settings.vegetationDistance!);
    this.touch.applySettings(this.settings);
    this.world.setMistProjection(this.camera.fov, renderer.domElement.height, renderer.getPixelRatio());
    this.sky = new Sky(this.scene);
    this.sky.setAdvancedVisuals(this.settings.decorativeClouds !== false);
    this.weatherVisual = new WeatherVisual(this.scene);
    this.player = new Player(this.world, this.input); this.player.ren_ming = this.ren_ming; this.player.heightCm = this.settings.profile!.heightCm; Object.assign(this.player, this.position);
    if(!this.options.prepared)this.player.y = Math.max(this.player.y, this.world.getGroundUnder(this.player.x, this.player.z, .3) + .01);
    this.player.flightAllowed = this.allowsCheats;
    this.player.flying = this.allowsCheats && (this.options.save?.flying ?? false);
    this.overlay = new DebugOverlay(document.body, this.world, this.player, this);
    this.overlay.setKeyLabel(keyLabel(this.settings.keyBindings.debug));
    this.syncOverlayMapStyle();
    this.input.onAction('debug', () => this.setGamingInfo(!this.gamingInfo));
    renderer.domElement.addEventListener('click', () => { if (!this.paused && this.settings.viewMode === '3d') this.lockPointer(); });
  }
  private runCommand(value: string): string {
    if (this.playSession.unlock(value)) {
      if (this.player) this.player.flightAllowed = true;
      this.syncRainCloudControls();
      if (this.menuPage === 'settings') { this.settingsPanel?.destroy(); this.showSettings(); }
      const tip = this.mapRoot.querySelector('.game-map-tip');
      tip?.setAttribute('data-i18n', 'game.map.tip'); applyDomI18n(this.mapRoot);
      return t('mode.unlocked');
    }
    if (!this.allowsCheats) return t('mode.commandsBlocked');
    if (value.startsWith('/visual.statu')) {
      const response = this.statusFeedback.command(value)!;
      this.updateStatusFeedback();
      return response;
    }
    const weatherResponse = this.weather.command(value);
    if (weatherResponse !== null) { this.updateWeather(); return weatherResponse; }
    const match = /^\/system\.gaming_info\s+(on|off)$/.exec(value);
    if (!match) return value.startsWith('/system.gaming_info')
      ? 'Usage: /system.gaming_info on|off' : 'Unknown command.';
    const enabled = match[1] === 'on';
    this.setGamingInfo(enabled);
    return 'Gaming info ' + (enabled ? 'enabled.' : 'disabled.');
  }
  addChatMessage(message: ChatMessage): void { this.chat.addMessage(message); }
  private setGamingInfo(enabled: boolean): void {
    if (!this.allowsCheats) enabled = false;
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
    this.mapRoot.innerHTML = `<div class="game-map-tools"><div class="map-options"><label><input data-info type="checkbox"><span data-i18n="game.map.chunkInfo"></span></label><label><input data-colors type="checkbox"><span data-i18n="atlas.toggleChunkColors"></span></label><label><input data-relief type="checkbox"><span data-i18n="game.map.relief"></span></label></div><div class="map-buttons"><select data-planet-view aria-label="${t('game.map.viewAria')}"><option value="globe" data-i18n="atlas.view.globe"></option><option value="projection" data-i18n="atlas.view.projection"></option><option value="mercator" data-i18n="atlas.view.mercator"></option></select><button data-overview data-i18n="game.map.overview"></button><button data-zoom-in data-i18n="atlas.zoomInTitle" data-i18n-attr="aria-label">＋</button><button data-zoom-out data-i18n="atlas.zoomOutTitle" data-i18n-attr="aria-label">－</button><button data-position data-i18n="game.map.position"></button><button data-enter data-i18n="game.map.enter3d"></button></div></div><div class="game-map-viewport atlas-frame"><canvas aria-label="${t('game.map.canvasAria')}"></canvas><div class="game-map-tip" data-i18n="${this.allowsCheats ? 'game.map.tip' : 'game.map.survivalTip'}"></div></div><footer class="game-map-detail" role="status" data-i18n="game.map.hint"></footer>`;
    applyDomI18n(this.mapRoot);
    const mapBars = this.mapBars = new ResizeObserver(entries => {
      for (const { target } of entries) {
        const element = target as HTMLElement;
        const name = element.matches('.game-map-tools') ? '--game-map-tools-h' : '--game-map-detail-h';
        this.mapRoot.style.setProperty(name, `${element.getBoundingClientRect().height}px`);
      }
      this.syncChatMapPosition();
    });
    mapBars.observe(this.mapRoot.querySelector('.game-map-tools')!);
    mapBars.observe(this.mapRoot.querySelector('.game-map-detail')!);
    this.atlas = new AtlasView({ canvas: this.mapRoot.querySelector('canvas')!, generator: this.generator, artificial: this.artificial,
      previewWorkers:performanceBudget(this.settings).previewWorkers,worldKey:this.worldKey,exploration:this.playSession.mode==='survival'?this.exploration:undefined,
      initialCenter: { cx: Math.floor(this.position.x / CHUNK_SIZE), cz: Math.floor(this.position.z / CHUNK_SIZE) },
      precipitation: this.precipitation ?? undefined,
      playerMarker: () => { const p = this.player ?? this.position; return { x: p.x, z: p.z, yaw: p.yaw }; },
      onDestination: point=>{if(!this.settings.touchControls){this.ensureAtlasMovement();this.atlasMovement?.target(point);}},
      onChunkActivate: info => this.enter3D(info.cx, info.cz),
      onChunkSelect: info => {
        this.mapRoot.querySelector('footer')!.textContent = t('game.map.chunkFooter', {
          cx: info.cx, cz: info.cz, code: formatChunkId(info.type), name: chunkNameById(info.type,this.generator.generation.terrainRule==='planet-macro'),
          elevation: Math.round(info.elevation), temperature: info.temperature.toFixed(1),
        }) + (info.territory ? ` · ${t('territory.detail', { id: info.territory.name ?? info.territory.id, level: info.territory.level })}` : '');
      } });
    const on = (selector: string, fn: () => void) => this.mapRoot.querySelector(selector)?.addEventListener('click', fn);
    on('[data-enter]', () => this.enter3D());
    on('[data-position]', () => this.centerMapOnPlayer()); on('[data-zoom-in]', () => this.atlas?.zoomBy(1.4)); on('[data-zoom-out]', () => this.atlas?.zoomBy(1 / 1.4));
    const view = this.mapRoot.querySelector<HTMLSelectElement>('[data-planet-view]');
    view?.addEventListener('change', () => this.atlas?.setPlanetView(view.value as 'globe' | 'projection' | 'mercator'));
    on('[data-overview]', () => { this.atlas?.setPlanetView(view!.value as 'globe' | 'projection' | 'mercator'); this.atlas?.showOverview(); });
    const info = this.mapRoot.querySelector<HTMLInputElement>('[data-info]')!;
    info.checked=this.atlasDisplay.info;this.atlas.setShowChunkInfo(info.checked);
    info.addEventListener('change', () => { this.atlasDisplay.info=info.checked;this.atlas?.setShowChunkInfo(info.checked); this.syncOverlayMapStyle(); });
    const colors=this.mapRoot.querySelector<HTMLInputElement>('[data-colors]')!;
    colors.checked=this.atlasDisplay.colors;this.atlas.setShowChunkColors(colors.checked);
    colors.addEventListener('change',()=>{this.atlasDisplay.colors=colors.checked;this.atlas?.setShowChunkColors(colors.checked);this.syncOverlayMapStyle();});
    const relief = this.mapRoot.querySelector<HTMLInputElement>('[data-relief]')!;
    relief.checked=this.atlasDisplay.relief;this.atlas.setShowProjection(relief.checked);
    relief.addEventListener('change', () => { this.atlasDisplay.relief=relief.checked;this.atlas?.setShowProjection(relief.checked); this.syncOverlayMapStyle(); });
    this.syncRainCloudControls();
    this.syncOverlayMapStyle();
    if (this.options.initialPlayer || this.artificial.playerFactionId) this.centerMapOnPlayer();
  }
  /** 只有宏观星球有雨云数据，其余世界隐藏勾选 */
  private syncRainCloudControls(): void {
    if (!this.atlas) return;
    const existing = this.mapRoot.querySelector<HTMLInputElement>('[data-rain-clouds]');
    if (!this.precipitation) { existing?.parentElement?.remove(); return; }
    if (existing) return;
    const label = document.createElement('label');
    label.innerHTML = '<input data-rain-clouds type="checkbox"><span data-i18n="game.map.rainClouds"></span>';
    const input = label.querySelector<HTMLInputElement>('input')!;
    input.checked=this.atlasDisplay.rainClouds;this.atlas.setShowRainClouds(input.checked);
    input.addEventListener('change', () => {this.atlasDisplay.rainClouds=input.checked;this.atlas?.setShowRainClouds(input.checked);});
    this.mapRoot.querySelector('.map-options')!.append(label); applyDomI18n(label);
  }
  private syncOverlayMapStyle(): void {
    const checked = (selector: string) => !!this.mapRoot.querySelector<HTMLInputElement>(selector)?.checked;
    this.overlay?.setMapStyle({ info: checked('[data-info]'), colors: checked('[data-colors]'), relief: checked('[data-relief]') });
  }
  private enter3D(cx?: number, cz?: number): void {
    if (!this.allowsCheats && cx !== undefined && cz !== undefined) return;
    try {
      this.ensure3D();
      if (this.allowsCheats && cx !== undefined && cz !== undefined) {
        const x = cx * CHUNK_SIZE + CHUNK_SIZE / 2 + .5, z = cz * CHUNK_SIZE + CHUNK_SIZE / 2 + .5;
        this.player!.x=x;this.player!.z=z;
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
    this.updateStatusFeedback();
    if(!is2D){this.atlasMovement?.dispose();this.atlasMovement=undefined;}
    this.world?.setActive(!is2D);
    if (!is2D) { this.lastFrame3DAt = 0; this.frame3DBudget = 0; }
    if (is2D && document.pointerLockElement) document.exitPointerLock();
    if (this.input) this.input.enabled = !this.chat.editing && !this.paused && (is2D || !!this.settings.touchControls || document.pointerLockElement === this.renderer?.domElement);
    this.touch?.setVisible(!this.paused && !!this.settings.touchControls);
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
    this.syncCorner();
    this.chat.element.classList.toggle('chat-atlas', is2D);
    this.syncChatMapPosition();
  }
  private syncChatMapPosition(): void {
    if (this.settings.viewMode !== '2d' || !this.mapRoot) return;
    const tools = this.mapRoot.querySelector('.game-map-tools');
    if (tools) this.chat.element.style.setProperty('--chat-atlas-top', `${tools.getBoundingClientRect().bottom + 12}px`);
  }
  private syncTopBar(): void {
    const hidden = this.menuPage === null && this.settings.viewMode === '3d' && this.settings.showTopBar === false;
    this.bar.classList.toggle('hidden', hidden);
    const height = hidden ? 60 : this.bar.offsetHeight || 68;
    document.documentElement.style.setProperty('--game-bar-h', `${height}px`);
    this.syncChatMapPosition();
  }
  private syncCrosshair(): void {
    document.getElementById('crosshair')!.classList.toggle('hidden', this.settings.viewMode === '2d' || this.paused);
  }
  private replaceRenderer():void {
    let renderer:THREE.WebGLRenderer;
    try{renderer=new THREE.WebGLRenderer({antialias:this.settings.antialias!==false,powerPreference:'default'});}catch{throw new Error(t('game.renderFailed'));}
    renderer.outputColorSpace=THREE.SRGBColorSpace;renderer.toneMapping=THREE.ACESFilmicToneMapping;renderer.toneMappingExposure=1.1;renderer.domElement.tabIndex=-1;
    const previous=this.renderer;this.renderer=renderer;this.resizeRenderer();this.container.appendChild(renderer.domElement);
    this.input?.setTarget(renderer.domElement);this.touch?.dispose();
    if(this.input){this.touch=new TouchControls(this.input,renderer.domElement);this.touch.applySettings(this.settings);}
    renderer.domElement.addEventListener('click',()=>{if(!this.paused&&this.settings.viewMode==='3d')this.lockPointer();});
    previous?.dispose();previous?.forceContextLoss();previous?.domElement.remove();
  }
  private resizeRenderer(): void {
    if (!this.renderer) return;
    const resolution = this.settings.resolution ?? 'auto';
    const [width, height] = resolution === 'auto' ? [innerWidth, innerHeight] : resolution.split('x').map(Number);
    const budget=performanceBudget(this.settings);
    const ratio=resolution==='auto'?Math.min(devicePixelRatio,budget.pixelRatioCap):1;
    const limit=budget.renderPixels;
    this.renderer.setPixelRatio(ratio*Math.min(1,Math.sqrt(limit/(width*height*ratio*ratio))));
    this.renderer.setSize(width, height, false);
    this.renderer.domElement.style.width = '100vw'; this.renderer.domElement.style.height = '100dvh';
    this.renderer.domElement.style.imageRendering = resolution === '320x240' ? 'pixelated' : 'auto';
    this.world?.setMistProjection(this.camera.fov, height, this.renderer.getPixelRatio());
  }
  applySettings(value: GameSettings, persist = true): void {
    const next = validateSettings(this.allowsCheats ? value : { ...value, profile: this.settings.profile, ren_ming: this.settings.ren_ming, timeSpeed: this.playSession.creationTimeSpeed, menuAutoPause: false });
    playerProfile(next.profile, this.playSession.mode);
    const changedAntialias=next.antialias!==this.settings.antialias;
    const changedPreset=next.graphicsPreset!==this.settings.graphicsPreset;
    const changedView = next.viewMode !== this.settings.viewMode;
    const changedDistance = next.renderDistance !== this.settings.renderDistance;
    this.settings = next;
    this.ren_ming = next.profile!.name;
    this.playerState.name = this.ren_ming;
    if (this.player) { this.player.ren_ming = this.ren_ming; this.player.heightCm = next.profile!.heightCm; }
    this.atlasMovement?.setHeight(next.profile!.heightCm);
    const playerName = this.bar.querySelector<HTMLElement>('[data-ren-name]')!;
    playerName.textContent = this.ren_ming; playerName.style.color = next.profile!.mainColor;
    this.chat.setPlayerIdentity(this.ren_ming, next.profile!.mainColor);
    this.artificial.setPlayerColors(next.profile!.mainColor, next.profile!.trimColor);
    applySettingsPalette(next);
    for (const faction of this.playerState.factions) if (faction.relationship === 'owned') { faction.mainColor = next.profile!.mainColor; faction.trimColor = next.profile!.trimColor; }
    const hadRenderer=!!this.renderer;
    if(next.viewMode==='3d')this.ensure3D();
    if(changedAntialias&&hadRenderer)this.replaceRenderer();
    if(changedPreset){
      const budget=performanceBudget(next);this.world?.setPerformanceBudget(next);this.atlas?.setPreviewWorkers(budget.previewWorkers);
      this.artificial.setCacheBudget(budget.generatorEntries);this.atlasMovement?.setCacheBudget(budget.generatorEntries);
    }
    this.generator.cacheLimit=performanceBudget(next).generatorEntries;
    applyAppearance(next); this.resizeRenderer(); this.touch?.applySettings(next);
    this.world?.setVegetationDistance(next.vegetationDistance!);
    this.world?.setDenseVegetation(next.denseVegetation!);
    this.world?.setAdvancedVisuals(next.waterReflections !== false);
    this.sky?.setAdvancedVisuals(next.decorativeClouds !== false);
    this.world?.setMistEnabled(next.particles !== false);
    this.updateStatusFeedback();
    if (changedDistance) {
      this.world?.setRenderDistance(next.renderDistance);
      if (this.world && this.player) this.world.update(this.player.x, this.player.z);
      const distance = next.renderDistance * CHUNK_SIZE;
      this.fogNear = distance * .55; this.fogFar = distance * .95;
      this.fog.near = this.fogNear; this.fog.far = this.fogFar;
      this.camera.far = distance * 1.5 + 512;
    }
    this.touch?.setVisible(!this.paused && !!next.touchControls);
    this.lastCalendarMs = -Infinity;
    this.camera.fov = next.fov; this.camera.near = Math.min(.1, next.profile!.heightCm / 2000); this.camera.updateProjectionMatrix();
    if (this.renderer) this.world?.setMistProjection(next.fov, this.renderer.domElement.height, this.renderer.getPixelRatio());
    this.input?.setBindings(next.keyBindings);
    if(this.input)this.input.enabled=!this.paused&&!this.chat.editing&&(next.viewMode==='2d'||!!next.touchControls||document.pointerLockElement===this.renderer?.domElement);
    this.overlay?.setKeyLabel(keyLabel(next.keyBindings.debug));
    if (changedView) this.setViewMode();
    this.syncTopBar();
    this.syncClock();
    this.updateCalendar(true);
    if (persist) this.options.persistSettings({ ...next, profile: this.options.settings.profile, ren_ming: this.options.settings.ren_ming });
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
        await this.atlasMovement?.flush();await this.artificial.flushBackground();await this.flushDiscovery();await this.world?.flushWrites();
        await this.saveWeather();
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
    this.ui.start.querySelector('[data-export]')!.addEventListener('click', async () => {
      try { await this.atlasMovement?.flush();await this.artificial.flushBackground();await this.flushDiscovery();await this.world?.flushWrites();await this.saveWeather();const save = this.createSave(name.value); await downloadWorldSave(save, save.worldName!, this.saveId); status.textContent = t('game.pause.exported'); }
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
    this.settingsPanel = new SettingsPanel(this.ui.start.firstElementChild as HTMLElement, this.settings, (value, persist) => this.applySettings(value, persist), () => this.showPause(), { headingBack: false, worldActive: true, hideMystery: !this.allowsCheats, profileLocked: !this.allowsCheats, playMode: this.playSession.mode });
    this.syncCorner();
  }
  private continueGame(): void {
    if (this.saving) return;
    this.settingsPanel?.destroy(); this.settingsPanel = null;
    this.menuPage = null; this.paused = false; this.syncClock(); this.ui.start.classList.add('hidden');
    this.syncCrosshair(); this.syncCorner();
    this.atlas?.setActive(this.settings.viewMode === '2d');
    if(this.settings.viewMode==='2d'){if(this.input)this.input.enabled=!this.chat.editing;this.touch?.setVisible(!!this.settings.touchControls);}
    if (this.settings.viewMode === '3d') this.lockPointer();
  }
  private async saveWeather():Promise<void> {
    if(!this.precipitation)return;
    const paused=this.calendarClock.paused;this.calendarClock.paused=true;
    try{await this.precipitation.save();}
    finally{this.calendarClock.paused=paused;}
  }
  private createSave(name: string): WorldSave {
    const p = this.player ?? this.position;
    return { atlasDisplay:{...this.atlasDisplay},worldKey:this.worldKey,exploration:this.exploration.snapshot(),playMode: this.playSession.mode, creationTimeSpeed: this.playSession.creationTimeSpeed, ruleVersion: CURRENT_RULE_ID, seed: this.generator.seed, climateWeights: this.generator.climateWeights,
      generation: this.generator.generation, calendarType: this.calendarType, unixMs: this.calendarClock.unixMs,
      ...(this.precipitation ? {weatherKey:this.precipitation.dataKey} : {}),
      artificialState: this.artificial.snapshot(), ren: this.ren, ren_ming: this.ren_ming,
      playerState: parsePlayerState(this.playerState), profile: { ...this.settings.profile! }, mystery: this.playSession.saveMystery({ timeSpeed: this.settings.timeSpeed, menuAutoPause: !!this.settings.menuAutoPause }),
      utcOffsetMinutes: this.calendarSystem.utcOffsetMinutes, worldName: name.trim().slice(0, 80) || this.worldName,
      flying: this.playSession.mode === 'survival' ? false : this.player?.flying ?? this.options.save?.flying ?? false,
      player: { x: p.x, y: p.y, z: p.z, yaw: p.yaw, pitch: p.pitch } };
  }
  start(): void { requestAnimationFrame(time => this.frame(time)); }
  private updateCalendar(force = false): void {
    if (!force && Math.abs(this.calendarClock.unixMs - this.lastCalendarMs) < 1000) return;
    this.lastCalendarMs = this.calendarClock.unixMs;
    this.currentSnapshot = this.calendarSystem.snapshotFromUnixMs(this.calendarClock.unixMs);
    const phase=this.currentSnapshot.season?.phase??0;
    const waterChanged=this.world?this.world.setWaterSeason(phase):this.generator.setWaterSeason(phase);
    if(waterChanged){this.weatherCache.clear();this.atlas?.requestRender();}
    this.precipitation?.refresh();
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
  private updateWeather(): void {
    const p = this.player ?? this.position;
    const natural = this.getChunkWeather(Math.floor(p.x / CHUNK_SIZE), Math.floor(p.z / CHUNK_SIZE));
    this.weather.display(natural);
    this.precipitationIntensity = this.weather.isForced ? weatherPrecipitation(this.weather.current) : natural.intensity;
  }
  /** 宏观星球改用逐日降水，其余世界仍用噪声云团 */
  private setupPrecipitation(options: GameOptions): void {
    const world = precipitationWorld(this.generator);
    if (!world) return;
    const store = this.precipitationStore = new WorldDataStore(this.worldKey);
    this.precipitation = new Precipitation({
      world, store, dataKey: options.save?.weatherKey ?? precipitationKey(),
      unixMs: options.save?.unixMs ?? this.calendarClock.unixMs, utcOffsetMinutes: this.calendarSystem.utcOffsetMinutes,
      isForced: () => this.weather.isForced, forcedWeather: () => this.weather.current,
      createWorker: () => new Worker(new URL('../systems/world/weather/daily.worker.mjs', import.meta.url), { type: 'module' }),
      onStateChange: () => { if (this.settings.viewMode === '2d') this.atlas?.requestRender(); },
    });
    void this.startPrecipitation(options.save?.worldKey);
  }
  private async startPrecipitation(existingWorldKey: string | undefined): Promise<void> {
    const precipitation = this.precipitation, store = this.precipitationStore;
    if (!precipitation || !store) return;
    try {
      const saved = existingWorldKey ? await store.get(precipitation.dataKey).catch(() => null) : null;
      await precipitation.initialize(saved);
    } catch (error) { console.warn('[Precipitation]', error); }
  }
  getChunkWeather(cx: number, cz: number): ChunkWeather {
    if (this.precipitation?.ready) return this.precipitation.chunkWeather(cx, cz, this.generator.getSurfaceTemperature(cx, cz));
    const minute = Math.floor(this.calendarClock.unixMs / 60000);
    if (minute !== this.weatherCacheMinute) { this.weatherCache.clear(); this.weatherCacheMinute = minute; }
    const key = `${Math.floor(cx)},${Math.floor(cz)}`;
    let state = this.weatherCache.get(key);
    if (!state) {
      state = sampleChunkWeather(this.generator, this.calendarClock.unixMs, cx, cz);
      if (this.weatherCache.size >= 4096) this.weatherCache.clear();
      this.weatherCache.set(key, state);
    }
    return state;
  }
  private updateLighting(): void {
    const sky = this.world!.updateLighting(this.currentDayRatio, this.currentSeason, this.camera.position, performance.now() / 1000, this.weather.current, this.weatherVisual?.flash ?? 0, this.precipitationIntensity);
    this.renderer!.setClearColor(sky); this.fog.color.copy(sky);
  }
  private frame(time: number): void {
    if(this.destroyed)return;
    requestAnimationFrame(next => this.frame(next));
    const dt = this.previousFrame ? Math.min((time - this.previousFrame) / 1000, .05) : 0;
    this.previousFrame = time; this.calendarClock.advance(dt); this.updateCalendar();
    this.updateWeather();
    const location=this.player??this.position;
    if((this.settings.viewMode==='2d'||this.spawned)&&this.exploration.revealDiamond(Math.floor(location.x/CHUNK_SIZE),Math.floor(location.z/CHUNK_SIZE))){this.discoveryDirty=true;}
    if(this.discoveryDirty&&time-this.lastDiscoveryWrite>3000){this.lastDiscoveryWrite=time;void this.flushDiscovery().catch(error=>console.warn('[Exploration]',error));}
    if (this.settings.viewMode === '2d' && this.atlas?.getShowRainClouds() && time - this.lastWeatherMapRender > 120000) {
      this.lastWeatherMapRender = time; this.atlas.requestRender();
    }
    if (!this.worldPaused) {
      const p = this.player ?? this.position;
      this.artificial.update(this.calendarClock.unixMs, Math.floor(p.x / CHUNK_SIZE), Math.floor(p.z / CHUNK_SIZE));
      if (this.artificial.revision !== this.atlasTerritoryRevision && time - this.lastTerritoryRender > 250) {
        this.atlasTerritoryRevision = this.artificial.revision; this.lastTerritoryRender = time;
        if (this.settings.viewMode === '2d') this.atlas?.requestRender();
      }
    }
    if (this.settings.viewMode === '3d') {
      const cap = this.settings.frameRate ?? 0;
      const interval = cap > 0 ? 1000 / cap : 0;
      const elapsed = this.lastFrame3DAt ? time - this.lastFrame3DAt : 0;
      if (interval > 0) {
        this.frame3DBudget += dt * 1000;
        if (this.lastFrame3DAt && this.frame3DBudget < interval - FRAME_RATE_SLACK_MS) return;
        this.frame3DBudget = Math.max(0, this.frame3DBudget - interval);
      }
      const step = this.lastFrame3DAt ? Math.min(elapsed / 1000, .05) : 0;
      this.lastFrame3DAt = time;
      this.frame3D(step, elapsed);
    } else {
      if(!this.worldPaused&&!this.paused&&!this.chat.editing){
        const dx=Number(this.input?.isActionDown('right'))-Number(this.input?.isActionDown('left'));
        const dz=Number(this.input?.isActionDown('back'))-Number(this.input?.isActionDown('forward'));
        if(dx||dz)this.ensureAtlasMovement();
        this.atlasMovement?.update(dt,dx,dz,!!this.input?.isActionDown('sprint'));
      }
      this.updateStatusFeedback();
      this.overlay?.update(performance.now());
    }
  }
  private ensureAtlasMovement():void {
    if(this.atlasMovement)return;
    this.atlasMovement=new AtlasMovement(new Worker(new URL('../systems/world/atlas-movement.worker.ts',import.meta.url),{type:'module'}),
      this.generator,this.player??this.position,value=>{
        if(this.destroyed||this.settings.viewMode!=='2d')return;
        const p=this.player??this.position,moved=p.x!==value.position.x||p.z!==value.position.z;
        Object.assign(p,value.position);
        if(this.player){this.player.vx=this.player.vy=this.player.vz=0;this.player.flying=false;}
        if(moved){this.exploration.revealDiamond(Math.floor(p.x/CHUNK_SIZE),Math.floor(p.z/CHUNK_SIZE));this.discoveryDirty=true;this.atlas?.requestRender();}
        if(value.blocked)this.mapRoot.querySelector('footer')!.textContent=t('game.map.noPath');
      });
    this.atlasMovement.setHeight(this.settings.profile!.heightCm);
  }
  private async flushDiscovery():Promise<void>{
    if(this.discoveryDirty){
      this.discoveryDirty=false;
      const bytes=encodeWorldData(this.exploration.snapshot());
      this.discoveryWrite=this.discoveryWrite.catch(()=>{}).then(()=>new WorldDataStore(this.worldKey).put('exploration',bytes)).catch(error=>{this.discoveryDirty=true;throw error;});
    }
    await this.discoveryWrite;
  }
  private frame3D(dt: number, frameMs = 0): void {
    const p = this.player!, world = this.world!, renderer = this.renderer!;
    // 球面环绕：越过经度接缝与极点时折返
    {
      const circumference = world.generator.generation.planet.equatorChunks * CHUNK_SIZE;
      p.x = ((p.x + circumference / 2) % circumference + circumference) % circumference - circumference / 2;
      const pole = circumference / 4;
      if (p.z < -pole || p.z > pole) {
        p.z = p.z < -pole ? -2 * pole - p.z : 2 * pole - p.z;
        p.x = ((p.x + circumference) % circumference + circumference) % circumference - circumference / 2;
        p.yaw += Math.PI; p.vx = -p.vx; p.vz = -p.vz;
      }
    }
    world.update(p.x, p.z, p.vx, p.vz, frameMs);
    if(this.spawned&&!world.hasGroundAround(p.x,p.z)){
      this.spawned=false;this.ui.loading.classList.remove('hidden');
    }
    if (!this.spawned) {
      this.updateStatusFeedback();
      const cx = Math.floor(p.x / CHUNK_SIZE), cz = Math.floor(p.z / CHUNK_SIZE);
      let ready = true;
      for (let dz = -1; dz <= 1 && ready; dz++) for (let dx = -1; dx <= 1 && ready; dx++) ready = world.isLoaded(cx + dx, cz + dz);
      if (ready) { p.spawnAt(p.x,p.z);this.spawned = true; this.ui.loading.classList.add('hidden'); }
      else return;
    } else if (!this.worldPaused && (this.paused || this.settings.touchControls || document.pointerLockElement === renderer.domElement)) p.update(dt);
    this.camera.position.set(p.x, p.eyeY, p.z); this.camera.rotation.set(p.pitch, p.yaw, 0);
    const underwater = p.eyeY < world.getWaterLevel(p.x, p.z) && world.getBedHeight(p.x,p.z) < p.eyeY;
    this.weatherVisual?.update(this.weather.current, this.worldPaused ? 0 : dt, this.camera.position, underwater,
      this.settings.particles !== false, (x, z) => {
        for (const cx of [Math.floor((x - 3) / CHUNK_SIZE), Math.floor((x + 3) / CHUNK_SIZE)])
          for (const cz of [Math.floor((z - 3) / CHUNK_SIZE), Math.floor((z + 3) / CHUNK_SIZE)])
            if (!world.isLoaded(cx, cz)) return this.camera.position.y + 10000;
        return Math.max(world.getHeight(x, z), world.getWaterLevel(x, z));
      },
      this.weather.isForced ? undefined : (x, z) => this.getChunkWeather(Math.floor(x / CHUNK_SIZE), Math.floor(z / CHUNK_SIZE)).id,
      this.settings.weatherAtmosphere !== false);
    this.updateLighting();
    if (underwater) {
      const day = THREE.MathUtils.smoothstep(-Math.cos(weatherDayRatio(this.weather.current, this.currentDayRatio) * Math.PI * 2), -.12, .3);
      this.fog.color.copy(UNDERWATER_NIGHT).lerp(UNDERWATER_COLOR, day);
      this.fog.near = 1; this.fog.far = 36; renderer.setClearColor(this.fog.color);
    }
    else { this.fog.near = this.fogNear * WEATHER[this.weather.current].fog; this.fog.far = this.fogFar * WEATHER[this.weather.current].fog; }
    this.sky?.setUnderwater(underwater);
    // 雾色先更新
    this.sky?.update((this.currentDayRatio + .5) % 1, this.camera.position, this.calendarClock.unixMs, this.weather.current, this.worldPaused, this.precipitationIntensity);
    this.ui.waterTint.classList.toggle('hidden', !underwater);
    this.updateStatusFeedback();
    world.updateDecorationView(this.camera);
    world.updateNaturalPhysics(this.worldPaused?0:dt,this.settings.naturalPhysics!==false,
      (cx,cz)=>this.weather.isForced?this.weather.current:this.getChunkWeather(cx,cz).id,
      `${Math.floor(this.calendarClock.unixMs/60000)}:${this.weather.isForced?this.weather.current:'natural'}`);
    this.overlay?.update(performance.now()); renderer.render(this.scene, this.camera);
  }
  private updateStatusFeedback(): void {
    const mountain = this.settings.viewMode === '3d';
    const blocked = mountain && this.spawned && !this.paused && !this.worldPaused && !this.chat.editing && this.input?.enabled && this.player?.blockedForward;
    this.statusFeedback.update(blocked ? '001' : null, this.settings.statusFeedback !== false,
      mountain ? this.renderer?.domElement ?? null : this.mapRoot.querySelector('canvas'));
  }

  destroy(): void { this.corner.remove();this.chat.destroy();this.destroyed=true;this.events.abort();this.precipitation?.dispose();this.statusFeedback.dispose();this.mapBars?.disconnect();this.input?.dispose();this.atlas?.destroy();this.atlasMovement?.dispose();this.touch?.dispose();this.artificial.dispose();this.world?.dispose();this.renderer?.dispose();this.weatherVisual?.dispose(); this.sky?.dispose(); this.settingsPanel?.destroy(); this.stopLocaleWatch(); }
}
