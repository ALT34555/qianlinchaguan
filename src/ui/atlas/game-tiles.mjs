import {TAU, LIMIT, clamp, project, detailLevel, unproject, levelFor, atlasCamera} from './tiles.mjs';
import {expandTriangle} from './tile-seams.mjs';
import {ancestor, planTiles} from './tile-plan.mjs';
import {contourUnit} from './contour.mjs';
import {TileScheduler} from './tile-scheduler.mjs';
import {LayerCompositor} from './layer-compositor.mjs';
import {drawRiverLayers} from './river-vectors.mjs';

const layers = new WeakMap();
import {isStaticLayer} from './static-layers.mjs';
import {macroBackdrop} from './macro-backdrop.mjs';
import {climatePlanTiles} from './climate-plan.mjs';
import {staticResidency, planLayerResidency} from './layer-residency.mjs';

// 底图换挡交叉淡化时长
const FADE_MS = 320;

// 游戏瓦片管理：调度与合成外观
export class GameTiles {
  constructor(view, world) {
    this.owner = view;
    this.world = world;
    this.state = {layers: [], density: 2};
    this.scheduler = new TileScheduler(world, {owner: view});
    this.compositor = new LayerCompositor();
  }

  get store() { return this.scheduler.store; }
  set store(s) { this.scheduler.store = s; }
  get failedTiles() { return this.scheduler.failedTiles; }
  get queue() { return this.scheduler.queue; }
  set queue(q) { this.scheduler.queue = q; }
  get inFlight() { return this.scheduler.inFlight; }
  set inFlight(f) { this.scheduler.inFlight = f; }
  get version() { return this.scheduler.version; }
  set version(v) { this.scheduler.version = v; }
  get baseVersion() { return this.scheduler.baseVersion; }
  set baseVersion(v) { this.scheduler.baseVersion = v; }
  get serial() { return this.scheduler.serial; }
  set serial(s) { this.scheduler.serial = s; }
  get cancelling() { return this.scheduler.cancelling; }
  set cancelling(c) { this.scheduler.cancelling = c; }
  get paused() { return this.scheduler.paused; }
  set paused(p) { this.scheduler.paused = p; }
  get failed() { return this.scheduler.failed; }
  set failed(f) { this.scheduler.failed = f; }
  get progress() { return this.scheduler.progress; }
  set progress(p) { this.scheduler.progress = p; }
  get stats() { return this.scheduler.stats; }
  set stats(s) { this.scheduler.stats = s; }
  get worker() { return this.scheduler.worker; }
  set worker(w) { this.scheduler.worker = w; }
  get plan() { return this.scheduler.plan; }
  set plan(p) { this.scheduler.plan = p; }
  get focusId() { return this.scheduler.focusId; }
  set focusId(id) { this.scheduler.focusId = id; }

  get projections() { return this.compositor ? this.compositor.projections : (this._projections ??= new Map()); }
  set projections(p) { if (this.compositor) this.compositor.projections = p; else this._projections = p; }
  get screenGrid() { return this.compositor ? this.compositor.screenGrid : this._screenGrid; }
  set screenGrid(g) { if (this.compositor) this.compositor.screenGrid = g; else this._screenGrid = g; }
  get daylight() { return this.compositor ? this.compositor.daylight : this._daylight; }
  set daylight(d) { if (this.compositor) this.compositor.daylight = d; else this._daylight = d; }

  setLayers(state) { this.state = state ?? this.state; }
  palette() { return this.owner.showSatellite ? 'satellite' : 'biome'; }
  unit() { return this.viewUnit ?? 0; }

  styleFor(id) {
    const level = this.renderLevel ?? this.logicalLevel;
    const riverLevel=this.logicalLevel??level??2;
    // 底图不按层级分档，祖先直接填补视野
    if (id === 'base') return `base:${this.palette()}:${riverLevel<7?'macro-rivers':riverLevel>=17?'water-split':'rivers'}`;
    if (id === 'contour') return `contour:L${this.logicalLevel}:${this.unit()}`;
    if (id === 'daylight') return `daylight:${(this.owner.sunHours ?? 10.5).toFixed(2)}`;
    if (isStaticLayer(id)) return `${id}:${this.world.fingerprint ?? ''}`;
    return `layer:${id}`;
  }

