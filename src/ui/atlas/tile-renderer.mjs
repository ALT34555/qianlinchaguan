import {createShorelineQuery,createPlanetShorelineSource} from '../../systems/world/planet-macro/shoreline-query.mjs';
import {clearCoverCache,coverCacheStats} from '../../systems/world/planet-macro/surface-cover.mjs';
import {tributaryCacheStats} from '../../systems/world/planet-macro/source-rivers.mjs';
import {TILE} from './tiles.mjs';
import {clearTributaryCache} from '../../systems/world/planet-macro/source-rivers.mjs';
import {LakeVectors, lakeMask, focusLakeAt} from './lake-vectors.mjs';
import {dayProfile, normalizeSunHours} from './relief.mjs';
import {contourUnit} from './contour.mjs';
import {collectRiverVectors,collectCellVectorsAsync} from './river-vectors.mjs';
import {SurfaceSampler, shadeLayerPixels, slopeAt, sampleOverview, overviewRiver, riverStrokePixels} from './surface-sampler.mjs';

export {sampleOverview, overviewRiver, riverStrokePixels, slopeAt};

let pauseChannel, pauseQueue = [];
const pause = () => new Promise(resolve => {
  if (!pauseChannel) {
    pauseChannel = new MessageChannel();
    pauseChannel.port1.onmessage = () => {
      const list = pauseQueue; pauseQueue = [];
      if (!pauseQueue.length) pauseChannel.port1.unref?.();
      for (const fn of list) fn();
    };
  }
  pauseQueue.push(resolve);
  if (pauseQueue.length === 1) {
    pauseChannel.port1.ref?.();
    pauseChannel.port2.postMessage(0);
  }
});

const CONTOUR_STRIDE = 2;

// 瓦片渲染器：整合采样服务与矢量湖河
export class TileRenderer {
  constructor(world, budget = 32 * 2 ** 20, options = {}) {
    this.world = world;
    this.sampler = new SurfaceSampler(world, budget);

    this.focusId = null;
    this.setSource(options.source??createPlanetShorelineSource(world,{includeDecorative:false}));
  }

  get lakes(){return this._lakes??=new LakeVectors(this.world);}
  set lakes(value){this._lakes=value;}

  setSource(source) {
    this.shorelines?.clear();this.fallbacks=new Map();this.sampler?.clearFields();this.source=source;
    this.shorelines=createShorelineQuery(source,{maxCacheBytes:8*1024*1024,maxSourceBytes:8*1024*1024});
  }

  async shoreline(tile,cancelled,progress) {
    const started=performance.now(),sourceCalls=this.shorelines.stats().sourceCalls;
    const n=2**tile.z,C=this.world.circumference,span=C/n,meters=C/2**(tile.z>15?12:tile.z)/TILE;
    const minX=tile.x/n*C-C/2,minZ=tile.y/n*C-C/4;
    const minStepM=Math.max(16,2**Math.floor(Math.log2(Math.max(16,meters))));
    const targetErrorM=Math.max(16,meters),maxStepM=Math.max(minStepM,2**Math.floor(Math.log2(Math.min(span,targetErrorM*8))));
    const request={worldRevision:this.source.worldRevision,hydrologyRevision:this.source.hydrologyRevision,
      coordinateSystem:'world-xz-metres',bounds:{minX,minZ,maxX:minX+span,maxZ:minZ+span},
      targetErrorM,minStepM,maxVertices:65536,maxCells:131072,maxStepM,rasterSide:TILE,phase:'surface',skipBoundaries:true,cancelled};
    const known=this.fallbacks.get(tile.key);
    if(known)Object.assign(request,known);
    let attempts=0,deadline=performance.now()+8;
    const yieldControl=async()=>{if(performance.now()>=deadline){await pause();deadline=performance.now()+8;}};
    while(true)try {
      const result=await this.shorelines.queryAsync(request,{yieldControl});
      if(!result.complete&&attempts++<2) {
        Object.assign(request,{maxStepM:Math.max(16,request.maxStepM*2),targetErrorM:Math.max(16,request.targetErrorM*2),disableHints:true});
        this.fallbacks.set(tile.key,{maxStepM:request.maxStepM,targetErrorM:request.targetErrorM,disableHints:true});
        while(this.fallbacks.size>128)this.fallbacks.delete(this.fallbacks.keys().next().value);
        continue;
      }
      if(known||attempts){result.budgetDegraded=true;result.requestedTargetErrorM=targetErrorM;}
      result.stats.samples=this.shorelines.stats().sourceCalls-sourceCalls;
      result.stats.elapsedMs=performance.now()-started;
      progress?.({stage:'共享岸线' ,sampled:result.stats.samples,complete:result.complete});
      return result;
    }catch(error) {
      if(error.name==='AbortError')return null;
      if(!error.promise)throw error;
      if(!await this.waitForShore(error.promise,cancelled))return null;
    }
  }

