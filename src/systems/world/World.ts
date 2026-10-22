import {ShorelineWorkerClient} from './planet-macro/ShorelineWorkerClient';
import type {ShorelineRequest,ShorelineResult} from './planet-macro/shoreline-query.mjs';
import {worldSignature} from './WorldSignature';
import {chunkSignature} from './ChunkCache';
import {IceShell} from './IceShell';
import { createWorldGenerator } from './GeneratorFactory';
import {patchWaterHeight} from './planet-macro/PlanetPatchWater';
import { performanceBudget } from '../../core/PerformanceBudget';
import type { GameSettings } from '../../core/GameSettings';
import { DENSE_VEGETATION_MULTIPLIERS, denseVegetationLevel } from '../../core/GameSettings';
import type { WaterFreezeRules, WaterFreezeState } from './WaterFreezing';
import type { WaterAppearanceUpdate, WaterForm, WaterPalette, ChunkWaterState } from './WaterAppearance';
/** 世界 / 区块管理（主线程） */
import * as THREE from 'three';
import type { ClimateWeights, WorldGeneration } from './WorldSettings';
import { CHUNK_SIZE } from '../../core/config';
import { renderChunkOffsets } from '../../core/RenderDistance';
import type { MeshData } from './ChunkMesher';
import type { ChunkResultMessage } from './ChunkProtocol';
import { ChunkWorkerPool } from './ChunkWorkerPool';
import type { WorldGeneratorBase as WorldGenerator } from './WorldContract';
import { terrainHeightAt, terrainGroundUnder } from './TerrainSurface';
import { WorldLighting } from './WorldLighting';
import { WaterMaterial, decodeSurfaceColors } from './WaterMaterial';
import { waterHeightAt, iceHeightAt } from './WaterSurface';
import { WaterKind, type WaterField } from './DynamicWater';
import { WaterfallMist } from './WaterfallMist';
import { ArtificialWorld } from './artificial/ArtificialWorld';
import { allowsNaturalDecoration } from './artificial/ArtificialState';
import { territoryFlag } from './artificial/TerritoryFlag';
import type { AimFlagBounds } from './AimProbe';
import type { DecorationPlacement } from './WorldDecoration';
import { DecorationRenderer } from './DecorationInstances';
import { DecorationCollisions } from './DecorationCollisions';
import { FlagWind } from './flags/FlagWind';
import { offsetFlagColliders } from './flags/FlagPoleColliders';

export interface LoadedChunk {
  cx: number;
  cz: number;
  type: number;
  /** 区块内高度图 64x64 */
  heights: Float32Array;
  surfaces: Uint32Array;
  waterLevels: Float32Array;
  waterField: WaterField;
  minimap: ImageData;
  terrain: THREE.Mesh | null;
  water: THREE.Mesh | null;
  decoration: THREE.Mesh | null;
  rocks: THREE.Mesh | null;
  mist: THREE.Points | null;
  mistEmitters: Float32Array | null;
  flag: THREE.Mesh | null;
  /** 领地旗帜世界包围盒（准心检视） */
  flagBounds: AimFlagBounds | null;
  /** 装饰落位（世界坐标，准心检视） */
  decorations: DecorationPlacement[];
}

const chunkKey = (cx: number, cz: number) => `${cx},${cz}`;

/** 网格世界包围盒 */
function meshBounds(mesh: THREE.Mesh): AimFlagBounds {
  mesh.geometry.computeBoundingBox();
  const box = mesh.geometry.boundingBox!;
  const { x, y, z } = mesh.position;
  return { minX: box.min.x + x, minY: box.min.y + y, minZ: box.min.z + z,
    maxX: box.max.x + x, maxY: box.max.y + y, maxZ: box.max.z + z };
}

/** 每帧接收区块时间预算，详见同名文档 */
const UPLOAD_BUDGET_MS = 2;
const UPLOAD_BUDGET_MAX_MS = 8;
/** 派发也允许用一点时间，否则单帧只提交 1 个任务 */
const DISPATCH_BUDGET_MS = 3;
/** 当前帧率下实际可用的接收预算 */
export function uploadBudgetMs(lastFrameMs = 0): number {
  if (!(lastFrameMs > 0)) return UPLOAD_BUDGET_MS;
  const scale = Math.min(1, 1000 / 60 / lastFrameMs);
  return Math.min(UPLOAD_BUDGET_MAX_MS, UPLOAD_BUDGET_MS / scale);
}