  focused(style, tile) {
    return tile && tile.z >= 13 ? `${style}:lake:${this.focusId ?? 'none'}` : style;
  }

  setPaused(paused) {
    this.scheduler.setPaused(paused);
    if (paused) this.camera = null;
  }

  scale() { return this.state?.scale; }
  enabledLayers() { return (this.state.layers ?? []).filter(l => l.on); }

  pump() {
    this.scheduler.pump({
      palette: this.palette(),
      logicalLevel:this.renderLevel ?? this.logicalLevel,
      sunHours: this.owner.sunHours,
      contourUnit: this.unit(),
      sunWorld: this.owner.sunWorld?.(),
    });
  }

  cancel() {
    this.scheduler.cancel();
  }

  draw(ctx, state) {
    state = {...state, rasterBudget: this.owner.refinementReady === false ? 90000 : 600000};
    this.state = {...this.state, ...state};
    this.logicalLevel=levelFor(state);
    this.scheduler.syncWater();
    this.view = state.view;
    this.width = state.width;
    this.height = state.height;
    this.state.scale = state.scale;

    const focus = this.owner.labAtlasFocus ?? {x: state.width / 2, y: state.height / 2};
    this.focusId=null;
    this.scheduler.focusId = this.focusId;

    const enabled = this.enabledLayers();
    const level = levelFor(state);
    this.viewUnit = enabled.some(l => l.id === 'contour') ? this.contourUnitFor(state, level) : 0;

    const key = JSON.stringify(state);
    const focusKey = `${focus.x}/${focus.y}/${this.focusId}`;
    const styleKeyNow = [this.palette(), this.viewUnit, ...enabled.map(l => l.id)].join('|');

    if (this.camera !== key || this.planStyle !== styleKeyNow || this.planVersion !== this.version || this.focusKey !== focusKey) {
      const failed = new Set([...this.failedTiles].filter(k => k.startsWith(this.styleFor('base') + ':')).map(k => k.split(':').at(-1)));
      this.camera = key;
      this.planStyle = styleKeyNow;
      this.planVersion = this.version;
      this.focusKey = focusKey;

      const fixed = staticResidency(state, enabled, this.store, id => this.styleFor(id));
      const maxBaseTiles = Math.max(8, Math.min(512, Math.floor((this.store.budget * .86 - fixed.bytes) / (128 * 128 * 4 * (this.logicalLevel>=17?2.4:1.4)))));
      this.plan = planTiles(state, k => this.store.has(this.focused(this.styleFor('base'), {z: Number(k.split('/')[0])}), k), failed, focus, maxBaseTiles, level);
      this.scheduler.plan = this.plan;
      this.renderLevel = this.plan.z;
      this.residency = planLayerResidency(this.plan, enabled, fixed, this.store.budget);

      const jobs = new Map(), protectedIds = new Set();
      const add = (layer, tile) => {
        const style = this.focused(this.styleFor(layer, tile), tile);
        const id = this.store.id(style, tile.key);
        protectedIds.add(id);
        if (!this.store.has(style, tile.key) && !this.failedTiles.has(`${style}:${tile.key}`)) {
          jobs.set(id, {layer, tile, style});
        }
      };

      for (const tile of this.plan.queue.filter(t => !t.prefetch)) add('base', tile);
      for (const [id, targets] of this.residency.targets) for (const tile of targets) add(id, tile);
      for (const tile of this.plan.queue.filter(t => t.prefetch)) add('base', tile);
      for (const k of this.plan.protectedKeys) {
        protectedIds.add(this.store.id(this.focused(this.styleFor('base'), {z: Number(k.split('/')[0])}), k));
      }

      this.scheduler.updateQueue(jobs, protectedIds);
    }

    const tiles = this.plan.tiles;
    const riverTiles=new Map();
    for(const tile of tiles)for(let z=tile.z;z>=2;z--){
      const p=ancestor(tile,z),cached=this.store.get(this.focused(this.styleFor('base'),p),p.key);
      if(cached){riverTiles.set(p.key,cached);break;}
    }
    const basePolicy = this.renderLevel >= 18 ? 'surface' : 'continuous';
    if (this.basePolicy !== undefined && this.basePolicy !== basePolicy && this.background?.width) {
      this.fadeCanvas = this.background;
      this.fadeStart = performance.now();
      this.background = document.createElement('canvas');
      this.backgroundKey = null;
    }
    this.basePolicy = basePolicy;
    const cacheKey = `${key}:${this.palette()}:${this.focusId}:${this.baseVersion}`;
    const output = ctx;

    if (this.backgroundKey !== cacheKey) {
      this.background ??= document.createElement('canvas');
      if (this.background.width !== Math.ceil(state.width) || this.background.height !== Math.ceil(state.height)) {
        this.background.width = Math.ceil(state.width);
        this.background.height = Math.ceil(state.height);
      }
      ctx = this.background.getContext('2d');
      ctx.clearRect(0, 0, state.width, state.height);
      const backdropKey = `${key}:${this.palette()}`;
      const backdropStyle=`${this.palette()}:${basePolicy}`;
      if (this.backdropPalette !== backdropStyle) {
        this.backdropTiles = macroBackdrop(this.world, this.palette());
        this.backdropPalette = backdropStyle;
      }
      if (this.backdropTiles.length) ctx.drawImage(this.projectLayer('backdrop', this.backdropTiles, state, backdropKey), 0, 0, state.width, state.height);
      ctx.save();
      if (state.view === 'globe') {
        ctx.beginPath();
        ctx.arc(state.width / 2, state.height / 2, state.scale, 0, TAU);
        ctx.clip();
      }
      const bases = new Map();
      for (const tile of tiles) {
        for (let z = tile.z; z >= 2; z--) {
          const p = ancestor(tile, z);
          const cached = this.store.get(this.focused(this.styleFor('base'), p), p.key);
          if (cached) {
            bases.set(p.key, cached);
            break;
          }
        }
      }
      const sources = [...bases.values()].sort((a, b) => a.z - b.z);
      if (sources.length) ctx.drawImage(this.projectLayer('base', sources, state, cacheKey), 0, 0, state.width, state.height);
      ctx.restore();
      this.backgroundKey = cacheKey;
    }

    output.drawImage(this.background, 0, 0, state.width, state.height);
    if (this.fadeCanvas) {
      const t = (performance.now() - this.fadeStart) / FADE_MS;
      if (t >= 1) this.fadeCanvas = null;
      else {
        output.save();
        output.globalAlpha = 1 - t;
        output.drawImage(this.fadeCanvas, 0, 0, state.width, state.height);
        output.restore();
        this.owner.requestRender?.();
      }
    }
    ctx = output;
    ctx.save();
    if (state.view === 'globe') {
      ctx.beginPath();
      ctx.arc(state.width / 2, state.height / 2, state.scale, 0, TAU);
      ctx.clip();
    }

    const riverSources=[...riverTiles.values()];
    const decorative=this.scheduler.tributaryVectors?.(tiles)??[];
    drawRiverLayers(ctx,state,this.world.circumference,riverSources,()=>{
      const water=riverSources.filter(t=>t.waterPixels).map(t=>({...t,pixels:t.waterPixels}));
      if(water.length)ctx.drawImage(this.projectLayer('river-water',water,state,`${cacheKey}:water`),0,0,state.width,state.height);
      else this.projections.delete('river-water');
    },decorative);
    this.compositor.draw(ctx, state, enabled, {
      tiles,
      store: this.store,
      focusId: this.focusId,
      owner: this.owner,
      world: this.world,
      key,
      styleFor: id => this.styleFor(id),
      compositor: this,
      layerTargets: this.residency.targets,
      climateTiles: enabled.some(l => l.id === 'climate') ? climatePlanTiles(this.world) : [],
    });

    ctx.restore();
    this.pump();

    const overlays = [...this.residency.targets].flatMap(([id, targets]) => targets.map(tile => ({id, tile})));
    return {
      overlaysLoaded: overlays.filter(({id, tile}) => this.store.has(this.focused(this.styleFor(id), tile), tile.key)).length,
      overlaysTotal: overlays.length,
      loaded: tiles.filter(t => this.store.has(this.focused(this.styleFor('base'), t), t.key)).length,
      total: tiles.length,
      z: this.plan.z,
      shore: this.progress,
      focusId: this.focusId,
    };
  }

