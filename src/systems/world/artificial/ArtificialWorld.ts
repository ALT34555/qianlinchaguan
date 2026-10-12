import type { PlayerFactionOptions } from './PlayerFaction';
import type { ChunkInfo, WorldGenerator } from '../WorldGenerator';
import { ArtificialGenerator, MIN_TERRITORY_SCORE } from './ArtificialGenerator';
import { normalizeArtificial } from './ArtificialSettings';
import { calendarYear, claimKey, DAY_MS, EXCLUSION_RADIUS, FLAG_LAND, OWNED_LAND,
  type ArtificialClaim, type ArtificialState, type Territory } from './ArtificialState';
import { expandTerritory } from './NaturalExpansion';
import { evolveTerritory } from './NaturalEvolution';
import { flagIndependence } from './FlagIndependence';
import type { WatershedState } from '../Watershed';

export type TerritoryChunkInfo = ChunkInfo & { territory?: Territory };

/** 出生点置旗地扫描半径 */
export const SPAWN_SCAN_RADIUS = 16;

/** 由近及远的方形扫描偏移，确定性排序 */
function scanOffsets(radius: number): readonly [number, number][] {
  const list: [number, number][] = [];
  for (let dz = -radius; dz <= radius; dz++) for (let dx = -radius; dx <= radius; dx++) list.push([dx, dz]);
  list.sort((a, b) => a[0] ** 2 + a[1] ** 2 - b[0] ** 2 - b[1] ** 2 ||
    Math.abs(a[0]) + Math.abs(a[1]) - Math.abs(b[0]) - Math.abs(b[1]) || a[0] - b[0] || a[1] - b[1]);
  return list;
}

const SPAWN_SCAN = scanOffsets(SPAWN_SCAN_RADIUS);

export class ArtificialWorld {
  readonly generator: ArtificialGenerator;
  readonly settings;
  readonly territories = new Map<string, Territory>();
  readonly claims = new Map<string, ArtificialClaim>();
  revision = 0;
  playerFactionId?: string;
  private readonly tiles = new Set<string>();
  private queryDiscovery=true;
  private readonly buckets = new Map<string, ArtificialClaim[]>();
  private readonly epochUnixMs: number;
  private lastDay: number;
  private work: Generator<void> | null = null;
  private needsCatchup = false;
  private background?:Worker;
  private busy=false;
  private pending?:{ms:number;cx:number;cz:number};
  private latest?:{ms:number;cx:number;cz:number};
  private region?:number[];
  private lastRequest=0;
  private backgroundWake?:ReturnType<typeof setTimeout>;
  private backgroundError?:Error;
  private flushing=false;
  private waiters:{resolve:()=>void;reject:(error:Error)=>void}[]=[];

  constructor(readonly terrain: WorldGenerator, unixMs = 0, private readonly mode: 'real' | 'yuan' = 'real',
    private readonly offset = 0, state?: ArtificialState) {
    this.settings = Object.freeze(normalizeArtificial(terrain.generation.artificial));
    this.generator = new ArtificialGenerator(terrain);
    this.epochUnixMs = state?.epochUnixMs ?? unixMs;
    this.lastDay = state?.lastDay ?? this.day(unixMs);
    if (state) {
      this.playerFactionId = state.playerFactionId;
      for (const t of state.territories) this.territories.set(t.id, { ...t });
      for (const c of state.claims) this.addClaim({ ...c });
      this.needsCatchup = true;
      for(const [x,z] of state.discoveredTiles??[])this.tiles.add(claimKey(x,z));
    }
  }