export class World {
  private readonly collisionVolumes = new DecorationCollisions();
  private readonly flagCollisions = new DecorationCollisions();
  private readonly flagWind = new FlagWind();
  readonly artificial: ArtificialWorld;
  private artificialRevision = -1;
  private waterRevision = 0;
  private shorelineClient?:ShorelineWorkerClient;
  private readonly waterStale=new Set<string>();
  private iceShell?:IceShell;
  readonly generator: WorldGenerator;
  private readonly chunks = new Map<string, LoadedChunk>();
  private readonly pending = new Set<string>();
  private readonly ready: ChunkResultMessage[] = [];
  private readonly pool: ChunkWorkerPool;
  private readonly group = new THREE.Group();
  private readonly terrainMaterial = new THREE.MeshStandardMaterial({ vertexColors: true, flatShading: true, roughness: .95 });
  private readonly waterMaterial = new WaterMaterial();
  private readonly decorationMaterial = new THREE.MeshStandardMaterial({ vertexColors: true, flatShading: true, roughness: .95, side: THREE.DoubleSide });
  private readonly mist = new WaterfallMist();
  private readonly instances=new DecorationRenderer(this.decorationMaterial);
  private mistEnabled = true;
  private vegetationDensity = 1;
  private denseVegetation = 0;
  private readonly lighting: WorldLighting;
  /** 以距离排序的加载偏移表（圆形范围） */
  private offsets: [number, number][] = [];
  private priorityOffsets: [number, number][] = [];
  private priorityKey = '';
  private renderDistance = 0;
  /** 首帧取用的粗格样本提供者，取用一次即清空 */
  private vegetationDistance = 16;
  private centerCx = Number.NaN;
  private centerCz = Number.NaN;
  private readonly listeners: ((c: LoadedChunk) => void)[] = [];
  private active = true;
  cacheHits = 0;

  constructor(scene: THREE.Scene, readonly seed: number, renderDistance: number, climateWeights: ClimateWeights, generation?: WorldGeneration, artificial?: ArtificialWorld,
    generator?:WorldGenerator,worldKey?:string,settings:Pick<GameSettings,'graphicsPreset'|'denseVegetation'>={graphicsPreset:'balanced'}) {
    this.generator = generator ?? createWorldGenerator(seed, climateWeights, generation);
    this.artificial = artificial ?? new ArtificialWorld(this.generator);
    this.terrainMaterial.onBeforeCompile = decodeSurfaceColors;
    this.decorationMaterial.onBeforeCompile = decodeSurfaceColors;
    this.lighting = new WorldLighting(scene);
    this.pool = new ChunkWorkerPool(seed, climateWeights, this.generator.generation, (msg) => this.ready.push(msg),performanceBudget(settings).chunkWorkers,worldKey,
      generator?generator.exposeHydrology():undefined,(cx,cz)=>this.pending.delete(chunkKey(cx,cz)));
    this.pool.setBudget(performanceBudget(settings).generatorEntries,performanceBudget(settings).derivedMeshBytes);
    this.generator.cacheLimit=performanceBudget(settings).generatorEntries;
    this.vegetationDensity=performanceBudget(settings).vegetationDensity;
    this.denseVegetation = settings.denseVegetation ?? denseVegetationLevel(settings.graphicsPreset ?? 'balanced');
    this.setRenderDistance(renderDistance);
    scene.add(this.group);
    this.group.add(this.instances.group);
  }

  updateLighting(dayRatio: number, season: number, camera: THREE.Vector3, time: number, weather: import('./Weather').WeatherId = 0, flash = 0, precipitation?: number): THREE.Color {
    const state = this.lighting.update(dayRatio, season, camera, weather, flash, precipitation);
    this.terrainMaterial.color.copy(this.lighting.tint);
    this.decorationMaterial.color.copy(this.lighting.tint);
    this.waterMaterial.update(time, this.lighting, state.daylight, state.night);
    this.mist.update(time, state.daylight);
    return this.lighting.fogColor;
  }