  projectLayer(id, tiles, state, key) {
    if (!this.compositor) {
      this.compositor = new LayerCompositor();
      if (this._projections) this.compositor.projections = this._projections;
      if (this._screenGrid) this.compositor.screenGrid = this._screenGrid;
    }
    return this.compositor.projectLayer(id, tiles, state, key);
  }

  daylightImage(state, key, hours, world) {
    return this.compositor.daylightImage(state, key, hours, world);
  }

  contourUnitFor(_state, level) {
    return contourUnit(level,this.world.columns);
  }

  dispose() {
    this.scheduler.dispose();
    this.compositor.dispose();
    if(this.background){this.background.width=0;this.background.height=0;}
    if(this.fadeCanvas){this.fadeCanvas.width=0;this.fadeCanvas.height=0;}
    this.background=undefined;this.fadeCanvas=undefined;this.backdropTiles=[];this.plan=null;this.residency=null;this.camera=null;
  }
}

export function drawGameAtlas(v) {
  if (!v.active) return true;
  if (!v.isPlanet || v.showClimate || v.showChunkColors) {
    suspendGameAtlas(v, true);
    return false;
  }
  const world = v.generator.exposeHydrology()?.planetMacro;
  if (!world) return false;
  let layer = layers.get(v);
  if (layer?.world !== world) {
    layer?.dispose?.();
    layer = new GameTiles(v, world);
    layers.set(v, layer);
  }
  layer.setPaused(false);

  layer.setLayers({layers: v.getMapLayers?.() ?? [], density: v.getContourDensity?.() ?? 2});
  const dpr = Math.min(window.devicePixelRatio || 1, 2);
  const width = v.canvas.width / dpr, height = v.canvas.height / dpr;
  const cell = v.baseCellSize * v.scale, size = v.generator.generation.planet.equatorChunks;
  const state = atlasCamera(v, width, height);
  const ctx = v.ctx;
  ctx.save();
  ctx.scale(dpr, dpr);
  ctx.fillStyle = '#241c13';
  ctx.fillRect(0, 0, width, height);
  v.surfaceCache.beginFrame();
  const progress = layer.draw(ctx, state);
  if (!v.isGlobe && cell >= 24) {
    const left = Math.floor(v.centerCx + (-width / 2 - v.panX) / cell);
    const right = Math.ceil(v.centerCx + (width / 2 - v.panX) / cell);
    const top = Math.floor(v.worldZ(v.mapZ(v.centerCz) + (-height / 2 - v.panY) / cell));
    const bottom = Math.ceil(v.worldZ(v.mapZ(v.centerCz) + (height / 2 - v.panY) / cell));
    const coords = v.centerCells(left, right, top, bottom, v.centerCx - v.panX / cell, v.worldZ(v.mapZ(v.centerCz) - v.panY / cell))
      .slice(0, 192)
      .filter(([, z]) => z >= -size / 4 && z < size / 4)
      .map(([x, z]) => [((x + size / 2) % size + size) % size - size / 2, z])
      .filter(([x,z])=>!v.exploration||v.exploration.has(x,z));
    drawAtlasDetails(v, ctx, state, coords, cell, size);
  }
  v.surfaceCache.endFrame();
  const at = v.getCenterCoordinates();
  v.onStatsUpdate?.({
    centerCx: at.cx,
    centerCz: at.cz,
    scale: v.scale,
    visibleChunks: 0,
    climateCounts: [0, 0, 0, 0, 0],
    climatePercents: [0, 0, 0, 0, 0],
    matchedCount: 0,
    matchedPercent: 0,
    activeFilterName: '',
    level: v.isGlobe ? 'globe' : 'overview',
  });
  layer.displayProgress = progress;
  ctx.restore();
  return true;
}

