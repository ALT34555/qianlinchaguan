import {TileStore} from './tile-plan.mjs';
import {isStaticLayer, STATIC_LAYER_LEVEL} from './static-layers.mjs';

// 瓦片调度器：优先级、取消与Worker池
export class TileScheduler {
  constructor(world, options = {}) {
    this.world = world;
    this.owner = options.owner ?? {requestRender() {}, refinementReady: true, active: true};
    this.store = options.store ?? new TileStore(options.budget ?? 48 * 2 ** 20);
    this.failedTiles = new Set();
    this.queue = [];
    this.serial = 0;
    this.generation = 1;
    this.version = 0;
    this.baseVersion = 0;
    this.paused = false;
    this.progress = null;
    this.stats = null;
    this.focusId = null;
    this.plan = null;
    this.conditionKey = '';
    this.context = {};
    this.disposed = false;
    this.slots = [];
    this.explicitConcurrency = options.concurrency;
    this.pumpScheduled = false;

    this.initWorker();
  }

  get maxWorkers() {
    if (this.explicitConcurrency) return this.explicitConcurrency;
    const native = typeof globalThis.Worker === 'function' && /\[native code\]/.test(Function.prototype.toString.call(globalThis.Worker));
    if (!native) return 1;
    return Math.min(8, Math.max(2, (globalThis.navigator?.hardwareConcurrency ?? 8) - 1));
  }

  get worker() { return this.slots[0]?.worker; }
  set worker(w) { if (this.slots[0]) this.slots[0].worker = w; else this.slots[0] = {worker: w, inFlight: null, cancelling: false}; }

  get inFlight() { return this.slots.find(s => s.inFlight)?.inFlight ?? this._inFlight; }
  set inFlight(v) {
    this._inFlight = v;
    if (!v) for (const s of this.slots) s.inFlight = null;
    else if (this.slots[0]) this.slots[0].inFlight = v;
  }

  get cancelling() { return this.slots.some(s => s.cancelling) || !!this._cancelling; }
  set cancelling(v) {
    this._cancelling = v;
    if (!v) for (const s of this.slots) s.cancelling = false;
    else if (this.slots[0]) this.slots[0].cancelling = true;
  }

  spawnSlot() {
    const worker = new Worker(new URL('./tiles.worker.ts', import.meta.url), {type: 'module'});
    const slot = {worker, inFlight: null, cancelling: false};
    worker.onmessage = ({data: m}) => {
      const active = slot.inFlight ?? this._inFlight;
      if (this.disposed || !active || m.id !== active.id) return;
      if (m.generation && m.generation !== this.generation) return;
      if (m.waterKey !== undefined && m.waterKey !== this.waterKey) return;
      if (m.kind === 'progress') {
        this.progress = m;
        this.owner.requestRender?.();
        return;
      }
      const discarded = slot.cancelling || this._cancelling;
      this.progress = null;
      slot.inFlight = null;
      if (this._inFlight?.id === m.id) this._inFlight = null;
      slot.cancelling = false;
      if (!this.slots.some(s => s.cancelling)) this._cancelling = false;
      if (m.kind === 'cancelled' || discarded) {
        this.pump();
        this.owner.requestRender?.();
        return;
      }

      if (m.kind === 'tile') {
        this.lastCompletedKey = m.tile.key;
        const tile = {...m.tile, pixels: m.pixels, side: m.side, revision: m.id,
          nearest: m.layer === 'base' && (m.logicalLevel ?? m.tile.z) >= 12,
          persistent: isStaticLayer(m.layer) && m.tile.z === STATIC_LAYER_LEVEL};
        if (m.layer === 'base') this.baseVersion++;
        this.store.set(m.style, tile);
        this.version++;
        this.stats = m.stats;
      } else if (m.kind === 'error') {
        this.failedTiles.add(`${m.style}:${m.tile.key}`);
        this.version++;
      }
      if (!this.paused) {
        this.pump();
        this.owner.requestRender?.();
      }
    };
    worker.onerror = () => {
      this.failed = true;
      this.progress = null;
      this.inFlight = null;
      this.queue = [];
      for (const s of this.slots) { try { s.worker.terminate(); } catch {} }
      this.owner.requestRender?.();
    };
    const generator = this.owner.generator;
    worker.postMessage({kind: 'world', world: this.world, generation: this.generation, water: this.waterObj, waterKey: this.waterKey,
      generatorOptions: generator ? {seed: generator.seed, climateWeights: generator.climateWeights, generation: generator.generation} : undefined});
    this.slots.push(slot);
    return slot;
  }

  initWorker() {
    this.slots = [];
    this._inFlight = undefined;
    this._cancelling = false;
    this.waterObj = this.waterState();
    this.waterKey = JSON.stringify(this.waterObj);
    this.spawnSlot();
  }

  waterState() {
    const generator = this.owner.generator;
    return {overrides: generator?.getWaterAppearanceOverrides?.() ?? [], freezeRules: generator?.getWaterFreezeRules?.() ?? {}, season: generator?.getWaterSeason?.() ?? null};
  }
  syncWater() {
    const water = this.waterState(), key = JSON.stringify(water);
    if (key === this.waterKey) return;
    this.waterObj = water; this.waterKey = key;
    this.cancel(); this.inFlight = null; this.cancelling = false; this.queue = []; this.failedTiles.clear(); this.store.clear(); this.version++; this.baseVersion++;
    for (const s of this.slots) s.worker.postMessage({kind: 'water', generation: this.generation, water, waterKey: key});
  }