  setMistEnabled(on: boolean): void {
    if (on === this.mistEnabled) return;
    this.mistEnabled = on;
    for (const chunk of this.chunks.values()) {
      if (on && !chunk.mist && chunk.mistEmitters) {
        chunk.mist = this.mist.build(chunk.mistEmitters, chunk.cx, chunk.cz, CHUNK_SIZE);
        if (chunk.mist) {
          chunk.mist.visible = this.inRange(chunk.cx, chunk.cz, 0);
          this.group.add(chunk.mist);
        }
      } else if (!on && chunk.mist) {
        this.mist.disposePoints(chunk.mist);
        chunk.mist = null;
      }
    }
  }

  setMistProjection(fov: number, viewportHeight: number, pixelRatio: number): void {
    this.mist.setProjection(fov, viewportHeight, pixelRatio);
  }

  setAdvancedVisuals(enabled: boolean): void {
    this.waterMaterial.setAdvancedVisuals(enabled);
  }

  setWaterWaves(enabled: boolean): void {
    this.waterMaterial.setWaterWaves(enabled);
  }

  setWaterAppearance(cx: number, cz: number, appearance: WaterAppearanceUpdate | null): void {
    this.generator.setWaterAppearance(cx, cz, appearance);
    this.invalidateWater();
  }

  getChunkWaterState(cx: number, cz: number): ChunkWaterState { return this.generator.getChunkWaterState(cx, cz); }

  getWaterState(wx: number, wz: number) { return this.generator.getWaterState(wx, wz); }

  setWaterForm(cx: number, cz: number, form: WaterForm | null): void { this.setWaterAppearance(cx, cz, {form}); }

  setWaterColors(cx: number, cz: number, colors: Partial<WaterPalette> | null): void { this.setWaterAppearance(cx, cz, {colors}); }

  setWaterFreezing(cx: number, cz: number, freezing: WaterFreezeState | null): void { this.setWaterAppearance(cx, cz, {freezing}); }

  setWaterTemperature(cx: number, cz: number, temperature: number | null): void { this.setWaterAppearance(cx, cz, {temperature}); }

  setWaterFreezeRules(rules: Partial<WaterFreezeRules>): void {
    this.generator.setWaterFreezeRules(rules);
    this.invalidateWater();
  }

  setWaterSeason(phase:number):boolean {
    if(!this.generator.setWaterSeason(phase))return false;
    this.waterRevision++;
    for(const key of this.chunks.keys())this.waterStale.add(key);
    this.iceShell?.clear();return true;
  }

  private invalidateWater(): void {
    this.waterRevision++;
    for (const chunk of this.chunks.values()) this.disposeChunk(chunk);
    this.chunks.clear();this.waterStale.clear();this.iceShell?.clear();
  }

  setRenderDistance(rd: number): void {
    this.renderDistance = rd;
    this.offsets = renderChunkOffsets(rd);
    this.priorityKey='';
    this.centerCx = Number.NaN; // 强制重新评估卸载
  }

  setPerformanceBudget(settings:Pick<GameSettings,'graphicsPreset'>):void {
    const budget=performanceBudget(settings);this.generator.cacheLimit=budget.generatorEntries;this.vegetationDensity=budget.vegetationDensity;
    this.pool.setBudget(budget.generatorEntries,budget.derivedMeshBytes);
    void this.pool.setCount(budget.chunkWorkers).catch(error=>console.warn('[Worker budget]',error));
  }
  setVegetationDistance(distance: number): void {
    this.vegetationDistance = distance;
    for (const chunk of this.chunks.values()) this.updateChunkVisibility(chunk);
    this.instances.sync(this.centerCx,this.centerCz,this.renderDistance,this.vegetationDistance,this.vegetationDensity);
  }

  setDenseVegetation(level: number): void {
    if (level === this.denseVegetation) return;
    this.denseVegetation = level;
    this.invalidateWater();
  }

  onChunkLoaded(fn: (c: LoadedChunk) => void): void {
    this.listeners.push(fn);
  }

  get loadedCount(): number {
    return this.chunks.size;
  }