export function drawGameAtlasLegend(v) {
  const layer = layers.get(v);
  if (!v.active || layer?.paused || !layer?.displayProgress) return;
  const {width, height} = layer.state ?? {};
  if (!width) return;
  const progress = layer.displayProgress;
  const meters = layer.world.radius / layer.state.scale * (v.isMercator ? Math.cos(layer.state.lat) : 1);
  const ctx = v.ctx, dpr = Math.min(window.devicePixelRatio || 1, 2);
  ctx.save();
  ctx.scale(dpr, dpr);
  const target = meters * 100, unit = 10 ** Math.floor(Math.log10(target));
  const length = [1, 2, 5, 10].map(n => n * unit).filter(n => n <= target).pop() ?? unit;
  ctx.fillStyle = 'rgba(27,24,18,.78)';
  ctx.fillRect(12, height - 90, progress.shore ? 340 : 256, 52);
  ctx.fillStyle = '#eadfc0';
  ctx.strokeStyle = '#eadfc0';
  ctx.font = '12px "Qianlin Song",serif';
  ctx.textAlign = 'left';
  ctx.fillText(
    `${detailLevel(meters)} L${layer.logicalLevel} · ${progress.loaded}/${progress.total}${progress.overlaysTotal ? ` · 图层 ${progress.overlaysLoaded}/${progress.overlaysTotal}` : ''}${progress.shore ? ' · ' + (progress.shore.stage ?? '湖形计算中') : progress.z >= 13 && progress.focusId !== null ? ' · 聚焦湖 ' + progress.focusId : ''}${layer.failedTiles.size ? ' · 部分细节失败' : ''}`,
    20,
    height - 74
  );
  ctx.fillText(
    (length >= 1000 ? `${length / 1000} km` : `${length} m`) + (layer.unit() > 0 ? ` · 等高距 ${layer.unit()} m` : '') + (progress.shore ? ` · 已采样 ${Math.round(progress.shore.sampled / 1000)} 千点` : ''),
    20,
    height - 56
  );
  ctx.beginPath();
  ctx.moveTo(20, height - 50);
  ctx.lineTo(20 + length / meters, height - 50);
  ctx.stroke();
  ctx.restore();
}

