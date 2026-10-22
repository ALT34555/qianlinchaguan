import { CHUNK_SIZE } from '../../core/config';
import { ArtificialWorld } from '../../systems/world/artificial/ArtificialWorld';
import { isAbortError, prepareWorld, type PreparedWorld, type WorldPreparationOptions } from '../../systems/world/PrepareWorld';
import type { WorldGeneratorBase as WorldGenerator } from '../../systems/world/WorldContract';
import { AtlasView, type AtlasViewOptions, type ViewportStats } from '../AtlasView';
import type { PlanetView } from '../PlanetAtlasLayer';
import { DEFAULT_GENERATION, normalizeGeneration, type WorldGeneration } from '../../systems/world/WorldSettings';
import { type LayerState } from '../atlas/layers.mjs';
import { t } from '../../i18n';
import type { StartOptions } from './options';
import type { AtlasLoadingHandle } from './loading';

export type PreviewPhase = 'requested' | 'reused' | 'skeleton-ready' | 'map-visible' | 'overlay-closed' | 'ready' | 'failed' | 'cancelled';

export interface PreviewTiming {
  phase: PreviewPhase;
  at: number;
  requestId: number;
  cacheHit?: boolean;
  reused?: boolean;
  detail?: string;
}

export interface PreviewControlState {
  layers: LayerState[];
  density: number;
  planetView: PlanetView;
}

/** 预览控制器的AtlasView接口 */
export interface PreviewAtlas {
  centerAt(cx: number, cz: number): void;
  setShowChunkInfo(show: boolean): void;
  setShowChunkColors(show: boolean): void;
  setShowSatellite(show: boolean): void;
  setMapLayers(layers: LayerState[]): void;
  setContourDensity(density: number): void;
  setShowFlowArrows(show: boolean): void;
  setShowClimate(show: boolean): void;
  setPlanetView(view: PlanetView): void;
  showOverview(): void;
  centerSpawn(): void;
  resetView(): void;
  zoomBy(factor: number): void;
  requestRender(): void;
  destroy(): void;
}

export type AtlasFactory = (options: AtlasViewOptions) => PreviewAtlas;
export type PrepareWorldFn = (options: WorldPreparationOptions, progress: (message: string) => void, control: { signal: AbortSignal }) => Promise<PreparedWorld>;

export interface PreviewUi {
  canvas: HTMLCanvasElement;
  createLoading(): AtlasLoadingHandle;
  showMap(): void;
  setStatus(message: string): void;
  setError(message: string): void;
  setBusy(busy: boolean): void;
  readControls(): PreviewControlState;
}

export interface PreviewHooks {
  onStatsUpdate(stats: ViewportStats): void;
  onPhase?(timing: PreviewTiming): void;
}

export interface PreviewControllerOptions {
  worldKey: string;
  playMode: 'survival' | 'leisure';
  ui: PreviewUi;
  hooks: PreviewHooks;
  /** 测试注入可控生成服务 */
  prepare?: PrepareWorldFn;
  createAtlas?: AtlasFactory;
  createArtificialWorker?: () => Worker;
  now?: () => number;
}

export function previewSignature(options: StartOptions): string {
  // 仅世界/出生/季节输入使签名失效
  return JSON.stringify([
    options.seed,
    options.climateWeights,
    normalizeGeneration(options.generation ?? DEFAULT_GENERATION),
    options.calendarType,
    options.unixMs,
    options.utcOffsetMinutes,
    options.playerFaction,
    options.playMode,
  ]);
}

function preparationOptions(options: StartOptions, worldKey: string): WorldPreparationOptions {
  return {
    worldKey,
    seed: options.seed,
    climateWeights: options.climateWeights,
    generation: options.generation,
    calendarType: options.calendarType,
    unixMs: options.unixMs,
    utcOffsetMinutes: options.utcOffsetMinutes,
    playerFaction: options.playerFaction,
    save: options.save,
  };
}

function nextFrame(signal: AbortSignal): Promise<void> {
  return new Promise(resolve => {
    if (signal.aborted) { resolve(); return; }
    let settled = false;
    const finish = (): void => {
      if (settled) return;
      settled = true;
      signal.removeEventListener('abort', onAbort);
      resolve();
    };
    const frame = requestAnimationFrame(() => finish());
    const onAbort = (): void => { cancelAnimationFrame(frame); finish(); };
    signal.addEventListener('abort', onAbort, { once: true });
  });
}