  get pendingCount(): number {
    return this.pending.size;
  }

  getChunk(cx: number, cz: number): LoadedChunk | undefined {
    return this.chunks.get(chunkKey(cx, cz));
  }

  isLoaded(cx: number, cz: number): boolean {
    return this.chunks.has(chunkKey(cx, cz));
  }

  hasGroundAround(x:number,z:number):boolean {
    for(const cx of [Math.floor((x-2)/CHUNK_SIZE),Math.floor((x+2)/CHUNK_SIZE)])
      for(const cz of [Math.floor((z-2)/CHUNK_SIZE),Math.floor((z+2)/CHUNK_SIZE)])
        if(!this.isLoaded(cx,cz))return false;
    return true;
  }

  getChunkType(cx: number, cz: number): number {
    return this.artificial.getClaim(cx, cz)?.type ?? this.getChunk(cx, cz)?.type ?? this.generator.getChunkType(cx, cz);
  }

  /** 固定三角面碰撞 */
  getHeight(wx: number, wz: number): number {
    return Math.max(terrainHeightAt(wx, wz, this.seed, this.columnBed), iceHeightAt(wx,wz,this.seed,this.columnIce,this.columnIceCoverage));
  }

  async queryShorelines(request:Omit<ShorelineRequest,'worldRevision'|'hydrologyRevision'|'cancelled'>,signal?:AbortSignal):Promise<ShorelineResult> {
    const worldRevision=worldSignature(this.generator),hydrologyRevision=`${this.waterRevision}:${chunkSignature(this.generator)}`;
    this.shorelineClient??=new ShorelineWorkerClient(this.generator,worldRevision);
    const result=await this.shorelineClient.query({...request,worldRevision,hydrologyRevision},signal);
    if(hydrologyRevision!==`${this.waterRevision}:${chunkSignature(this.generator)}`)
      throw Object.assign(new Error('Old shoreline hydrology'),{name:'StaleShorelineRevision'});
    return result;
  }

  getBedHeight(wx:number,wz:number):number {return terrainHeightAt(wx,wz,this.seed,this.columnBed);}
  private get ice():IceShell {return this.iceShell??=new IceShell(this.seed,this.columnBed,this.columnIce,this.columnIceBottom,this.columnIceCoverage);}
  getGroundUnder(wx:number,wz:number,halfWidth:number,feet=Infinity):number {
    return Math.max(terrainGroundUnder(wx,wz,halfWidth,this.seed,this.getBedHeight.bind(this)),this.ice.ground(wx,wz,halfWidth,feet));
  }
  getIceCeiling(x:number,z:number,half:number,head:number):number {return this.ice.ceiling(x,z,half,head);}
  resolveIceOverlap(x:number,y:number,z:number,height:number,half:number):number {
    return this.ice.resolve(x,y,z,height,half,terrainGroundUnder(x,z,half,this.seed,this.getBedHeight.bind(this)));
  }
  isDecorationBlocked(x:number,y:number,z:number,height:number,half:number):boolean {
    return this.ice.blocked(x,y,z,height,half)||this.collisionVolumes.blocked(x,y,z,height,half)||this.flagCollisions.blocked(x,y,z,height,half);
  }
  decorationSpeedFactor(x:number,y:number,z:number,height:number,half:number):number {return this.collisionVolumes.speedFactor(x,y,z,height,half);}
  decorationLandingHeight(x:number,z:number,from:number,to:number,half:number):number {return Math.max(this.collisionVolumes.landingHeight(x,z,from,to,half),this.flagCollisions.landingHeight(x,z,from,to,half));}
  plantContactCount(x:number,y:number,z:number,height:number,half:number):number {return this.collisionVolumes.triggerCount(x,y,z,height,half);}
  resolveFlagSpawn(x:number,z:number,height:number,half:number):{x:number;z:number} {
    const clear=(sx:number,sz:number)=>!this.flagCollisions.blocked(sx,this.getGroundUnder(sx,sz,half)+.01,sz,height,half);
    if(clear(x,z))return {x,z};
    for(const radius of [.75,1.5,2.25,3])for(const [dx,dz] of [[0,1],[1,0],[0,-1],[-1,0],[1,1],[-1,1],[1,-1],[-1,-1]]){
      const sx=x+dx*radius,sz=z+dz*radius;if(clear(sx,sz))return {x:sx,z:sz};
    }
    return {x,z};
  }