function drawAtlasDetails(v, ctx, state, coords, cell, size) {
  for (const [cx, cz] of coords) {
    const wrapped = cx + Math.round((v.centerCx - v.panX / cell - cx) / size) * size;
    const x = state.width / 2 + v.panX + (wrapped - v.centerCx) * cell;
    const y = state.height / 2 + v.panY + (v.mapZ(cz) - v.mapZ(v.centerCz)) * cell;
    const h = (v.mapZ(cz + 1) - v.mapZ(cz)) * cell;
    if ((v.showChunkInfo||v.filterTypeId!==null||v.filterCategory!==null) && v.refinementReady) {
      const tile = v.surfaceCache.get(cx, cz, (x - state.width / 2) ** 2 + (y - state.height / 2) ** 2, false, false, true);
      const info = tile?.summary?.info ?? v.generator.cachedChunkInfo(cx, cz);
      if(info&&(v.filterTypeId!==null||v.filterCategory!==null)&&v.isChunkMatched?.(info)){
        ctx.strokeStyle='#c2333f';ctx.lineWidth=1;ctx.strokeRect(x,y,cell,h);
      }
      if (info && v.showChunkInfo&&cell >= 36) {
        ctx.fillStyle = '#eadfc0';
        ctx.font = '10px "Qianlin Song",serif';
        ctx.fillText(String(info.type), x + 3, y + 12);
        if (v.showFlowArrows) v.drawFlowArrow(ctx, info, x, y, cell, h);
      }
    }
    if (
      (v.hoveredChunk?.cx === cx && v.hoveredChunk?.cz === cz) ||
      (v.selectedChunk?.cx === cx && v.selectedChunk?.cz === cz)
    ) {
      ctx.strokeStyle = v.selectedChunk?.cx === cx && v.selectedChunk?.cz === cz ? '#eac876' : '#ffffff';
      ctx.lineWidth = 1;
      ctx.strokeRect(x, y, cell, h);
    }
  }
}

export function suspendGameAtlas(v, paused) {
  layers.get(v)?.setPaused(paused);
}