/** 拥有预览请求身份、生成资源与失效路径 */
export class PreviewController {
  private epoch = 0;
  private disposed = false;
  private abort?: AbortController;
  private loading?: AtlasLoadingHandle;
  private busyState = false;
  private atlasView: PreviewAtlas | null = null;
  private artificialWorld?: ArtificialWorld;
  private generatorWorld: WorldGenerator | null = null;
  private reusable?: { signature: string; prepared: PreparedWorld };
  private timings: PreviewTiming[] = [];
  private readonly prepare: PrepareWorldFn;
  private readonly createAtlas: AtlasFactory;
  private readonly createArtificialWorker: () => Worker;
  private readonly now: () => number;

  constructor(private readonly options: PreviewControllerOptions) {
    this.prepare = options.prepare ?? ((target, progress, control) => prepareWorld(target, progress, control));
    this.createAtlas = options.createAtlas ?? (view => new AtlasView(view));
    this.createArtificialWorker = options.createArtificialWorker ?? (() => new Worker(new URL('../../systems/world/artificial/artificial.worker.ts', import.meta.url), { type: 'module' }));
    this.now = options.now ?? (() => performance.now());
  }

  get atlas(): PreviewAtlas | null { return this.atlasView; }
  get generation(): WorldGeneration | null { return this.generatorWorld?.generation ?? null; }
  get busy(): boolean { return this.busyState; }
  get lastTimings(): readonly PreviewTiming[] { return this.timings; }

  /** 新预览会中断旧任务；参数相同可直接复用已准备世界 */
  async start(options: StartOptions): Promise<void> {
    if (this.disposed) return;
    const epoch = ++this.epoch;
    const previous = this.abort;
    const controller = new AbortController();
    this.abort = controller;
    previous?.abort();
    this.loading?.cancel();
    this.loading = undefined;
    this.busyState = true;
    this.options.ui.setBusy(true);
    this.timings = [];
    let loading: AtlasLoadingHandle | undefined;
    try {
      const signature = previewSignature(options);
      const cached = this.reusable?.signature === signature ? this.reusable.prepared : undefined;
      loading = cached ? undefined : this.options.ui.createLoading();
      if (loading) this.loading = loading;
      let prepared = cached;
      if (prepared) {
        this.emit('reused', epoch, { reused: true });
      } else {
        this.emit('requested', epoch);
        prepared = await this.prepare(preparationOptions(options, this.options.worldKey), message => {
          if (!this.isCurrent(epoch)) return;
          this.options.ui.setStatus(message);
          loading?.update(message);
          loading?.phase(.3);
        }, { signal: controller.signal });
      }
      if (!this.isCurrent(epoch)) return;
      this.emit('skeleton-ready', epoch, { cacheHit: prepared.cacheHit, reused: !!cached });
      loading?.phase(.55);
      await this.mount(prepared, options, signature, epoch, controller.signal, loading);
    } catch (error) {
      if (!this.isCurrent(epoch)) return;
      if (isAbortError(error)) {
        this.emit('cancelled', epoch);
        return;
      }
      this.emit('failed', epoch, { detail: error instanceof Error ? error.message : String(error) });
      this.options.ui.setError(error instanceof Error ? error.message : String(error));
    } finally {
      if (this.isCurrent(epoch)) {
        loading?.cancel();
        this.loading = undefined;
        this.busyState = false;
        this.abort = undefined;
        this.options.ui.setBusy(false);
      }
    }
  }

  /** 参数变化中断任务，保留当前地图 */
  invalidate(): void {
    if (this.disposed) return;
    if (this.busyState || this.abort || this.loading) this.emit('cancelled', this.epoch, { detail: 'invalidated' });
    this.epoch++;
    this.abort?.abort();
    this.abort = undefined;
    this.loading?.cancel();
    this.loading = undefined;
    this.busyState = false;
    this.options.ui.setBusy(false);
    this.reusable = undefined;
  }

  destroy(): void {
    if (this.disposed) return;
    this.disposed = true;
    this.epoch++;
    this.abort?.abort();
    this.abort = undefined;
    this.loading?.cancel();
    this.loading = undefined;
    this.atlasView?.destroy();
    this.atlasView = null;
    this.artificialWorld?.dispose();
    this.artificialWorld = undefined;
    this.generatorWorld = null;
    this.reusable = undefined;
    this.busyState = false;
    this.options.ui.setBusy(false);
  }

