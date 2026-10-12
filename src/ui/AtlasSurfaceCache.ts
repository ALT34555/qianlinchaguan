import type {WorldGenerator} from '../systems/world/WorldGenerator';
import type {SurfacePreview} from '../systems/world/SurfacePreview';
import type {SurfacePreviewRequest} from '../systems/world/ChunkProtocol';
import type {MapSummary} from '../systems/world/ChunkCache';

export interface Tile {average: string; image?: HTMLCanvasElement;summary?:MapSummary;missing?:boolean;refined?:boolean}
interface Want {cx: number; cz: number; priority: number; image: boolean;cachedOnly:boolean;detail:boolean}
interface Slot {worker: Worker; id: number; load: number}
/** 每个 Worker 最多在途请求数 */
const PER_WORKER = 1;
/** LRU 瓦片上限 */
const TILE_LIMIT = 1024;
/** 保留像素画布的瓦片上限，其余只留平均色 */
const IMAGE_LIMIT = 320;

/** 低内存设备少开 Worker */
function workerCount(): number {
  const memory = (navigator as unknown as {deviceMemory?: number}).deviceMemory;
  const cap = memory !== undefined && memory <= 4 ? 1 : 3;
  return Math.max(1, Math.min(cap, (navigator.hardwareConcurrency || 4) - 1));
}

/** 多预览 Worker、有限 LRU；瓦片按需保留像素画布 */
export class AtlasSurfaceCache {
  private readonly tiles = new Map<string, Tile>();
  private readonly wanted = new Map<string, Want>();
  private readonly pending = new Map<string, number>();
  private readonly slots: Slot[] = [];
  private nextId = 1;
  private images = 0;
  private failed = false;
  private paused = false;
  constructor(private generator: WorldGenerator, private readonly redraw: () => void,private readonly worldKey?:string,private limit=workerCount()) {}
  setWorkerLimit(count:number):void {
    this.limit=Math.max(1,Math.min(3,count));
    if(this.slots.length>this.limit){const paused=this.paused;this.idle();this.paused=paused;}
    for(const slot of this.slots)slot.worker.postMessage({kind:'budget',entries:this.limit===1?2048:8192} satisfies SurfacePreviewRequest);
  }
  setGenerator(generator: WorldGenerator): void { this.dispose(); this.generator = generator; this.failed = false; this.paused = false; }
  beginFrame(): void { this.wanted.clear(); }
  private coordinates(cx: number, cz: number): [number, number] {
    if (this.generator.generation.mode === 'planet') {
      const n = this.generator.generation.planet.equatorChunks; cx = ((cx + n / 2) % n + n) % n - n / 2;
    }
    return [cx, cz];
  }
  get(cx: number, cz: number, priority: number, image = false,cachedOnly=false,detail=image): Tile | undefined {
    [cx, cz] = this.coordinates(cx, cz); const key = `${cx},${cz}`;
    const previous=this.wanted.get(key);
    this.wanted.set(key, {cx, cz, priority:Math.min(priority,previous?.priority??Infinity), image:image||!!previous?.image,detail:detail||!!previous?.detail,cachedOnly:cachedOnly&&(previous?.cachedOnly??true)});
    let tile = this.tiles.get(key);
    if(tile?.missing&&!cachedOnly){this.tiles.delete(key);tile=undefined;}
    if (tile) { this.tiles.delete(key); this.tiles.set(key, tile); }
    return tile;
  }
  peek(cx:number,cz:number):Tile|undefined { [cx,cz]=this.coordinates(cx,cz);return this.tiles.get(`${cx},${cz}`); }
  get loading(): boolean {
    if (this.failed) return false;
    for (const [key, want] of this.wanted) {
      const tile = this.tiles.get(key);
      if (!tile || want.detail&&tile.refined===false || want.image && !tile.image&&!tile.missing) return true;
    }
    return false;
  }
  endFrame(): void { this.pump(); }

  /** 按优先级派发；可在帧外调用，避免被 rAF 节流 */
  private pump(): void {
    if (this.failed || this.paused || !this.wanted.size) return;
    const jobs: [string, Want][] = [];
    for (const job of this.wanted) {
      const tile = this.tiles.get(job[0]);
      if ((!tile || job[1].detail&&tile.refined===false || job[1].image && !tile.image&&!tile.missing) && !this.pending.has(job[0])) jobs.push(job);
    }
    if (!jobs.length) return;
    jobs.sort((a, b) => a[1].priority*(this.tiles.has(a[0])?2:1)-b[1].priority*(this.tiles.has(b[0])?2:1));
    for (const [key, job] of jobs) {
      const slot = this.idleSlot() ?? this.openSlot();
      if (!slot) break;
      this.pending.set(key, slot.id); slot.load++;
      slot.worker.postMessage({kind: 'generate', cx: job.cx, cz: job.cz, image: job.image,detail:job.detail,cachedOnly:job.cachedOnly} satisfies SurfacePreviewRequest);
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
    const tile: Tile = {average: `rgb(${preview.average.map(Math.round).join(',')})`,refined:preview.refined};
    if('info' in preview){tile.summary=preview as MapSummary;if(preview.refined!==false)this.generator.adoptMapSummary(tile.summary.info,tile.summary.paths);}
    if (preview.rgba) {
      const image = document.createElement('canvas');
      image.width = preview.size; image.height = preview.size;
      image.getContext('2d')!.putImageData(new ImageData(preview.rgba, preview.size, preview.size), 0, 0);
      tile.image = image; this.images++;
    }
    this.tiles.set(key, tile);
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

  /** 先丢旧像素图，再按 LRU 淘汰 */
  private trim(): void {
    if (this.images > IMAGE_LIMIT) for (const [key, tile] of this.tiles) {
      if (this.images <= IMAGE_LIMIT) break;
      if (tile.image && !this.wanted.has(key)) { tile.image = undefined; this.images--; }
    }
    for (const [key, tile] of this.tiles) {
      if (this.tiles.size <= TILE_LIMIT) break;
      if (this.wanted.has(key)) continue;
      if (tile.image) this.images--;
      this.tiles.delete(key);
    }
    const limit = Math.max(TILE_LIMIT, this.wanted.size);
    while (this.tiles.size > limit) {
      const key = this.tiles.keys().next().value!;
      if (this.tiles.get(key)?.image) this.images--;
      this.tiles.delete(key);
    }
  }

  dispose(): void {
    for (const slot of this.slots) slot.worker.terminate();
    this.slots.length = 0; this.nextId = 1;
    this.pending.clear(); this.wanted.clear(); this.tiles.clear(); this.images = 0;
  }

  /** 释放 Worker 与生成器缓存，保留瓦片；resume 前不再派发 */
  idle(): void {
    this.paused = true;
    for (const slot of this.slots) slot.worker.terminate();
    this.slots.length = 0;
    this.pending.clear();
  }
  resume(): void { this.paused = false; }
}