export function disposeGameAtlas(v) {
  const layer = layers.get(v);
  if (layer) {
    layer.dispose();
    layers.delete(v);
  }
}

class TileMesh {
  drawTile(ctx, tile, view) {
    const span = TAU / 2 ** tile.z, lon0 = tile.x * span - Math.PI, lat0 = Math.PI / 2 - tile.y * span;
    const steps = this.view === 'globe' ? clamp(Math.ceil(span * this.scale() / 32), 1, 24) : this.view === 'mercator' ? 8 : 1, side = tile.canvas.width - 2;
    const center = project(view, lon0 + span / 2, clamp(lat0 - span / 2, -LIMIT, LIMIT), false);
    const vertex = (x, y) => {
      const lat = clamp(lat0 - y * span, view.view === 'mercator' ? -LIMIT : -Math.PI / 2, view.view === 'mercator' ? LIMIT : Math.PI / 2);
      const p = project(view, lon0 + x * span, lat);
      if (p && this.view !== 'globe') p.x = center.x + (x - 0.5) * span * view.scale;
      return p;
    };
    for (let y = 0; y < steps; y++) for (let x = 0; x < steps; x++) {
      const u = x / steps, v = y / steps, d = 1 / steps;
      const a = vertex(u, v), b = vertex(u + d, v), c = vertex(u, v + d), e = vertex(u + d, v + d);
      this.triangle(ctx, tile.canvas, [a, b, c], [[u * side, v * side], [(u + d) * side, v * side], [u * side, (v + d) * side]]);
      this.triangle(ctx, tile.canvas, [b, e, c], [[(u + d) * side, v * side], [(u + d) * side, (v + d) * side], [u * side, (v + d) * side]]);
    }
  }
  triangle(ctx, image, p, uv) {
    uv = uv.map(([x, y]) => [x + 1, y + 1]);
    if (p.some(v => !v)) return;
    if (this.view !== 'globe' && Math.max(...p.map(v => v.x)) - Math.min(...p.map(v => v.x)) > this.scale() * Math.PI) return;
    if (Math.max(...p.map(v => v.x)) < 0 || Math.min(...p.map(v => v.x)) > this.width || Math.max(...p.map(v => v.y)) < 0 || Math.min(...p.map(v => v.y)) > this.height) return;
    const [a, b, c] = p, [u, v, w] = uv, det = (v[0] - u[0]) * (w[1] - u[1]) - (v[1] - u[1]) * (w[0] - u[0]);
    if (!det) return;
    const ax = ((b.x - a.x) * (w[1] - u[1]) - (c.x - a.x) * (v[1] - u[1])) / det;
    const ay = ((b.y - a.y) * (w[1] - u[1]) - (c.y - a.y) * (v[1] - u[1])) / det;
    const bx = ((c.x - a.x) * (v[0] - u[0]) - (b.x - a.x) * (w[0] - u[0])) / det;
    const by = ((c.y - a.y) * (v[0] - u[0]) - (b.y - a.y) * (w[0] - u[0])) / det;
    const [ca, cb, cc] = expandTriangle(p);
    ctx.save();
    ctx.beginPath();
    ctx.moveTo(ca.x, ca.y);
    ctx.lineTo(cb.x, cb.y);
    ctx.lineTo(cc.x, cc.y);
    ctx.closePath();
    ctx.clip();
    ctx.transform(ax, ay, bx, by, a.x - ax * u[0] - bx * u[1], a.y - ay * u[0] - by * u[1]);
    ctx.drawImage(image, 0, 0);
    ctx.restore();
  }
}

GameTiles.prototype.drawTile = TileMesh.prototype.drawTile;
GameTiles.prototype.triangle = TileMesh.prototype.triangle;

export function gameAtlasStatus(v) {
  const layer=layers.get(v);
  if(!layer?.displayProgress)return null;
  return {...layer.displayProgress,logicalLevel:layer.logicalLevel,
    pendingRivers:layer.scheduler.tributaries.queue.length+layer.scheduler.slots.filter(s=>s.tributary).length,
    failed:layer.failedTiles.size,workers:layer.scheduler.slots.length,maxWorkers:layer.scheduler.maxWorkers,queued:layer.scheduler.queue.length};
}