  cacheStats() {
    if(!this.sampler||!this.shorelines)return null;
    const source=this.shorelines.stats(),sampler=this.sampler.cacheStats();
    return {...sampler,shorelineBytes:source.cacheBytes,shorelineSourceBytes:source.sourceCacheBytes,
      totalPayloadBytes:sampler.totalBytes+source.cacheBytes+source.sourceCacheBytes+(this.source.hintStats?.().bytes??0),
      hintBytes:this.source.hintStats?.().bytes??0,tributaries:tributaryCacheStats(this.world),cover:coverCacheStats(this.world)};
  }

  dispose() {
    this.shorelines.clear();this.sampler.clear();this._lakes?.models.clear();this._lakes?.shapes.clear();this._lakes?.details.clear();this._lakes?.nearby.clear();this._lakes?.catalog.clear();
    this.regionalLakes?.models.clear();this.regionalLakes?.shapes.clear();this.regionalLakes?.details.clear();
    clearTributaryCache(this.world);clearCoverCache(this.world);
  }

  get samples() { return this.sampler ? this.sampler.samples : (this._samples ?? 0); }
  set samples(v) { if (this.sampler) this.sampler.samples = v; else this._samples = v; }
  get hits() { return this.sampler ? this.sampler.hits : (this._hits ?? 0); }
  set hits(v) { if (this.sampler) this.sampler.hits = v; else this._hits = v; }
  get bytes() { return this.sampler ? this.sampler.bytes : (this._bytes ?? 0); }
  set bytes(v) { if (this.sampler) this.sampler.bytes = v; else this._bytes = v; }
  get cache() { return this.sampler ? this.sampler.cache : (this._cache ??= new Map()); }
  get budget() { return this.sampler.budget; }
  get rivers() { return this.sampler.rivers; }
  get riverThreshold() { return this.sampler.riverThreshold; }
  get colorSampleCount() { return this.sampler ? this.sampler.colorSampleCount : 0; }
  get shorelineGeometryCount() { return this.sampler ? this.sampler.shorelineGeometryCount : 0; }
  get dataLayerCounts() { return this.sampler ? this.sampler.dataLayerCounts : {}; }

  async waitForShore(promise, cancelled) {
    let done = false, failure;
    promise.then(() => { done = true; }, error => { failure = error; done = true; });
    while (!done) {
      await new Promise(resolve => setTimeout(resolve, 16));
      if (cancelled()) return false;
    }
    if (failure) throw failure;
    return !cancelled();
  }

  // 共享地表字段缓存
  async fields(tile, cancelled = () => false, stride = 1, options = {}) {
    return this.sampler.getFields(tile, this.focusId, cancelled, stride, (p, c) => this.waitForShore(p, c), options);
  }