  private readonly columnIce = (wx: number, wz: number): number => {
    const bx=Math.floor(wx),bz=Math.floor(wz),cx=Math.floor(bx/CHUNK_SIZE),cz=Math.floor(bz/CHUNK_SIZE);
    const c=this.getChunk(cx,cz);
    return c?.waterField.iceLevels?.[(bz-cz*CHUNK_SIZE)*CHUNK_SIZE+bx-cx*CHUNK_SIZE]
      ?? this.generator.getWaterState(bx,bz).iceLevel;
  };

  private readonly columnIceBottom=(wx:number,wz:number):number=>{
    const x=Math.floor(wx),z=Math.floor(wz),cx=Math.floor(x/CHUNK_SIZE),cz=Math.floor(z/CHUNK_SIZE),c=this.getChunk(cx,cz);
    return c?.waterField.iceBottomLevels?.[(z-cz*CHUNK_SIZE)*CHUNK_SIZE+x-cx*CHUNK_SIZE]??this.generator.getWaterState(x,z).iceBottomLevel;
  };
  private readonly columnIceCoverage = (wx: number, wz: number): number => {
    const bx=Math.floor(wx),bz=Math.floor(wz),cx=Math.floor(bx/CHUNK_SIZE),cz=Math.floor(bz/CHUNK_SIZE);
    const c=this.getChunk(cx,cz);
    return c?.waterField.iceCoverages?.[(bz-cz*CHUNK_SIZE)*CHUNK_SIZE+bx-cx*CHUNK_SIZE]
      ?? this.generator.getWaterState(bx,bz).iceCoverage;
  };

  private readonly columnWater = (wx: number, wz: number): number => {
    const bx = Math.floor(wx), bz = Math.floor(wz);
    const cx = Math.floor(bx / CHUNK_SIZE), cz = Math.floor(bz / CHUNK_SIZE);
    const c = this.getChunk(cx, cz);
    return c ? c.waterLevels[(bz - cz * CHUNK_SIZE) * CHUNK_SIZE + bx - cx * CHUNK_SIZE]
      : this.generator.getWaterLevel(bx, bz);
  };

  private readonly columnBed = (wx: number, wz: number): number => {
    const bx=Math.floor(wx),bz=Math.floor(wz),cx=Math.floor(bx/CHUNK_SIZE),cz=Math.floor(bz/CHUNK_SIZE);
    const c=this.getChunk(cx,cz);
    return c?.waterField.bedLevels?.[(bz-cz*CHUNK_SIZE)*CHUNK_SIZE+bx-cx*CHUNK_SIZE]
      ?? this.generator.getWaterState(bx,bz).bedLevel;
  };

  getWaterLevel(wx: number, wz: number): number {
    if(this.generator.generation.terrainRule==='planet-macro')return patchWaterHeight(wx,wz,this.seed,this.columnBed,this.columnWater);
    return waterHeightAt(wx, wz, this.seed, this.columnBed, this.columnWater, false, this.columnShallow);
  }
  private readonly columnShallow = (wx: number, wz: number): boolean => {
    const x = Math.floor(wx), z = Math.floor(wz), cx = Math.floor(x / CHUNK_SIZE), cz = Math.floor(z / CHUNK_SIZE);
    const chunk = this.getChunk(cx, cz), i = (z - cz * CHUNK_SIZE) * CHUNK_SIZE + x - cx * CHUNK_SIZE;
    if (chunk) return chunk.waterField.kinds[i] === WaterKind.STATIC && !(chunk.waterField.iceCoverages?.[i]! >= .5);
    const state = this.generator.getWaterState(x, z);
    return state.kind === WaterKind.STATIC && state.iceCoverage < .5;
  };

  updateDecorationView(camera:THREE.PerspectiveCamera):void {this.instances.updateVisibility(camera);}
  updateNaturalPhysics(dt:number,enabled:boolean,weatherAt:(cx:number,cz:number)=>number,revision:string):void {
    this.instances.updateNaturalPhysics(dt,enabled,weatherAt,revision);
    this.flagWind.update(dt,enabled,weatherAt,revision,this.decorationMaterial.color);
  }
  flushWrites():Promise<void>{return this.pool.flush();}