  setWorld(world) {
    for (const s of this.slots) { try { s.worker.terminate(); } catch {} }
    this.world = world;
    this.generation++;
    this.failed = false;
    this.failedTiles.clear();
    this.store.clear();
    this.queue = [];
    this.inFlight = null;
    this.progress = null;
    this.cancelling = false;
    this.focusId = null;
    this.plan = null;
    this.lastCompletedKey = null;
    this.version++;
    this.baseVersion++;
    this.initWorker();
    this._inFlight = null;
  }

  setPaused(paused) {
    if (this.paused === paused) return;
    this.paused = paused;
    if (paused) {
      this.queue = [];
      this.cancel();
    }
  }

  cancelSlot(slot) {
    if (slot.inFlight && !slot.cancelling) {
      slot.cancelling = true;
      slot.worker.postMessage({kind: 'cancel', generation: this.generation});
    }
  }

  cancel() {
    for (const s of this.slots) this.cancelSlot(s);
  }

  // 优先级队列：底图 > 激活图层 > 预取瓦片
  updateQueue(jobs, protectedIds) {
    this.store.protectedKeys = protectedIds;
    this.store.trim();

    const all = [...jobs.values()];
    const visibleBase = all.filter(j => j.layer === 'base' && !j.tile.prefetch);
    const visibleOverlay = all.filter(j => j.layer !== 'base' && !j.tile.prefetch)
      .sort((a, b) => (a.tile.distance ?? 0) - (b.tile.distance ?? 0) || a.tile.key.localeCompare(b.tile.key));
    const prefetch = all.filter(j => j.tile.prefetch)
      .sort((a, b) => (a.tile.distance ?? 0) - (b.tile.distance ?? 0));

    this.queue = [...visibleBase, ...visibleOverlay, ...prefetch];

    for (const slot of this.slots) {
      const active = slot.inFlight;
      if (!active) continue;
      const isOutdatedFocus = active.tile.z >= 13 && active.focusId !== this.focusId;
      const isUnprotected = !protectedIds.has(this.store.id(active.style, active.tile.key));
      if (isOutdatedFocus || isUnprotected) this.cancelSlot(slot);
    }
  }

  // 每帧最多新建一个Worker，避免首帧集中克隆世界
  schedulePump() {
    if (this.pumpScheduled || this.disposed) return;
    this.pumpScheduled = true;
    const run = () => {
      this.pumpScheduled = false;
      if (!this.disposed) this.pump();
    };
    if (typeof requestAnimationFrame === 'function') requestAnimationFrame(run);
    else setTimeout(run, 0);
  }

  pump(context) {
    if (context) this.context = context;
    context = this.context;
    if (this.disposed || this.failed || this.paused || !this.owner.active) return;
    const maxW = this.maxWorkers;
    let spawned = false;
    while (this.queue.length) {
      let slot = this.slots.find(s => !s.inFlight && !(maxW === 1 && this._inFlight));
      if (!slot && this.slots.length < maxW) {
        if (spawned) break;
        slot = this.spawnSlot();
        spawned = true;
      }
      if (!slot) break;

      const adjacent = this.lastCompletedKey == null ? -1 : this.queue.findIndex(j => j.tile?.key === this.lastCompletedKey && j.layer !== 'base' && !j.tile.prefetch);
      if (adjacent > 0) this.queue.unshift(this.queue.splice(adjacent, 1)[0]);
      const raw = this.queue.shift();
      const tile = raw.tile || raw;
      const layer = raw.layer || 'base';
      const style = raw.style || `${layer}:${tile.key}`;

      const refinementReady = this.owner.refinementReady ?? true;
      const currentZ = this.plan?.z ?? tile.z;

      // 预取不抢占未就绪细化与低倍大湖
      if (tile.prefetch && (!refinementReady || (currentZ < 13 && tile.z >= 13) || (currentZ <= 15 && tile.z >= 16))) continue;
      if (tile.prefetch && currentZ <= 9 && tile.z >= 10) continue;

      if (this.store.has(style, tile.key) || this.failedTiles.has(`${style}:${tile.key}`)) continue;
      if (this.slots.some(s => s.inFlight && s.inFlight.style === style && s.inFlight.tile.key === tile.key)) continue;

      const job = {
        id: ++this.serial,
        tile,
        layer,
        style,
        focusId: this.focusId,
        generation: this.generation,
      };
      slot.inFlight = job;
      slot.cancelling = false;

      slot.worker.postMessage({
        kind: 'tile',
        ...job,
        generation: this.generation,
        waterKey: this.waterKey,
        logicalLevel: context.logicalLevel ?? this.plan?.z ?? tile.z,
        palette: context.palette ?? (this.owner.showSatellite ? 'satellite' : 'biome'),
        sunHours: context.sunHours ?? this.owner.sunHours,
        contourUnit: context.contourUnit ?? 0,
        sunWorld: context.sunWorld ?? this.owner.sunWorld?.(),
      });
    }
    if (!this.inFlight) this.cancelling = false;
    if (spawned && this.queue.length && this.slots.length < maxW) this.schedulePump();
  }

  dispose() {
    this.disposed = true;
    this.queue = [];
    this.pumpScheduled = false;
    this.inFlight = undefined;
    this.store.clear();
    for (const s of this.slots) {
      try { s.worker.terminate(); } catch {}
    }
    this.slots = [];
  }
}