  enableBackground(worker:Worker,ms:number,cx:number,cz:number,hydrology:WatershedState,onChange?:()=>void):void {
    if(!this.settings.enabled||this.background)return;
    this.background=worker;
    worker.onmessage=event=>{
      this.busy=false;
      if(event.data.error){this.failBackground(new Error(event.data.error));return;}
      if(event.data.state){this.adoptBackground(event.data.state);onChange?.();}
      this.pumpBackground();
      if(!this.busy&&!this.pending){this.flushing=false;this.waiters.splice(0).forEach(w=>w.resolve());}
      if(event.data.moreWork&&!this.busy&&!this.backgroundError){
        this.backgroundWake=setTimeout(()=>{this.pending=this.latest;this.pumpBackground();},150);
      }
    };
    worker.onerror=event=>this.failBackground(new Error(event.message));
    worker.postMessage({kind:'init',seed:this.terrain.seed,climateWeights:this.terrain.climateWeights,generation:this.terrain.generation,
      hydrology,state:this.snapshot(),ms,mode:this.mode,offset:this.offset,entries:this.terrain.cacheLimit});
    this.update(ms,cx,cz);
  }
  setCacheBudget(entries:number):void {this.background?.postMessage({kind:'budget',entries});}

  private adoptBackground(state:ArtificialState):void {
    this.territories.clear();this.claims.clear();this.buckets.clear();this.tiles.clear();
    this.playerFactionId=state.playerFactionId;this.lastDay=state.lastDay;
    for(const territory of state.territories)this.territories.set(territory.id,territory);
    for(const claim of state.claims)this.addClaim(claim);
    for(const [x,z] of state.discoveredTiles??[])this.tiles.add(claimKey(x,z));
    this.revision++;
  }

  private pumpBackground():void {
    if(this.busy||this.backgroundError||!this.background||!this.pending)return;
    clearTimeout(this.backgroundWake);this.backgroundWake=undefined;
    this.busy=true;const pending=this.pending;this.pending=undefined;
    this.background.postMessage({kind:'update',...pending,region:this.region});this.region=undefined;
  }

  private failBackground(error:Error):void {
    console.error('[Artificial worker]',error);
    this.backgroundError=error;
    clearTimeout(this.backgroundWake);
    this.flushing=false;
    this.busy=false;this.pending=undefined;
    this.waiters.splice(0).forEach(w=>w.reject(error));
    this.background?.terminate();
    // 保持只读，避免同步回退卡顿
  }

  flushBackground():Promise<void>{
    if(this.backgroundError)return Promise.reject(this.backgroundError);
    if(!this.busy&&!this.pending)return Promise.resolve();
    this.flushing=true;
    return new Promise((resolve,reject)=>this.waiters.push({resolve,reject}));
  }
  dispose():void {clearTimeout(this.backgroundWake);this.background?.terminate();this.background=undefined;this.busy=false;this.pending=undefined;this.waiters.splice(0).forEach(w=>w.resolve());}

  private day(ms: number): number { return Math.floor((ms + this.offset * 60000) / DAY_MS); }
  private year(ms: number): number {
    return Math.max(0, calendarYear(ms, this.mode, this.offset) - calendarYear(this.epochUnixMs, this.mode, this.offset));
  }

  private addClaim(c: ArtificialClaim): void {
    this.claims.set(claimKey(c.cx, c.cz), c);
    const key = claimKey(Math.floor(c.cx / EXCLUSION_RADIUS), Math.floor(c.cz / EXCLUSION_RADIUS));
    const bucket = this.buckets.get(key) ?? [];
    bucket.push(c); this.buckets.set(key, bucket);
    this.revision++;
  }

  private found(cx: number, cz: number, type: number, year: number, salt: string): boolean {
    cx = this.generator.wrap(cx);
    const t = this.generator.faction(cx, cz, year, salt);
    if (this.territories.has(t.id) || this.claims.has(claimKey(cx, cz))) return false;
    this.territories.set(t.id, t);
    this.addClaim({ cx, cz, type, faction: t.id, step: 0 });
    this.needsCatchup = true;
    return true;
  }

  createPlayerFaction(cx: number, cz: number, options: PlayerFactionOptions): void {
    cx = this.generator.wrap(cx);
    if (this.playerFactionId || !this.generator.suitable(cx, cz) || !this.hasSpace(cx, cz)) throw new Error('己方置旗地不可用。');
    const t = { ...this.generator.faction(cx, cz, 0, 'ren'), name: options.name,
      mainColor: options.mainColor, trimColor: options.trimColor };
    this.playerFactionId = t.id;
    this.territories.set(t.id, t);
    this.addClaim({ cx, cz, type: FLAG_LAND, faction: t.id, step: 0 });
  }