  update(playerX: number, playerZ: number, velocityX=0, velocityZ=0, lastFrameMs=0): void {
    if (this.artificialRevision !== this.artificial.revision) {
      for (const chunk of this.chunks.values()) this.updateTerritory(chunk);
      this.artificialRevision = this.artificial.revision;
    }
    const pcx = Math.floor(playerX / CHUNK_SIZE);
    const pcz = Math.floor(playerZ / CHUNK_SIZE);

    if (pcx !== this.centerCx || pcz !== this.centerCz) {
      if(Number.isFinite(this.centerCx)&&Math.hypot(pcx-this.centerCx,pcz-this.centerCz)>this.renderDistance*2&&this.pool.reset()){
        this.pending.clear();this.ready.length=0;
      }
      this.centerCx = pcx;
      this.centerCz = pcz;
      this.unloadFar();
    }

    const deadline=performance.now()+uploadBudgetMs(lastFrameMs);
    this.ready.sort((a,b)=>(a.cx-pcx)**2+(a.cz-pcz)**2-(b.cx-pcx)**2-(b.cz-pcz)**2);
    while (this.ready.length > 0 && performance.now()<deadline) this.addChunk(this.ready.shift()!);
    // 在途任务继续派发，避免阻塞
    if (this.active && this.pool.capacity > 0) {
      const aheadX=Math.max(-2,Math.min(2,Math.round(velocityX*8/CHUNK_SIZE)/4)),aheadZ=Math.max(-2,Math.min(2,Math.round(velocityZ*8/CHUNK_SIZE)/4));
      const priorityKey=`${aheadX},${aheadZ}`;
      if(this.priorityKey!==priorityKey){
        const priority=([x,z]:[number,number])=>Math.abs(x)<=1&&Math.abs(z)<=1?x*x+z*z-100:(x-aheadX)**2+(z-aheadZ)**2;
        this.priorityOffsets=[...this.offsets].sort((a,b)=>priority(a)-priority(b));this.priorityKey=priorityKey;
      }
      const dispatchDeadline=performance.now()+Math.max(DISPATCH_BUDGET_MS,uploadBudgetMs(lastFrameMs));
      for (const [dx, dz] of this.priorityOffsets) {
        if(performance.now()>=dispatchDeadline)break;
        const cx = pcx + dx;
        const cz = pcz + dz;
        const key = chunkKey(cx, cz);
        if (this.chunks.has(key)&&!this.waterStale?.has(key)||this.pending.has(key)) continue;
        if (!this.pool.request(cx, cz, this.artificial.getClaim(cx, cz)?.type, this.generator.getWaterAppearanceOverrides(), this.waterRevision, this.generator.getWaterFreezeRules(), DENSE_VEGETATION_MULTIPLIERS[this.denseVegetation],this.generator.getWaterSeason?.()??null)) break;
        this.pending.add(key);
      }
    }

    this.instances.sync(this.centerCx,this.centerCz,this.renderDistance,this.vegetationDistance,this.vegetationDensity);
  }
  setActive(active:boolean):void {this.active=active;}

  private inRange(cx: number, cz: number, margin: number): boolean {
    const dx = cx - this.centerCx;
    const dz = cz - this.centerCz;
    const r = this.renderDistance + 0.5 + margin;
    return dx * dx + dz * dz <= r * r;
  }

  private unloadFar(): void {
    for (const [key, c] of this.chunks) {
      this.updateChunkVisibility(c);
      if (this.inRange(c.cx, c.cz, 1)) continue;
      this.disposeChunk(c);
      this.chunks.delete(key);this.waterStale.delete(key);this.iceShell?.clear();
    }
  }