  // 输出独立RGBA图层
  async render(tile, layer, cancelled, progress, context = {}) {
    this.progress = progress;
    this.cancelled = cancelled ?? (() => false);
    if (this.cancelled()) return null;

    const shadeLayer = layer === 'contour' || layer === 'daylight';
    const w = this.world, n = 2 ** tile.z;
    const center = {
      wx: (tile.x + 0.5) / n * w.circumference - w.circumference / 2,
      wz: (tile.y + 0.5) / n * w.circumference - w.circumference / 4,
    };
    const logicalLevel=context.logicalLevel??tile.z;
    const focus = logicalLevel<12&&tile.z >= 13
      ? (context.focusId === undefined ? focusLakeAt(w, center.wx, center.wz) : context.focusId)
      : null;

    // 支流按宏观格确定，焦点变化不重建网络
    if (focus !== this.focusId) this.focusId = focus;

    let shapes = [];
    const shared=logicalLevel>15&&tile.z>15;
    const needsLakes = !shared&&(layer === 'base' || layer === 'satellite' || layer === 'biome' || layer === 'ice');
    const regional = tile.z >= 7 && tile.z <= 9;
    const lakes = shared?null:regional ? (this.regionalLakes ??= new LakeVectors(w, {maxDiv: 8, contourDiv: 2})) : this.lakes;

    if (needsLakes) {
      try {
        shapes = await lakes.prepare(tile, this.cancelled, progress);
        await lakes.prepareDetail(tile, this.focusId, this.cancelled, progress);
        if (this.sampler) this.sampler.shorelineGeometryCount += shapes.length;
      } catch (error) {
        if (error.name === 'AbortError') return null;
        throw error;
      }
    }

    const shoreline=shared&&layer!=='daylight'
      ?await this.shoreline(tile,this.cancelled,progress):null;
    if(this.cancelled())return null;
    const fields = await this.fields(tile, this.cancelled, shadeLayer ? CONTOUR_STRIDE : 1, { layer, palette: context.palette,logicalLevel,independentWater:shared });
    if (!fields || cancelled?.()) return null;

    const daylight = layer === 'daylight' ? dayProfile(normalizeSunHours(context.sunHours), context.sunWorld) : null;
    const mask = shared?shoreline?.waterMask:lakeMask(shapes, tile, w, lakes?.activeDetail ? this.focusId : null);
    const hasFocus = shapes.some(s => s.id === this.focusId) && lakes?.activeDetail != null;
    const unit = layer === 'contour' ? (context.contourUnit > 0 ? context.contourUnit : contourUnit(tile.z,w.columns)) : 0;
    const satellite = context.palette ? context.palette === 'satellite' : layer === 'satellite';

    const baseLayer=['base','satellite','biome'].includes(layer);
    const splitWater=baseLayer&&shared&&logicalLevel>=17;
    const shading={
      unit,
      daylight,
      world: w,
      tile,
      mask,
      satellite,
      independentWater:shared,waterMaskBytes:shared,iceMask:shoreline?.iceMask,
    };
    const waterPixels=splitWater?shadeLayerPixels('water',fields,shading):undefined;
    const pixels=shadeLayerPixels(layer,fields,splitWater?{...shading,mask:null,iceMask:null}:shading);

    let riverVectors;
    if(baseLayer){
      let deadline=performance.now()+8;
      while(!this.cancelled())try{
        riverVectors=await collectRiverVectors(w,tile,this.sampler,this.cancelled,async()=>{if(performance.now()>=deadline){await pause();deadline=performance.now()+8;}},shared,logicalLevel,{decorative:false});
        break;
      }catch(error){
        if(!error.promise)throw error;
        if(!await this.waitForShore(error.promise,this.cancelled))return null;
      }
      if(!riverVectors||this.cancelled())return null;
    }
    return {
      pixels,
      riverVectors,
      waterPixels,
      side: TILE,
      stats: {
        samples: this.samples,
        geometry:shoreline?{...shoreline.stats,complete:shoreline.complete,budgetDegraded:shoreline.budgetDegraded,modelDegraded:shoreline.modelDegraded,error:shoreline.error,requestedTargetErrorM:shoreline.requestedTargetErrorM??shoreline.targetErrorM,targetErrorM:shoreline.targetErrorM,modelRevision:shoreline.modelRevision}:null,
        logicalLevel,cache:this.cacheStats(),
        hits: this.hits,
        bytes: this.bytes,
        colorSampleCount: this.colorSampleCount,
        shorelineGeometryCount: this.shorelineGeometryCount,
        contourUnit: unit,
        lakes: lakes?.stats()??{models:0,shapes:0,details:0,sampled:0,vertices:0},
        lakeDetail: shared?'shared':regional ? 'regional' : 'fine',
        focusId: hasFocus ? this.focusId : null,
      },
    };
  }

  // 装饰支流独立任务：只建本宏观格的网络
  tributaryCell(cell,tier,cancelled){
    return collectCellVectorsAsync(this.world,cell,tier,{cancelled});
  }

  slopeAt(fields, x, y, dx, dy) {
    return slopeAt(fields, x, y, dx, dy);
  }

  async drawRivers(fields, tile, cancelled) {
    return this.sampler.drawRivers(fields, tile, cancelled);
  }
}