  /** 出生点附近保底立一处自然置旗地 */
  seedNearby(cx: number, cz: number, radius = SPAWN_SCAN_RADIUS): boolean {
    if (!this.settings.enabled) return false;
    const offsets = radius === SPAWN_SCAN_RADIUS ? SPAWN_SCAN : scanOffsets(radius);
    for (const strict of [true, false]) {
      for (const [dx, dz] of offsets) {
        const x = this.generator.wrap(cx + dx), z = cz + dz;
        if (!this.generator.inWorld(z) || this.claims.has(claimKey(x, z)) || !this.hasSpace(x, z)) continue;
        const value = this.generator.score(x, z);
        if (value < 0 || (strict && value < MIN_TERRITORY_SCORE)) continue;
        if (this.found(x, z, FLAG_LAND, 0, 'spawn')) return true;
      }
    }
    return false;
  }

  discover(cx: number, cz: number, radius = 0): void {
    if (!this.settings.enabled) return;
    const s = this.generator.cellSize;
    const planet = this.terrain.generation.mode === 'planet';
    const size = this.terrain.generation.planet.equatorChunks;
    const ox = planet ? -size / 2 : 0, oz = planet ? -size / 4 : 0;
    cx = this.generator.wrap(cx);
    const columns = Math.round(size / s);
    for (let z = Math.floor((cz - radius - oz) / s); z <= Math.floor((cz + radius - oz) / s); z++) {
      if (planet && (z < 0 || z * s >= size / 2)) continue;
      for (let x = Math.floor((cx - radius - ox) / s); x <= Math.floor((cx + radius - ox) / s); x++) {
        const tx = planet ? (x % columns + columns) % columns : x;
        const key = claimKey(tx, z);
        if (this.tiles.has(key)) continue;
        this.tiles.add(key);
        const c = this.generator.candidate(tx, z);
        if (c && this.hasSpace(c.cx, c.cz)) this.found(c.cx, c.cz, OWNED_LAND, 0, 'initial');
      }
    }
  }

  getClaim(cx: number, cz: number): ArtificialClaim | undefined {
    if(!this.background&&this.queryDiscovery)this.discover(cx, cz);
    return this.claims.get(claimKey(this.generator.wrap(cx), cz));
  }
  useCachedQueries():void {this.queryDiscovery=false;}

  *discoverRegion(minX: number, minZ: number, maxX: number, maxZ: number): Generator<void> {
    if (!this.settings.enabled) return;
    if(this.background){
      if(this.backgroundError||this.flushing)return;
      this.region=[minX,minZ,maxX,maxZ];this.pending=this.latest;this.pumpBackground();yield;return;
    }
    const s = this.generator.cellSize, planet = this.terrain.generation.mode === 'planet';
    const size = this.terrain.generation.planet.equatorChunks;
    const ox = planet ? -size / 2 : 0, oz = planet ? -size / 4 : 0;
    for (let z = Math.floor((minZ - oz) / s); z <= Math.floor((maxZ - oz) / s); z++) {
      for (let x = Math.floor((minX - ox) / s); x <= Math.floor((maxX - ox) / s); x++) {
        this.discover(Math.floor(ox + (x + .5) * s), Math.floor(Math.min(oz + (z + .5) * s, planet ? size / 4 - 1 : Infinity)));
        yield;
      }
    }
  }

  info(cx: number, cz: number): TerritoryChunkInfo {
    const info = this.terrain.getChunkInfo(cx, cz), c = this.getClaim(cx, cz);
    return c ? { ...info, type: c.type, territory: this.territories.get(c.faction) } : info;
  }

  unowned(cx: number, cz: number, except?: string): boolean {
    cx = this.generator.wrap(cx);
    this.discover(cx, cz, EXCLUSION_RADIUS);
    return this.hasSpace(cx, cz, except);
  }

