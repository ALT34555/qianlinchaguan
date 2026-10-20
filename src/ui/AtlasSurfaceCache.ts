import type {WorldGeneratorBase as WorldGenerator} from '../systems/world/WorldContract';
import type {SurfacePreview} from '../systems/world/SurfacePreview';
import type {SurfacePreviewRequest} from '../systems/world/ChunkProtocol';
import type {MapSummary} from '../systems/world/ChunkCache';
import {overviewColor} from './PlanetAtlasLayer';

export interface Tile {average: string; image?: HTMLCanvasElement;summary?:MapSummary;missing?:boolean;refined?:boolean}
interface Want {cx: number; cz: number; priority: number; image: boolean;cachedOnly:boolean;detail:boolean;nodes:boolean}
interface Slot {worker: Worker; id: number; load: number}
/** 每个 Worker 最多在途请求数 */
const PER_WORKER = 1;
/** LRU 瓦片上限 */
const TILE_LIMIT = 1024;
/** 保留像素画布的瓦片上限，其余只留平均色 */
const IMAGE_LIMIT = 320;
const HISTORY_LIMIT = 32768;
const LOW_HISTORY_LIMIT = 8192;

/** 低内存设备少开 Worker */
function workerCount(): number {
  const memory = (navigator as unknown as {deviceMemory?: number}).deviceMemory;
  const cap = memory !== undefined && memory <= 4 ? 1 : 3;
  return Math.max(1, Math.min(cap, (navigator.hardwareConcurrency || 4) - 1));
}

/** 舆图地表预览缓存，详见同名文档 */
export class AtlasSurfaceCache {
  private readonly tiles = new Map<string, Tile>();
  private readonly history = new Map<string, Tile>();
  private readonly wanted = new Map<string, Want>();
  private readonly pending = new Map<string, number>();
  private readonly slots: Slot[] = [];
  private nextId = 1;
  private images = 0;
  private failed = false;
  private paused = false;
  private season:number|null=null;
  constructor(private generator: WorldGenerator, private readonly redraw: () => void,private readonly worldKey?:string,private limit=workerCount()) { this.limit=Math.max(1,Math.min(workerCount(),this.limit)); }
  private get historyLimit():number { return this.limit===1?LOW_HISTORY_LIMIT:HISTORY_LIMIT; }
  private get imageLimit():number { return this.limit===1?64:IMAGE_LIMIT; }
  setWorkerLimit(count:number):void {
    this.limit=Math.max(1,Math.min(workerCount(),count));
    if(this.slots.length>this.limit){const paused=this.paused;this.idle();this.paused=paused;}
    this.trimHistory();this.trim();
    for(const slot of this.slots)slot.worker.postMessage({kind:'budget',entries:this.limit===1?2048:8192} satisfies SurfacePreviewRequest);
  }
  setGenerator(generator: WorldGenerator): void { this.dispose(); this.generator = generator; this.failed = false; this.paused = false; }
  beginFrame(): void {
    const season=this.generator.getWaterSeason?.()??null;
    if(season!==this.season){this.dispose();this.failed=false;this.season=season;}
    this.wanted.clear();
  }
  private coordinates(cx: number, cz: number): [number, number] {
    if (this.generator.generation.mode === 'planet') {
      const n = this.generator.generation.planet.equatorChunks; cx = ((cx + n / 2) % n + n) % n - n / 2;
    }
    return [cx, cz];
  }
  get(cx: number, cz: number, priority: number, image = false,cachedOnly=false,detail=image,nodes=false): Tile | undefined {
    [cx, cz] = this.coordinates(cx, cz); const key = `${cx},${cz}`;
    const previous=this.wanted.get(key);
    this.wanted.set(key, {cx, cz, priority:Math.min(priority,previous?.priority??Infinity), image:image||!!previous?.image,detail:detail||!!previous?.detail,cachedOnly:cachedOnly&&(previous?.cachedOnly??true),nodes:(!detail||nodes)&&(!previous?.detail||!!previous.nodes)});
    let tile = this.tiles.get(key);
    if(tile?.missing&&!cachedOnly){this.tiles.delete(key);tile=undefined;}
    if(!tile){
      const info=this.generator.cachedChunkInfo(cx,cz);
      if(info){
        const pathsReady=this.generator.hasRiverPaths(cx,cz),paths=pathsReady?this.generator.getRiverPaths(cx,cz):[];
        const average=overviewColor(info,false,false,true);
        tile={average:`rgb(${average.join(',')})`,refined:true,summary:{cx,cz,size:1,average,info,paths,refined:true,schematic:!pathsReady}};
        this.history.delete(key);this.tiles.set(key,tile);
      }
    }
    if(!tile){tile=this.history.get(key);if(tile){this.history.delete(key);this.tiles.set(key,tile);}}
    if (tile) {
      this.tiles.delete(key); this.tiles.set(key, tile);
      if(tile.summary&&!tile.summary.schematic&&tile.refined!==false&&!this.generator.cachedChunkInfo(cx,cz))this.generator.adoptMapSummary(tile.summary.info,tile.summary.paths);
    }
    return tile;
  }
  peek(cx:number,cz:number):Tile|undefined { [cx,cz]=this.coordinates(cx,cz);return this.tiles.get(`${cx},${cz}`); }
  get loading(): boolean {
    if (this.failed) return false;
    for (const [key, want] of this.wanted) {
      const tile = this.tiles.get(key);
      if (!tile || want.detail&&(tile.refined===false||!want.nodes&&tile.summary?.schematic) || want.image && !tile.image&&!tile.missing) return true;
    }
    return false;
  }
  endFrame(): void { this.trim();this.pump(); }