  private updateChunkVisibility(chunk: LoadedChunk): void {
    const visible = this.inRange(chunk.cx, chunk.cz, 0);
    if (chunk.terrain) chunk.terrain.visible = visible;
    if (chunk.water) chunk.water.visible = visible;
    if (chunk.decoration) chunk.decoration.visible = visible && (chunk.cx - this.centerCx) ** 2 + (chunk.cz - this.centerCz) ** 2 <= (this.vegetationDistance + .5) ** 2;
    if (chunk.rocks) chunk.rocks.visible = visible;
    if (chunk.mist) chunk.mist.visible = visible;
    if (chunk.flag) chunk.flag.visible = visible;
  }

  private buildMesh(data: MeshData, material: THREE.Material, cx: number, cz: number): THREE.Mesh {
    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position', new THREE.BufferAttribute(data.positions, 3));
    geo.setAttribute('color', new THREE.BufferAttribute(data.colors, 3, true));
    if(data.flagWind)geo.setAttribute('flagWind',new THREE.BufferAttribute(data.flagWind,4));
    if (data.waterDepths) geo.setAttribute('waterDepth', new THREE.BufferAttribute(data.waterDepths, 1));
    if (data.waterFlows) geo.setAttribute('waterFlow',new THREE.BufferAttribute(data.waterFlows,2));
    if (data.waterFoams) geo.setAttribute('waterFoam',new THREE.BufferAttribute(data.waterFoams,3,true));
    if (data.waterFalls) geo.setAttribute('waterFall',new THREE.BufferAttribute(data.waterFalls,1));
    geo.setIndex(new THREE.BufferAttribute(data.indices, 1));
    if(data.normals)geo.setAttribute('normal',new THREE.BufferAttribute(data.normals,3));else geo.computeVertexNormals();
    geo.computeBoundingSphere();
    if(data.flagWind)geo.boundingSphere!.radius+=.35;
    const mesh = new THREE.Mesh(geo, material);
    mesh.position.set(cx * CHUNK_SIZE, 0, cz * CHUNK_SIZE);
    mesh.matrixAutoUpdate = false;
    mesh.updateMatrix();
    return mesh;
  }

  private addChunk(msg: ChunkResultMessage): void {
    const key = chunkKey(msg.cx, msg.cz);
    this.pending.delete(key);
    if ((msg.waterRevision ?? 0) !== this.waterRevision) return;
    // 玩家已远离：丢弃
    if (!this.inRange(msg.cx, msg.cz, 1)) return;
    const previous=this.chunks.get(key);
    if(previous&&!this.waterStale.has(key))return;
    if(previous)this.disposeChunk(previous);
    this.waterStale.delete(key);this.iceShell?.clear();

    const chunk: LoadedChunk = {
      cx: msg.cx,
      cz: msg.cz,
      type: msg.type,
      heights: msg.heights,
      surfaces: msg.surfaces,
      waterLevels: msg.waterLevels,
      waterField:msg.waterField,
      minimap: new ImageData(msg.minimap, CHUNK_SIZE, CHUNK_SIZE),
      terrain: msg.terrain ? this.buildMesh(msg.terrain, this.terrainMaterial, msg.cx, msg.cz) : null,
      water: msg.water ? this.buildMesh(msg.water, this.waterMaterial, msg.cx, msg.cz) : null,
      decoration: msg.decoration ? this.buildMesh(msg.decoration, this.decorationMaterial, msg.cx, msg.cz) : null,
      rocks: msg.rocks ? this.buildMesh(msg.rocks, this.decorationMaterial, msg.cx, msg.cz) : null,
      mist: null,
      flag: null,
      flagBounds: null,
      decorations: msg.decorations ?? [],
      mistEmitters: msg.mist ?? null,
    };
    if (chunk.terrain) this.group.add(chunk.terrain);
    if (chunk.rocks) this.group.add(chunk.rocks);
    if (chunk.decoration) {
      chunk.decoration.name = `decoration:${key}`;
      this.group.add(chunk.decoration);
    }
    if (chunk.water) {
      chunk.water.renderOrder = 1;
      this.group.add(chunk.water);
    }
    if (this.mistEnabled && chunk.mistEmitters) {
      chunk.mist = this.mist.build(chunk.mistEmitters, msg.cx, msg.cz, CHUNK_SIZE);
      if (chunk.mist) this.group.add(chunk.mist);
    }
    this.chunks.set(key, chunk);
    if(msg.colliders)this.collisionVolumes.set(msg.cx,msg.cz,msg.colliders);
    if(msg.instances)this.instances.set(msg.cx,msg.cz,msg.instances);
    if(msg.cacheHit)this.cacheHits++;
    this.updateTerritory(chunk);
    this.updateChunkVisibility(chunk);
    for (const fn of this.listeners) fn(chunk);
  }