  private hasSpace(cx: number, cz: number, except?: string): boolean {
    const size = this.terrain.generation.planet.equatorChunks;
    const shifts = this.terrain.generation.mode === 'planet' ? [-size, 0, size] : [0];
    for (const shift of shifts) {
      const bx = Math.floor((cx + shift) / EXCLUSION_RADIUS), bz = Math.floor(cz / EXCLUSION_RADIUS);
      for (let z = bz - 1; z <= bz + 1; z++) for (let x = bx - 1; x <= bx + 1; x++) {
        for (const c of this.buckets.get(claimKey(x, z)) ?? []) {
          if (c.faction !== except && this.generator.distance2(cx, cz, c.cx, c.cz) <= EXCLUSION_RADIUS ** 2) return false;
        }
      }
    }
    return true;
  }

  private claim = (cx: number, cz: number, faction: string, step: number): boolean => {
    cx = this.generator.wrap(cx);
    if (this.claims.has(claimKey(cx, cz)) || !this.generator.inWorld(cz) || !this.unowned(cx, cz, faction) ||
        !this.generator.suitable(cx, cz)) return false;
    this.addClaim({ cx, cz, type: OWNED_LAND, faction, step });
    return true;
  };

  private *advance(year: number): Generator<void> {
    for (const t of [...this.territories.values()].sort((a, b) => a.id.localeCompare(b.id))) {
      if (evolveTerritory(t, year, this.settings.evolution)) this.revision++;
      yield* expandTerritory(t, year, this.settings.expansion, {
        claims: this.claims, wrap: cx => this.generator.wrap(cx), claim: this.claim,
      });
      yield;
    }
  }

  private *simulate(targetDay: number, playerCx: number, playerCz: number): Generator<void> {
    if (this.needsCatchup) {
      this.needsCatchup = false;
      yield* this.advance(this.year(this.lastDay * DAY_MS - this.offset * 60000));
    }
    while (this.lastDay < targetDay) {
      const day = this.lastDay + 1, year = this.year(day * DAY_MS - this.offset * 60000);
      yield* this.advance(year);
      const highestLevel = Math.max(0, ...[...this.territories.values()].map(t => t.level));
      yield* flagIndependence({ enabled: this.settings.evolution > 0 && this.settings.independence,
        highestLevel, seed: this.terrain.seed, day, cx: playerCx, cz: playerCz,
        unowned: (x, z) => this.unowned(x, z), suitable: (x, z) => this.generator.suitable(x, z),
        found: (x, z) => this.found(x, z, FLAG_LAND, year, `day:${day}`),
      });
      this.lastDay = day;
      yield;
    }
  }

  update(ms: number, playerCx: number, playerCz: number, budgetMs = 3): void {
    if (!this.settings.enabled) return;
    if(this.background){
      if(this.backgroundError||this.flushing)return;
      this.latest={ms,cx:playerCx,cz:playerCz};
      const now=performance.now();
      if(this.lastRequest&&now-this.lastRequest<250&&!this.region)return;
      this.lastRequest=now;this.pending=this.latest;this.pumpBackground();return;
    }
    this.discover(playerCx, playerCz);
    const day = this.day(ms);
    if (!this.work && (day > this.lastDay || this.needsCatchup)) this.work = this.simulate(day, playerCx, playerCz);
    const start = performance.now();
    while (this.work) {
      if (this.work.next().done) this.work = null;
      if (performance.now() - start >= budgetMs) break;
    }
  }

  snapshot(): ArtificialState | undefined {
    if (!this.settings.enabled && !this.playerFactionId) return undefined;
    return { discoveredTiles:[...this.tiles].map(k=>k.split(',').map(Number) as [number,number]),...(this.playerFactionId ? { playerFactionId: this.playerFactionId } : {}), epochUnixMs: this.epochUnixMs, lastDay: this.lastDay,
      territories: [...this.territories.values()].map(t => ({ ...t })), claims: [...this.claims.values()].map(c => ({ ...c })) };
  }
}