  /** 按优先级派发；可在帧外调用，避免被 rAF 节流 */
  private pump(): void {
    if (this.failed || this.paused || !this.wanted.size) return;
    const jobs: [string, Want][] = [];
    for (const job of this.wanted) {
      const tile = this.tiles.get(job[0]);
      if ((!tile || job[1].detail&&(tile.refined===false||!job[1].nodes&&tile.summary?.schematic) || job[1].image && !tile.image&&!tile.missing) && !this.pending.has(job[0])) jobs.push(job);
    }
    if (!jobs.length) return;
    jobs.sort((a,b)=>Number(this.tiles.has(a[0]))-Number(this.tiles.has(b[0]))||a[1].priority-b[1].priority);
    for (const [key, job] of jobs) {
      const slot = this.idleSlot() ?? this.openSlot();
      if (!slot) break;
      this.pending.set(key, slot.id); slot.load++;
      const base=!this.tiles.has(key)&&!job.cachedOnly;
      slot.worker.postMessage({kind: 'generate', cx: job.cx, cz: job.cz, image:base?false:job.image,detail:base?false:job.detail,cachedOnly:job.cachedOnly,nodes:base?false:job.nodes,base,waterSeason:this.generator.getWaterSeason?.()??null} satisfies SurfacePreviewRequest);
    }
  }

  private idleSlot(): Slot | null {
    let best: Slot | null = null;
    for (const slot of this.slots) if (slot.load < PER_WORKER && (!best || slot.load < best.load)) best = slot;
    return best;
  }

  /** 按需扩容：现有 Worker 全忙时才新建 */
  private openSlot(): Slot | null {
    if (this.slots.length >= this.limit) return null;
    const worker = new Worker(new URL('../systems/world/surface-preview.worker.ts', import.meta.url), {type: 'module'});
    const slot: Slot = {worker, id: this.nextId++, load: 0};
    worker.onmessage = (event: MessageEvent<SurfacePreview>) => this.receive(slot, event.data);
    worker.onerror = event => this.drop(slot, event.message);
    worker.postMessage({kind: 'init', seed: this.generator.seed, climateWeights: this.generator.climateWeights, generation: this.generator.generation,
      worldKey:this.worldKey,hydrology:this.worldKey?this.generator.exposeHydrology():undefined} satisfies SurfacePreviewRequest);
    worker.postMessage({kind:'budget',entries:this.limit===1?2048:8192} satisfies SurfacePreviewRequest);
    this.slots.push(slot);
    return slot;
  }