  private disposeChunk(c: LoadedChunk): void {
    if(c.flag)this.flagWind.remove(c.flag);
    this.flagCollisions.remove(c.cx,c.cz);
    this.collisionVolumes.remove(c.cx,c.cz);
    this.instances.remove(c.cx,c.cz);
    for (const m of [c.terrain, c.water, c.decoration, c.rocks, c.flag]) {
      if (!m) continue;
      this.group.remove(m);
      m.geometry.dispose();
    }
    c.flagBounds = null;
    if (c.mist) {
      this.mist.disposePoints(c.mist);
      c.mist = null;
    }
  }

  dispose(): void {
    this.shorelineClient?.dispose();
    for (const c of this.chunks.values()) this.disposeChunk(c);
    this.chunks.clear();this.waterStale.clear();this.iceShell?.clear();
    this.pool.dispose();
    this.terrainMaterial.dispose();
    this.waterMaterial.dispose();
    this.decorationMaterial.dispose();
    this.flagWind.dispose();
    this.flagCollisions.clear();
    this.mist.dispose();
    this.instances.dispose();
    this.collisionVolumes.clear();
    this.lighting.dispose();
    this.group.removeFromParent();
  }

  private removeFlag(chunk:LoadedChunk):void {
    this.flagCollisions.remove(chunk.cx,chunk.cz);
    if(chunk.flag){this.flagWind.remove(chunk.flag);this.group.remove(chunk.flag);chunk.flag.geometry.dispose();chunk.flag=null;}
    chunk.flagBounds=null;
  }
  private updateTerritory(chunk: LoadedChunk): void {
    const claim = this.artificial.getClaim(chunk.cx, chunk.cz);
    if (!claim) {this.removeFlag(chunk);return;}
    chunk.type = claim.type;
    if (!allowsNaturalDecoration(claim.type)) {
      this.instances.remove(chunk.cx,chunk.cz);
      this.collisionVolumes.remove(chunk.cx,chunk.cz);
      chunk.decorations = [];
      for (const name of ['decoration', 'rocks'] as const) {
        const mesh = chunk[name];
        if (mesh) { this.group.remove(mesh); mesh.geometry.dispose(); chunk[name] = null; }
      }
    }
    const t = this.artificial.territories.get(claim.faction)!;
    const flagColors = `${t.id}:${t.mainColor}/${t.trimColor}`;
    const flagSite=t.cx===this.artificial.generator.wrap(chunk.cx)&&t.cz===chunk.cz;
    if(!flagSite)this.removeFlag(chunk);
    if (chunk.flag && chunk.flag.userData.colors !== flagColors) {
      this.removeFlag(chunk);
    }
    if (!chunk.flag && flagSite) {
      const h = this.getHeight(chunk.cx * CHUNK_SIZE + CHUNK_SIZE / 2 + .5, chunk.cz * CHUNK_SIZE + CHUNK_SIZE / 2 + .5);
      const data=territoryFlag(t,h);
      chunk.flag = this.buildMesh(data, this.flagWind.material, chunk.cx, chunk.cz);
      this.flagWind.add(chunk.flag,chunk.cx,chunk.cz);
      this.flagCollisions.set(chunk.cx,chunk.cz,offsetFlagColliders(data.colliders,chunk.cx*CHUNK_SIZE,0,chunk.cz*CHUNK_SIZE));
      chunk.flag.name = `territory:${t.id}`;
      chunk.flag.userData.colors = flagColors;
      chunk.flagBounds = meshBounds(chunk.flag);
      chunk.flagBounds.minX-=.35;chunk.flagBounds.maxX+=.35;chunk.flagBounds.minZ-=.35;chunk.flagBounds.maxZ+=.35;
      this.group.add(chunk.flag);
    }
    this.updateChunkVisibility(chunk);
  }
}