  private async mount(prepared: PreparedWorld, options: StartOptions, signature: string, epoch: number,
    signal: AbortSignal, loading?: AtlasLoadingHandle): Promise<void> {
    const spawn = prepared.spawn;
    const spawnMarker = { x: spawn.cx * CHUNK_SIZE + CHUNK_SIZE / 2 + .5,
      z: spawn.cz * CHUNK_SIZE + CHUNK_SIZE / 2 + .5, yaw: 0 };
    const artificial = new ArtificialWorld(prepared.generator, options.unixMs, options.calendarType, options.utcOffsetMinutes, prepared.artificialState);
    if (this.options.playMode === 'survival') artificial.useCachedQueries();
    try {
      if (artificial.settings.enabled && this.options.playMode !== 'survival') {
        artificial.enableBackground(this.createArtificialWorker(), options.unixMs ?? 0, spawn.cx, spawn.cz, prepared.generator.exposeHydrology(), () => {
          if (this.isCurrent(epoch)) this.atlasView?.requestRender();
        });
      }
    } catch (error) { artificial.dispose(); throw error; }
    if (!this.isCurrent(epoch)) { artificial.dispose(); return; }

    this.options.ui.showMap();
    this.generatorWorld = prepared.generator;
    const oldView = this.atlasView;
    this.atlasView = null;
    oldView?.destroy();
    let view: PreviewAtlas;
    try {
      view = this.createAtlas({
        maxZoomLevel: 8,
        canvas: this.options.ui.canvas,
        survivalPreview: this.options.playMode === 'survival',
        worldKey: this.options.worldKey,
        initialCenter: spawn,
        playerMarker: () => spawnMarker,
        generator: prepared.generator,
        artificial,
        onStatsUpdate: stats => { if (this.isCurrent(epoch)) this.options.hooks.onStatsUpdate(stats); },
      });
    } catch (error) {
      artificial.dispose();
      this.artificialWorld?.dispose();
      this.artificialWorld = undefined;
      this.generatorWorld = null;
      throw error;
    }
    this.atlasView = view;
    this.artificialWorld?.dispose();
    this.artificialWorld = artificial;

    try {
      const controls = this.options.ui.readControls();
      view.centerAt(spawn.cx, spawn.cz);
      view.setShowChunkInfo(false);
      view.setShowChunkColors(false);
      view.setShowSatellite(false);
      view.setMapLayers(controls.layers.map(layer => ({ ...layer })));
      view.setContourDensity(controls.density);
      view.setShowFlowArrows(false);
      view.setShowClimate(false);
      if (options.generation.mode === 'planet' && this.options.playMode !== 'survival') {
        view.setPlanetView(controls.planetView);
        view.showOverview();
      }
      view.requestRender();
      this.emit('map-visible', epoch);
      loading?.phase(.8);

      // 首帧提交后关闭遮罩
      await nextFrame(signal);
      if (!this.isCurrent(epoch)) return;
      if (loading) {
        loading.phase(1);
        await loading.complete();
      }
      if (!this.isCurrent(epoch)) return;
      this.emit('overlay-closed', epoch);
      this.options.ui.setStatus(t(this.options.playMode==='survival'?'creator.survivalMapReady':'creator.mapReady', { seed: options.seed }));
      this.reusable = { signature, prepared };
      this.emit('ready', epoch);
    } catch (error) {
      if (this.atlasView === view) {
        this.atlasView = null;
        view.destroy();
        artificial.dispose();
        this.artificialWorld = undefined;
        this.generatorWorld = null;
      }
      throw error;
    }
  }

  private isCurrent(epoch: number): boolean {
    return !this.disposed && this.epoch === epoch;
  }

  private emit(phase: PreviewPhase, requestId: number, extra: { cacheHit?: boolean; reused?: boolean; detail?: string } = {}): void {
    if (this.disposed) return;
    const timing: PreviewTiming = { phase, at: this.now(), requestId, ...extra };
    this.timings.push(timing);
    try { this.options.hooks.onPhase?.(timing); } catch (error) { console.warn('[Preview timing]', error); }
  }
}