  private receive(slot: Slot, preview: SurfacePreview): void {
    if (this.failed || !this.slots.includes(slot)) return;
    if('error' in preview){this.drop(slot,String(preview.error));return;}
    slot.load--; const key = `${preview.cx},${preview.cz}`;
    this.pending.delete(key);
    if('missing' in preview){this.tiles.set(key,{average:'rgba(145,121,83,.13)',missing:true});this.trim();this.redraw();this.pump();return;}
    if(this.tiles.get(key)?.image)this.images--;
    const previous=this.tiles.get(key);
    const tile: Tile = {average: `rgb(${preview.average.map(Math.round).join(',')})`,refined:preview.refined,image:previous?.image};
    if('info' in preview){
      const {rgba:_rgba,...summary}=preview as MapSummary;
      tile.summary=summary;if(preview.refined!==false&&!summary.schematic)this.generator.adoptMapSummary(summary.info,summary.paths);
      if(previous?.summary&&!previous.summary.schematic&&previous.refined!==false&&(preview.refined===false||summary.schematic)){
        tile.summary=previous.summary;tile.refined=previous.refined;tile.average=previous.average;
      }
    }
    if (preview.rgba) {
      const image = document.createElement('canvas');
      image.width = preview.size; image.height = preview.size;
      image.getContext('2d')!.putImageData(new ImageData(preview.rgba, preview.size, preview.size), 0, 0);
      tile.image = image;
    }
    if(tile.image)this.images++;
    this.history.delete(key);this.tiles.set(key, tile);
    this.trim();
    this.redraw();
    this.pump();
  }

  private drop(slot: Slot, message: string): void {
    console.error('[AtlasSurfaceCache]', message);
    slot.worker.terminate();
    const at = this.slots.indexOf(slot);
    if (at >= 0) this.slots.splice(at, 1);
    for (const [key, id] of this.pending) if (id === slot.id) this.pending.delete(key);
    if (!this.slots.length) { this.failed = true; this.pending.clear(); }
    this.redraw();
  }

  /** 历史层只留颜色、区块信息和节点 */
  private remember(key:string,tile:Tile):void {
    if(tile.missing)return;
    const summary=tile.summary?{...tile.summary,paths:[],schematic:true}:undefined;
    this.history.delete(key);this.history.set(key,{average:tile.average,summary,refined:tile.refined});
    this.trimHistory();
  }
  private trimHistory():void {
    while(this.history.size>this.historyLimit)this.history.delete(this.history.keys().next().value!);
  }
  /** 细节淘汰时保留轻量历史 */
  private trim(): void {
    if (this.images > this.imageLimit) for (const [key, tile] of this.tiles) {
      if (this.images <= this.imageLimit) break;
      if (tile.image && !this.wanted.has(key)) { tile.image = undefined; this.images--; }
    }
    for (const [key, tile] of this.tiles) {
      if (this.tiles.size <= TILE_LIMIT) break;
      if (this.wanted.has(key)) continue;
      if (tile.image) this.images--;
      this.remember(key,tile);
      this.tiles.delete(key);
    }
    const limit = Math.max(TILE_LIMIT, this.wanted.size);
    while (this.tiles.size > limit) {
      const key = this.tiles.keys().next().value!;
      const tile=this.tiles.get(key)!;
      if (tile.image) this.images--;
      this.remember(key,tile);
      this.tiles.delete(key);
    }
  }

  dispose(): void {
    for (const slot of this.slots) slot.worker.terminate();
    this.slots.length = 0; this.nextId = 1;
    this.pending.clear(); this.wanted.clear(); this.tiles.clear(); this.history.clear(); this.images = 0;
  }

  /** 释放后台 Worker 缓存，保留瓦片 */
  idle(): void {
    this.paused = true;
    for (const slot of this.slots) slot.worker.terminate();
    this.slots.length = 0;
    this.pending.clear();
  }
  resume(): void { this.paused = false; }
}
