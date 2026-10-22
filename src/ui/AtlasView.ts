import {zoomGameAtlas,limitAtlasZoom} from './atlas/zoom.mjs';
import {drawGameAtlas,drawGameAtlasLegend,suspendGameAtlas,disposeGameAtlas} from './atlas/game-tiles.mjs';
import {clipExplored} from './atlas/exploration-mask.mjs';
import {unproject,atlasCamera,levelFor,TILE} from './atlas/tiles.mjs';
import {AtlasCameraController, type AtlasCameraSnapshot} from './atlas/camera';
import {AtlasInputController, type AtlasInputHost} from './atlas/input';
import { mercatorZ,inverseMercatorZ,MERCATOR_LIMIT } from './AtlasProjection';
import { iconUrl } from './Icons';
import { FLOW_DIRECTIONS, type ChunkInfo } from '../systems/world/WorldContract';
import type { WorldGeneratorBase as WorldGenerator } from '../systems/world/WorldContract';
import { getChunkTypeDef, formatChunkId, isRiverType } from '../systems/world/ChunkTypes';
import { CLIMATES, planetCoordinates } from '../systems/world/WorldSettings';
import { PlanetAtlasLayer, globePoint, overviewColor, type OverviewStyle, type PlanetView } from './PlanetAtlasLayer';
import { chunkName } from '../i18n/content';
import { t } from '../i18n';
import { CHUNK_SIZE } from '../core/config';
import { MAX_BRAID_OFFSET, MAX_MOUTH_LENGTH, MAX_RIVER_RADIUS } from '../systems/world/RiverChannels';
import {MAX_RIVER_BEND} from '../systems/world/RiverGeometry';
import {AtlasSurfaceCache} from './AtlasSurfaceCache';
import { ArtificialWorld, type TerritoryChunkInfo } from '../systems/world/artificial/ArtificialWorld';
import { drawTerritories } from './TerritoryAtlas';
import type {Precipitation} from '../systems/world/weather/Precipitation';
import type { Exploration } from '../systems/world/Exploration';
import {drawMapRiver} from './AtlasMapLayers';
import {normalizeSunHours,elevationRampValue,LAND,type SunWorld} from './atlas/relief.mjs';
import {defaultLayers,normalizeLayers,clampDensity,type LayerState} from './atlas/layers.mjs';
import {contourUnit,bandPosition,terraceShade} from './atlas/contour.mjs';
import type {MapSummary} from '../systems/world/ChunkCache';

/** 画布配色 */
const INK_BG = '#241c13';
const PAPER_TEXT = '#eadfc0';
const CINNABAR_MARK = '#c2333f';
const AMBER_MARK = '#e0b04a';
const CANVAS_FONT = '"Qianlin Song", "Songti SC", serif';
/** 全览每帧最多重采样的瓦片数 */
const OVERVIEW_TILE_BUDGET = 720;
/** 纹理档只做统计，采样更稀 */
const OVERVIEW_STAT_BUDGET = 160;

export interface ViewportStats {
  centerCx: number;
  centerCz: number;
  scale: number;
  visibleChunks: number;
  climateCounts: [number, number, number, number, number];
  climatePercents: [number, number, number, number, number];
  matchedCount: number;
  matchedPercent: number;
  activeFilterName: string;
  level: 'globe' | 'overview' | 'chunks';
}

export interface AtlasPlayerMarker {
  x: number;
  z: number;
  yaw: number;
}

export interface AtlasViewOptions {
  initialPlanetView?: PlanetView;
  maxZoomLevel?: number;
  previewWorkers?:number;
  worldKey?:string;
  exploration?:Exploration;
  survivalPreview?:boolean;
  onDestination?:(point:{x:number;z:number})=>void;
  precipitation?: Precipitation;
  artificial?: ArtificialWorld;
  canvas: HTMLCanvasElement;
  generator: WorldGenerator;
  onChunkActivate?: (info: TerritoryChunkInfo) => void;
  onChunkSelect?: (info: TerritoryChunkInfo) => void;
  onStatsUpdate?: (stats: ViewportStats) => void;
  playerMarker?: () => AtlasPlayerMarker | null;
  /** 已知视图中心时直接使用，避免触发全星球流域求解 */
  initialCenter?: { cx: number; cz: number };
}

export class AtlasView {
  readonly maxZoomLevel?: number;
  private readonly worldKey?:string;
  private readonly exploration?:Exploration;
  private readonly survivalPreview:boolean;
  private survivalZoomLevel=8;
  private readonly onDestination?:AtlasViewOptions['onDestination'];
  setPreviewWorkers(count:number):void {this.surfaceCache.setWorkerLimit(count);}
  private showRainClouds = false;
  private rainOpacity = 1;
  private precipitation?: Precipitation;
  private cloudLayer?: {canvas: HTMLCanvasElement; key: string};
  private readonly icons = new Map<string, HTMLImageElement>();
  private readonly canvas: HTMLCanvasElement;
  private readonly ctx: CanvasRenderingContext2D;
  private generator: WorldGenerator;
  private artificial: ArtificialWorld;
  private territoryDiscovery: Generator<void> | null = null;
  private territoryDiscoveryKey = '';

  // 视图控制与输入控制器
  private readonly camera!: AtlasCameraController;
  private input!: AtlasInputController;
  private _centerCx?: number;
  private _centerCz?: number;
  private _panX?: number;
  private _panY?: number;
  private _scale?: number;
  private _baseCellSize?: number;
  private _planetView?: PlanetView;
  private _preferredOverview?: PlanetView;

  get centerCx(): number { return this.camera ? this.camera.centerCx : (this._centerCx ?? 0); }
  set centerCx(v: number) { if (this.camera) this.camera.centerCx = v; else this._centerCx = v; }
  get centerCz(): number { return this.camera ? this.camera.centerCz : (this._centerCz ?? 0); }
  set centerCz(v: number) { if (this.camera) this.camera.centerCz = v; else this._centerCz = v; }
  get panX(): number { return this.camera ? this.camera.panX : (this._panX ?? 0); }
  set panX(v: number) { if (this.camera) this.camera.panX = v; else this._panX = v; }
  get panY(): number { return this.camera ? this.camera.panY : (this._panY ?? 0); }
  set panY(v: number) { if (this.camera) this.camera.panY = v; else this._panY = v; }
  get scale(): number { return this.camera ? this.camera.scale : (this._scale ?? 1.0); }
  set scale(v: number) { if (this.camera) this.camera.scale = v; else this._scale = v; }
  get baseCellSize(): number { return this.camera ? this.camera.baseCellSize : (this._baseCellSize ?? 36); }
  set baseCellSize(v: number) { if (this.camera) this.camera.baseCellSize = v; else this._baseCellSize = v; }
  get planetView(): PlanetView { return this.camera ? this.camera.planetView : (this._planetView ?? 'globe'); }
  set planetView(v: PlanetView) { if (this.camera) this.camera.planetView = v; else this._planetView = v; }
  get preferredOverview(): PlanetView { return this.camera ? this.camera.preferredOverview : (this._preferredOverview ?? 'globe'); }
  set preferredOverview(v: PlanetView) { if (this.camera) this.camera.preferredOverview = v; else this._preferredOverview = v; }
  get isDragging(): boolean { return this.input?.dragging ?? false; }

  getCameraSnapshot(): AtlasCameraSnapshot {
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    return this.camera.snapshot(this.canvas.width / dpr, this.canvas.height / dpr);
  }
  private readonly planetLayer: PlanetAtlasLayer;
  private maskClimateLayer?:PlanetAtlasLayer;
  private readonly surfaceCache: AtlasSurfaceCache;
  private showChunkColors=false;
  private showSatellite=false;
  private spawnChunk: { cx: number; cz: number } | null = null;

  // 状态控制（进入2D地图界面后，默认只显示色块）
  private showChunkInfo = false;     // 显示/隐藏区块信息（编号、类型）
  /** 叠加图层：顺序即合成顺序，列表在前=画在上层 */
  private mapLayers:LayerState[]=defaultLayers();
  /** 等高线密度档 0..4，越大越密 */
  private contourDensity=2;
  private sunHours=10.5;             // 拟真光照的时刻
  private showFlowArrows=false;      // 水系流向箭头
  private showClimate = false;       // 显示气候分类
  private activeClimateFilter: number | null = null; // 单个气候高亮（0..4）

  // 区块筛选控制（红色轮廓线）
  private filterTypeId: number | null = null;
  private filterCategory: string | null = null;

  // 交互状态
  private selectedChunk: { cx: number; cz: number } | null = null;
  private hoveredChunk: { cx: number; cz: number } | null = null;







  private onChunkSelect?: (info: TerritoryChunkInfo) => void;
  private onChunkActivate?: (info: TerritoryChunkInfo) => void;
  private onStatsUpdate?: (stats: ViewportStats) => void;
  private playerMarker?: () => AtlasPlayerMarker | null;
  private resizeObserver: ResizeObserver | null = null;
  private renderScheduled = false;
  private readonly events = new AbortController();
  private destroyed = false;
  private active = true;
  private refineAfter=0;
  private refinementTimer:ReturnType<typeof setTimeout>|undefined;

  private get refinementReady():boolean{return !this.isDragging&&performance.now()>=(this.refineAfter??0);}
  private deferRefinement():void{
    this.refineAfter=performance.now()+110;
    clearTimeout(this.refinementTimer);
    this.refinementTimer=setTimeout(()=>{if(this.active&&!this.destroyed)this.requestRender();},110);
  }

  constructor(options: AtlasViewOptions) {
    this.maxZoomLevel=options.maxZoomLevel;
    this.worldKey=options.worldKey;
    this.survivalPreview=!!options.survivalPreview;this.onDestination=options.onDestination;
    this.exploration=options.exploration;
    this.precipitation = options.precipitation;
    this.canvas = options.canvas;
    if(this.exploration){this.canvas.style.backgroundColor='var(--ql-paper)';this.canvas.style.backgroundImage='var(--ql-paper-weave)';}
    for (const name of ['flag-pin', 'flow-e', 'flow-ne', 'flow-n', 'flow-nw', 'flow-w', 'flow-sw', 'flow-s', 'flow-se']) {
      const image = new Image(); image.onload = () => { if (!this.destroyed) this.requestRender(); };
      image.src = iconUrl(name); this.icons.set(name, image);
    }
    this.ctx = this.canvas.getContext('2d')!;
    this.generator = options.generator;
    const isPlanetMode = this.generator.generation.mode === 'planet';
    const equatorSize = this.generator.generation.planet?.equatorChunks ?? 1024;
    this.camera = new AtlasCameraController({
      equatorChunks: equatorSize,
      isPlanet: isPlanetMode,
      initialCenter: options.initialCenter,
      initialView: isPlanetMode ? options.initialPlanetView ?? 'globe' : 'projection',
      preferredOverview: isPlanetMode ? options.initialPlanetView ?? 'globe' : 'projection',
      onViewChange: () => { if (this.active) this.requestRender(); },
    });
    this.showSatellite=this.generator.generation.terrainRule==='planet-macro';
    this.artificial = options.artificial ?? new ArtificialWorld(this.generator);
    this.planetLayer = new PlanetAtlasLayer(this.generator, () => { if (this.active) this.requestRender(); });
    this.surfaceCache=new AtlasSurfaceCache(this.generator,()=>{if(this.active)this.requestRender();},options.worldKey,options.previewWorkers);
    this.onChunkSelect = options.onChunkSelect;
    this.onChunkActivate = options.onChunkActivate;
    this.onStatsUpdate = options.onStatsUpdate;
    this.playerMarker = options.playerMarker;

    this.initEvents();
    this.setupResize();
    if (options.initialCenter) {this.spawnChunk=options.initialCenter;this.centerAt(options.initialCenter.cx, options.initialCenter.cz);}
    else this.centerSpawn();
    if (this.isPlanet&&!this.exploration&&!this.survivalPreview) { this.planetView = this.preferredOverview; this.showOverview(); }
  }

  setGenerator(generator: WorldGenerator, artificial?: ArtificialWorld): void {
    disposeGameAtlas(this);
    const view = this.planetView;
    this.generator = generator;
    this.camera.equatorChunks = generator.generation.planet?.equatorChunks ?? 1024;
    this.camera.isPlanet = generator.generation.mode === 'planet';
    if(generator.generation.terrainRule!=='planet-macro')this.showSatellite=false;
    this.artificial = artificial ?? new ArtificialWorld(generator);
    this.territoryDiscovery = null; this.territoryDiscoveryKey = '';
    this.spawnChunk = null;
    this.planetLayer.setGenerator(generator);
    this.maskClimateLayer?.setGenerator(generator);
    this.surfaceCache.setGenerator(generator);
    this.centerSpawn();
    if (this.isPlanet) { this.planetView = view; this.showOverview(); }
    this.requestRender();
  }

  /** 出生区块只查一次，避免每帧重新扫描 */
  private spawn(): { cx: number; cz: number } {
    const faction = this.artificial.playerFactionId ? this.artificial.territories.get(this.artificial.playerFactionId) : undefined;
    if (faction) return faction;
    return this.spawnChunk ??= this.generator.findSpawnChunk();
  }

  centerSpawn(): void {
    const spawn = this.spawn();
    this.centerAt(spawn.cx, spawn.cz);
  }

  centerAt(cx: number, cz: number): void {
    if(this.survivalPreview)this.survivalZoomLevel=8;
    this.scale = 1;
    this.planetView = this.preferredOverview==='mercator'?'mercator':'projection';
    this.centerCx = cx;
    this.centerCz = cz;
    this.panX = 0;
    this.panY = 0;
    this.fitSurvivalPreview();
    this.selectedChunk = { cx, cz };
    if (this.onChunkSelect && (!this.worldKey || this.generator.cachedChunkInfo(cx,cz)) && (!this.exploration || this.exploration.has(cx,cz))) {
      this.onChunkSelect(this.artificial.info(cx, cz));
    }
    this.requestRender();
  }

  setActive(value: boolean): void {
    this.active = value;
    suspendGameAtlas(this,!value);
    this.input?.reset();
    this.canvas.style.cursor = 'grab';
    if (value) { this.surfaceCache.resume(); this.requestRender(); } else this.surfaceCache.idle();
  }

  resetView(): void {
    if (this.isPlanet) { this.planetView = this.preferredOverview; this.showOverview(); return; }
    this.scale = 1.0;
    this.panX = 0;
    this.panY = 0;
    this.requestRender();
  }

  zoomBy(factor: number, clientX?: number, clientY?: number): void {
    if(this.survivalPreview&&this.isPlanet){
      if(!(factor>0)||!Number.isFinite(factor))return;
      this.survivalZoomLevel=Math.max(6,Math.min(8,this.survivalZoomLevel+Math.log2(factor)));
      this.fitSurvivalPreview();this.requestRender();return;
    }
    if(zoomGameAtlas(this,factor,clientX,clientY))return;
    this.deferRefinement();
    const rect = this.canvas.getBoundingClientRect();
    const cx = clientX !== undefined ? clientX - rect.left : this.canvas.clientWidth / 2;
    const cy = clientY !== undefined ? clientY - rect.top : this.canvas.clientHeight / 2;

    const oldScale = this.scale;
    const requestedScale=Math.max(this.isPlanet?Math.min(this.overviewScale,.039):.35,Math.min(8,oldScale*factor));
    const newScale=this.isPlanet&&!this.isGlobe?Math.max(requestedScale,this.projectionMinScale):requestedScale;
    if(this.isPlanet&&!this.isGlobe&&this.preferredOverview==='globe'&&factor<1&&requestedScale<.04){
      const center=this.getCenterCoordinates();this.centerCx=center.cx;this.centerCz=center.cz;this.panX=this.panY=0;this.planetView='globe';this.scale=Math.min(requestedScale,this.overviewScale);this.requestRender();return;
    }
    if (newScale === oldScale) return;

    if (this.isPlanet && this.planetView === 'globe') {
      const target = this.getChunkAtPoint(cx, cy);
      this.scale = newScale;
      if (newScale >= this.overviewScale * 5) {
        const center = target ?? { cx: this.centerCx, cz: this.centerCz };
        const size = this.generator.generation.planet.equatorChunks;
        // 将球面的角尺度转换为投影的区块尺度
        this.scale = Math.min(8, this.globeRadius * Math.PI * 2 / size / this.baseCellSize);
        this.centerCx = Math.round(center.cx); this.centerCz = Math.round(center.cz);
        const cell = this.baseCellSize * this.scale;
        this.panX = cx - this.canvas.clientWidth / 2 - cell / 2;
        this.panY = cy - this.canvas.clientHeight / 2 - cell / 2;
        this.planetView = this.preferredOverview==='mercator'?'mercator':'projection';
      }
      this.requestRender(); return;
    }

    // 以缩放中心点保持不变进行视角平移调整
    const midX = this.canvas.clientWidth / 2;
    const midY = this.canvas.clientHeight / 2;
    const curRelX = cx - midX - this.panX;
    const curRelY = cy - midY - this.panY;
    const ratio = newScale / oldScale;

    this.panX -= curRelX * (ratio - 1);
    this.panY -= curRelY * (ratio - 1);
    this.scale = newScale;
    this.requestRender();
  }

  private get isPlanet(): boolean { return this.generator.generation.mode === 'planet'; }
  private get projectionMinScale():number{return this.canvas.clientWidth/(this.generator.generation.planet.equatorChunks*this.baseCellSize);}
  private get overviewScale(): number {
    const size = this.generator.generation.planet.equatorChunks;
    const fit=Math.min(this.canvas.clientWidth/size,this.canvas.clientHeight/(this.isMercator?size:size/2))*.88/this.baseCellSize;
    return this.isGlobe?fit:Math.max(fit,this.projectionMinScale);
  }
  private get globeRadius(): number {
    return Math.min(this.canvas.clientWidth, this.canvas.clientHeight) * .43 * this.scale / this.overviewScale;
  }
  private get isMercator():boolean{return this.isPlanet&&this.planetView==='mercator';}
  private mapZ(z:number):number{return this.isMercator?mercatorZ(z,this.generator.generation.planet.equatorChunks):z;}
  private worldZ(y:number):number{return this.isMercator?inverseMercatorZ(y,this.generator.generation.planet.equatorChunks):y;}
  private get isGlobe(): boolean { return this.isPlanet && this.planetView === 'globe'; }

  setPlanetView(view: PlanetView): void {
    if (!this.isPlanet) return;
    if(this.survivalPreview){this.fitSurvivalPreview();this.requestRender();return;}
    const center = this.getCenterCoordinates();
    this.centerCx = Math.round(center.cx); this.centerCz = Math.round(center.cz);
    this.panX = 0; this.panY = 0; this.planetView = view;
    this.preferredOverview = view;
    this.scale = this.overviewScale;
    this.requestRender();
  }

  showOverview(): void {
    if(this.survivalPreview&&this.isPlanet){this.centerSpawn();return;}
    this.panX = 0; this.panY = 0; this.scale = this.overviewScale;
    if (this.planetView === 'globe'&&this.exploration&&!this.survivalPreview) {
      const marker=this.playerMarker?.();
      if(marker){this.centerCx=marker.x/CHUNK_SIZE-.5;this.centerCz=marker.z/CHUNK_SIZE-.5;}
    } else if (this.planetView === 'globe') {
      const size = this.generator.generation.planet.equatorChunks;
      this.centerCx = size * 20 / 360 - .5; this.centerCz = -size * 20 / 360 - .5;
    } else { this.centerCx = 0; this.centerCz = 0; }
    this.requestRender();
  }

  private panBy(dx: number, dy: number): void {
    if(this.survivalPreview)return;
    this.deferRefinement();
    if (this.isGlobe) {
      const size = this.generator.generation.planet.equatorChunks;
      this.centerCx -= dx / this.globeRadius * size / (Math.PI * 2);
      this.centerCz = Math.max(-size / 4 + 1, Math.min(size / 4 - 1,
        this.centerCz - dy / this.globeRadius * size / (Math.PI * 2)));
    } else {
      this.panX += dx; this.panY += dy;
      if (this.isPlanet) {
        const size = this.generator.generation.planet.equatorChunks, cell = this.baseCellSize * this.scale;
        this.panY = Math.max((this.mapZ(this.centerCz) - (this.isMercator?size/2:size/4)) * cell, Math.min((this.mapZ(this.centerCz) + (this.isMercator?size/2:size/4)) * cell, this.panY));
      }
    }
  }

  // ---- 控制开关接口 ----

  setShowChunkInfo(show: boolean): void {
    this.showChunkInfo = show;
    this.requestRender();
  }

  getShowChunkInfo(): boolean {
    return this.showChunkInfo;
  }

  /** 图层状态：顺序、开关与透明度 */
  setMapLayers(layers:LayerState[]):void{
    const next=normalizeLayers(layers);
    if(JSON.stringify(next)===JSON.stringify(this.mapLayers))return;
    this.mapLayers=next;this.requestRender();
  }
  getMapLayers():LayerState[]{return this.mapLayers;}
  /** 等高线密度 0..4，越大越密 */
  setContourDensity(value:number):void{
    const next=clampDensity(value);
    if(next===this.contourDensity)return;
    this.contourDensity=next;this.requestRender();
  }
  getContourDensity():number{return this.contourDensity;}
  /** 某图层是否开启 */
  layerEnabled(id:string):boolean{return (this.mapLayers??defaultLayers()).some(l=>l.id===id&&l.on);}
  layerOpacity(id:string):number{const hit=(this.mapLayers??defaultLayers()).find(l=>l.id===id);return hit?hit.opacity:1;}
  /** 日照时刻，0-24 时 */
  setSunHours(hours:number):void{
    const next=normalizeSunHours(hours);
    if(next===this.sunHours)return;
    this.sunHours=next;
    if(this.layerEnabled('daylight'))this.requestRender();
  }
  getSunHours():number{return this.sunHours;}
  /** 太阳方位输入：地轴倾角与自转方向来自世界设定 */
  sunWorld():SunWorld{
    const planet=this.generator.generation.planet as {axialTilt?:number;prograde?:boolean};
    return {axialTilt:planet?.axialTilt,prograde:planet?.prograde};
  }
  /** 流向箭头独立开关，不再跟地势绑定 */
  setShowFlowArrows(show:boolean):void{this.showFlowArrows=show;this.requestRender();}
  getShowFlowArrows():boolean{return this.showFlowArrows;}

  setShowClimate(show: boolean): void {
    this.showClimate = show;
    this.requestRender();
  }

  getShowClimate(): boolean {
    return this.showClimate;
  }

  setShowSatellite(show:boolean):void{this.showSatellite=show&&this.generator.generation.terrainRule==='planet-macro';this.requestRender();}
  setShowChunkColors(show:boolean):void{this.showChunkColors=show;this.requestRender();}
  getShowChunkColors():boolean{return this.showChunkColors;}
  setShowRainClouds(show: boolean): void { this.showRainClouds = show && !!this.precipitation; this.requestRender(); }
  /** 雨云层透明度 */
  setRainOpacity(value:number):void{this.rainOpacity=Math.max(0,Math.min(1,value));this.requestRender();}
  getShowRainClouds(): boolean { return this.showRainClouds; }

  setActiveClimateFilter(climateIndex: number | null): void {
    this.activeClimateFilter = climateIndex;
    this.requestRender();
  }

  getActiveClimateFilter(): number | null {
    return this.activeClimateFilter;
  }

  setFilterTypeId(typeId: number | null): void {
    this.filterTypeId = typeId;
    this.filterCategory = null;
    this.requestRender();
  }

  setFilterCategory(category: string | null): void {
    this.filterCategory = category;
    this.filterTypeId = null;
    this.requestRender();
  }

  clearFilter(): void {
    this.filterTypeId = null;
    this.filterCategory = null;
    this.activeClimateFilter = null;
    this.requestRender();
  }

  getSelectedChunk(): ChunkInfo | null {
    if (!this.selectedChunk) return null;
    if(this.exploration&&(!this.exploration.has(this.selectedChunk.cx,this.selectedChunk.cz)||!this.cachedMapInfo(this.selectedChunk.cx,this.selectedChunk.cz)))return null;
    return this.artificial.info(this.selectedChunk.cx, this.selectedChunk.cz);
  }

  getScalePercent(): number {
    return Math.round(this.scale * 100);
  }

  getCenterCoordinates(): { cx: number; cz: number } {
    const cellSize = this.baseCellSize * this.scale;
    const cx = this.centerCx - Math.round(this.panX / cellSize);
    const cz = Math.round(this.worldZ(this.mapZ(this.centerCz)-this.panY/cellSize));
    return { cx, cz };
  }

  // ---- 渲染调度 ----

  requestRender(): void {
    if (this.destroyed) return;
    if(this.survivalPreview)this.fitSurvivalPreview();
    limitAtlasZoom(this);
    if (this.renderScheduled) return;
    this.renderScheduled = true;
    requestAnimationFrame(() => {
      this.renderScheduled = false;
      if (!this.destroyed && this.canvas.clientWidth > 0 && this.canvas.clientHeight > 0) this.draw();
    });
  }

  private setupResize(): void {
    this.resizeObserver = new ResizeObserver(() => {
      this.resizeCanvas();
      this.requestRender();
    });
    this.resizeObserver.observe(this.canvas);
    this.resizeCanvas();
  }

  private resizeCanvas(): void {
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    const rect = this.canvas.getBoundingClientRect();
    const width = Math.max(100, Math.floor(rect.width));
    const height = Math.max(100, Math.floor(rect.height));

    if (this.canvas.width !== width * dpr || this.canvas.height !== height * dpr) {
      this.canvas.width = width * dpr;
      this.canvas.height = height * dpr;
    }
    if(this.isPlanet&&!this.isGlobe)this.scale=Math.max(this.scale,this.projectionMinScale);
    this.fitSurvivalPreview();
  }
  private fitSurvivalPreview():void {
    if(!this.survivalPreview||!this.isPlanet)return;
    const spawn=this.spawn(),marker=this.playerMarker?.();
    this.planetView=this.preferredOverview='projection';
    this.centerCx=marker?marker.x/CHUNK_SIZE:spawn.cx+.5;
    this.centerCz=marker?marker.z/CHUNK_SIZE:spawn.cz+.5;
    this.panX=this.panY=0;
    this.scale=TILE*2**this.survivalZoomLevel/(this.generator.generation.planet.equatorChunks*this.baseCellSize)*(1-1e-10);
  }

  private isChunkMatched(info: ChunkInfo): boolean {
    return this.isTypeMatched(info.type);
  }

  /** 只按类型判定，供全览样式在烘焙时复用 */
  private isTypeMatched(type: number): boolean {
    if (this.filterTypeId !== null) {
      return type === this.filterTypeId;
    }
    if(this.filterCategory&&this.generator.generation.terrainRule==='planet-macro'){
      const land=Math.floor(type/100),material=Math.floor(type/10)%10,terrain=type%10;
      return this.filterCategory==='river'?type===4||(type>=5&&type<100&&type%10===5):this.filterCategory==='water'?[2,4,5,22,32].includes(type)||(type>=5&&type<100&&type%10===5):this.filterCategory==='mountain'?land>0&&terrain===4:this.filterCategory==='snow'?land===1:this.filterCategory==='forest'?land>=2&&material===2:false;
    }
    if (this.filterCategory) {
      switch (this.filterCategory) {
        case 'river':
          return isRiverType(type);
        case 'water':
          return type === 2 || isRiverType(type) || type % 100 === 2 || type === 105 || type===11;
        case 'mountain':
          return (
            type === 3 ||
            type === 8 ||
            type % 100 === 3 ||
            type % 100 === 8 ||
            type === 406
          );
        case 'snow':
          return type === 406 || type === 409;
        case 'forest':
          return (
            type === 6 ||
            type % 100 === 4 ||
            type % 100 === 5 ||
            type % 100 === 6 ||
            type === 109
          );
      }
    }
    return false;
  }

  private getActiveFilterLabel(): string {
    if (this.filterTypeId !== null) {
      const def = getChunkTypeDef(this.filterTypeId,this.generator.generation.terrainRule==='planet-macro');
      return `${formatChunkId(def.id)} ${chunkName(def)}`;
    }
    if (this.filterCategory) {
      const key = `atlas.filter.${this.filterCategory}`;
      const label = t(key);
      return label === key ? this.filterCategory : label;
    }
    return '';
  }

  // ---- 核心绘制逻辑 ----

  private draw(): void {
    if(!this.active)return;
    const ctx=this.ctx,dpr=Math.min(window.devicePixelRatio||1,2),width=this.canvas.width/dpr,height=this.canvas.height/dpr,cell=this.baseCellSize*this.scale;
    ctx.save();
    if(this.exploration&&!this.survivalPreview){
      ctx.scale(dpr,dpr);ctx.clearRect(0,0,width,height);
      if(this.isGlobe){
        const p=planetCoordinates(this.centerCx,this.centerCz,this.generator.generation.planet.equatorChunks);
        this.maskClimateLayer??=new PlanetAtlasLayer(this.generator,()=>{if(this.active)this.requestRender();},true);
        this.maskClimateLayer.drawGlobe(ctx,width,height,this.globeRadius,p.longitude,p.latitude,{climate:true,relief:false,rivers:false,satellite:false,climateFilter:null,filterKey:'',matches:()=>false});
      }else this.drawClimateBackdrop(ctx,width,height,cell);
      clipExplored(ctx,atlasCamera(this,width,height),this.exploration,this.generator.generation.planet.equatorChunks);
      ctx.scale(1/dpr,1/dpr);
    }
    this.drawWorld();ctx.restore();
    ctx.save();ctx.scale(dpr,dpr);this.drawPlayerMarker(ctx,width,height,cell);ctx.restore();
    drawGameAtlasLegend(this);
  }

  private drawWorld(): void {
    if(drawGameAtlas(this))return;
    this.surfaceCache.beginFrame();
    const ctx = this.ctx;
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    const width = this.canvas.width / dpr;
    const height = this.canvas.height / dpr;

    ctx.save();
    ctx.scale(dpr, dpr);

    // 背景：与视口外框同为暖墨色，让地图色块浮出来
    if(this.exploration)ctx.clearRect(0,0,width,height);
    else{ctx.fillStyle = INK_BG;ctx.fillRect(0, 0, width, height);}
    if(this.isPlanet&&!this.isGlobe){this.drawFlatPlanet(ctx,width,height);ctx.restore();return;}

    const overviewCell = Math.max(12, Math.sqrt(width * height / 1024));
    if (this.isPlanet && (this.isGlobe || this.baseCellSize * this.scale < overviewCell)) {
      this.drawPlanetOverview(ctx, width, height);
      ctx.restore(); return;
    }

    const cellSize = this.baseCellSize * this.scale;
    const surfaceMode=!this.showSatellite&&!this.showChunkColors&&!this.showClimate&&cellSize>=Math.max(24,Math.sqrt(width*height/512));
    const detailWanted=this.refinementReady&&(this.showChunkInfo||(surfaceMode&&cellSize>=72));
    const halfW = width / 2;
    const halfH = height / 2;

    // 可视区块计算
    const minCx = this.centerCx + Math.floor((-halfW - this.panX) / cellSize) - 1;
    const maxCx = this.centerCx + Math.ceil((halfW - this.panX) / cellSize) + 1;
    const minCz = this.centerCz + Math.floor((-halfH - this.panY) / cellSize) - 1;
    const maxCz = this.centerCz + Math.ceil((halfH - this.panY) / cellSize) + 1;

    // 统计数据
    const climateCounts: [number, number, number, number, number] = [0, 0, 0, 0, 0];
    const terrainCounts = new Map<number, number>();
    let visibleCount = 0;
    let matchedCount = 0;
    const isFiltering = this.filterTypeId !== null || this.filterCategory !== null;

    // 收集可视区块
    const visibleChunks: { info: TerritoryChunkInfo; sx: number; sy: number }[] = [];

    // 生成预算内未完成的区块，先垫全览底图避免空洞
    if (this.isPlanet) {
      const style = this.overviewStyle();
      this.planetLayer.drawProjection(ctx, width, height, cellSize, this.centerCx, this.centerCz, this.panX, this.panY, style, false);
    }
    let pending = false;
    const cells = this.centerCells(minCx,maxCx,minCz,maxCz,this.centerCx-this.panX/cellSize,this.centerCz-this.panY/cellSize);

    for (const [cx,cz] of cells) {
        if (this.isPlanet && (cz < -this.generator.generation.planet.equatorChunks / 4 || cz >= this.generator.generation.planet.equatorChunks / 4)) continue;
        const sx = halfW + this.panX + (cx - this.centerCx) * cellSize;
        const sy = halfH + this.panY + (cz - this.centerCz) * cellSize;

        if (sx + cellSize < 0 || sx > width || sy + cellSize < 0 || sy > height) {
          continue;
        }

        // 底色先画，细节由 Worker 补齐
        const basicTile=this.surfaceCache.get(cx,cz,(sx+cellSize/2-width/2)**2+(sy+cellSize/2-height/2)**2,surfaceMode&&cellSize>=72&&this.refinementReady,false,detailWanted);
        const basicInfo=basicTile?.summary?.info;
        if (!this.generator.cachedChunkInfo(cx, cz) && !basicInfo) {
          pending = true;
          continue;
        }

        const info = this.generator.cachedChunkInfo(cx,cz)?this.artificial.info(cx,cz):basicInfo!;
        visibleChunks.push({ info, sx, sy });

        visibleCount++;
        climateCounts[info.climate]++;
        terrainCounts.set(info.type, (terrainCounts.get(info.type) ?? 0) + 1);

        if (isFiltering && this.isChunkMatched(info)) {
          matchedCount++;
        }
    }

    // 1. 绘制基础底色（默认模式下只显示色块）
    for (const item of visibleChunks) {
      const { info, sx, sy } = item;
      const def = getChunkTypeDef(info.type,this.generator.generation.terrainRule==='planet-macro');

      let color = this.showChunkColors ? def.mapColor : `rgb(${overviewColor(info,false,false,false,this.generator.generation.terrainRule==='planet-macro').join(',')})`;
      const wantImage=cellSize>=72&&this.refinementReady;
      const tile=surfaceMode?this.surfaceCache.get(info.cx,info.cz,(sx+cellSize/2-width/2)**2+(sy+cellSize/2-height/2)**2,wantImage):undefined;
      if(tile)color=tile.average;
      if(this.showSatellite&&!this.showChunkColors&&!this.showClimate){color=def.mapColor;}
      if (this.showClimate) {
        color = CLIMATES[info.climate].color;
        // 如果激活了单项气候高亮，则弱化其他气候色块
        if (this.activeClimateFilter !== null && info.climate !== this.activeClimateFilter) {
          color = this.dimHexColor(color, 0.28);
        }
      }

      ctx.fillStyle = color;
      ctx.fillRect(sx, sy, cellSize, cellSize);
      if(tile?.image&&cellSize>=72){ctx.imageSmoothingEnabled=false;ctx.drawImage(tile.image,sx,sy,cellSize,cellSize);}
      if(this.layerEnabled('contour')&&!this.showSatellite&&!(tile?.image&&cellSize>=72))this.drawContourChunk(ctx,info,sx,sy,cellSize,cellSize);

      // 区块网格线（微弱分割）
      ctx.strokeStyle = 'rgba(0, 0, 0, 0.12)';
      ctx.lineWidth = 1;
      ctx.strokeRect(sx, sy, cellSize, cellSize);
    }

    if((!surfaceMode||cellSize<72)&&!this.showClimate&&cellSize>=6){
      ctx.save();
      this.drawRiverLayer(ctx,width,height,cellSize,minCx,maxCx,minCz,maxCz);
      if(this.showFlowArrows&&cellSize>=10)for(const {info,sx,sy} of visibleChunks)this.drawFlowArrow(ctx,info,sx,sy,cellSize,cellSize);
      ctx.restore();
    }

    // 3. 显示区块信息（编号、类型）
    if (this.showChunkInfo) {
      for (const item of visibleChunks) {
        const { info, sx, sy } = item;
        const def = getChunkTypeDef(info.type,this.generator.generation.terrainRule==='planet-macro');
        const code = formatChunkId(info.type);
        const name = chunkName(def) + (info.territory ? ` Lv.${info.territory.level}` : '');

        const midX = sx + cellSize * 0.5;
        const midY = sy + cellSize * 0.5;

        ctx.save();
        ctx.textAlign = 'center';
        ctx.textBaseline = 'middle';

        if (cellSize >= 38) {
          // 放大状态：同时显示编号与类型名称
          const codeSize = Math.max(9, Math.round(cellSize * 0.23));
          const nameSize = Math.max(8, Math.round(cellSize * 0.21));

          // 编号（上方）
          ctx.font = `bold ${codeSize}px ${CANVAS_FONT}`;
          ctx.strokeStyle = 'rgba(0, 0, 0, 0.8)';
          ctx.lineWidth = 2.5;
          ctx.strokeText(code, midX, midY - cellSize * 0.16);
          ctx.fillStyle = '#ffffff';
          ctx.fillText(code, midX, midY - cellSize * 0.16);

          // 类型（下方）
          ctx.font = `500 ${nameSize}px ${CANVAS_FONT}`;
          ctx.strokeText(name, midX, midY + cellSize * 0.18);
          ctx.fillStyle = '#f0fdf4';
          ctx.fillText(name, midX, midY + cellSize * 0.18);
        } else if (cellSize >= 20) {
          // 中等尺寸：显示编号
          const codeSize = Math.max(8, Math.round(cellSize * 0.32));
          ctx.font = `bold ${codeSize}px ${CANVAS_FONT}`;
          ctx.strokeStyle = 'rgba(0, 0, 0, 0.75)';
          ctx.lineWidth = 2;
          ctx.strokeText(code, midX, midY);
          ctx.fillStyle = '#ffffff';
          ctx.fillText(code, midX, midY);
        }
        ctx.restore();
      }
    }

    // 4. 筛选特定区块：用红色表示轮廓线（核心需求）
    if (isFiltering) {
      ctx.save();
      for (const item of visibleChunks) {
        const { info, sx, sy } = item;
        if (this.isChunkMatched(info)) {
          // 红色轮廓线高亮（朱砂）
          const strokeW = Math.max(2.5, Math.min(4.5, cellSize * 0.1));
          ctx.strokeStyle = CINNABAR_MARK;
          ctx.lineWidth = strokeW;
          ctx.shadowColor = CINNABAR_MARK;
          ctx.shadowBlur = 6;
          ctx.strokeRect(sx + strokeW * 0.5, sy + strokeW * 0.5, cellSize - strokeW, cellSize - strokeW);

          // 内部轻微朱砂半透明叠色，增强辨识度
          ctx.fillStyle = 'rgba(166, 27, 41, 0.28)';
          ctx.fillRect(sx, sy, cellSize, cellSize);
        }
      }
      ctx.restore();
    }

    // 5. 悬停提示
    if (this.hoveredChunk) {
      const hx = halfW + this.panX + (this.hoveredChunk.cx - this.centerCx) * cellSize;
      const hy = halfH + this.panY + (this.hoveredChunk.cz - this.centerCz) * cellSize;
      if (hx + cellSize >= 0 && hx <= width && hy + cellSize >= 0 && hy <= height) {
        ctx.strokeStyle = 'rgba(255, 255, 255, 0.65)';
        ctx.lineWidth = 2;
        ctx.strokeRect(hx + 1, hy + 1, cellSize - 2, cellSize - 2);
      }
    }

    // 6. 选中区块白金轮廓线高亮
    if (this.selectedChunk) {
      const selX = halfW + this.panX + (this.selectedChunk.cx - this.centerCx) * cellSize;
      const selY = halfH + this.panY + (this.selectedChunk.cz - this.centerCz) * cellSize;
      if (selX + cellSize >= 0 && selX <= width && selY + cellSize >= 0 && selY <= height) {
        ctx.save();
        ctx.strokeStyle = '#ffffff';
        ctx.lineWidth = 3;
        ctx.shadowColor = '#ffffff';
        ctx.shadowBlur = 8;
        ctx.strokeRect(selX + 1.5, selY + 1.5, cellSize - 3, cellSize - 3);

        // 四角琥珀色标记
        ctx.strokeStyle = AMBER_MARK;
        ctx.lineWidth = 2;
        const cornerLen = Math.min(8, cellSize * 0.28);
        // 左上
        ctx.beginPath(); ctx.moveTo(selX, selY + cornerLen); ctx.lineTo(selX, selY); ctx.lineTo(selX + cornerLen, selY); ctx.stroke();
        // 右上
        ctx.beginPath(); ctx.moveTo(selX + cellSize - cornerLen, selY); ctx.lineTo(selX + cellSize, selY); ctx.lineTo(selX + cellSize, selY + cornerLen); ctx.stroke();
        // 左下
        ctx.beginPath(); ctx.moveTo(selX, selY + cellSize - cornerLen); ctx.lineTo(selX, selY + cellSize); ctx.lineTo(selX + cornerLen, selY + cellSize); ctx.stroke();
        // 右下
        ctx.beginPath(); ctx.moveTo(selX + cellSize - cornerLen, selY + cellSize); ctx.lineTo(selX + cellSize, selY + cellSize); ctx.lineTo(selX + cellSize, selY + cellSize - cornerLen); ctx.stroke();
        ctx.restore();
      }
    }

    this.paintCivilizationLayer(ctx, width, height, cellSize);
    this.drawRainClouds(ctx, width, height, cellSize);

    // 7. 出生点标记（金色罗盘星）
    const spawn = this.spawn();
    const spX = halfW + this.panX + (spawn.cx - this.centerCx) * cellSize + cellSize * 0.5;
    const spY = halfH + this.panY + (spawn.cz - this.centerCz) * cellSize + cellSize * 0.5;
    if (!this.artificial.playerFactionId && spX >= 0 && spX <= width && spY >= 0 && spY <= height) {
      ctx.save();
      ctx.fillStyle = AMBER_MARK;
      ctx.strokeStyle = '#241c13';
      ctx.lineWidth = 2;
      ctx.beginPath();
      ctx.arc(spX, spY, Math.max(3, cellSize * 0.14), 0, Math.PI * 2);
      ctx.fill();
      ctx.stroke();
      const pin = this.icons.get('flag-pin');
      if (pin?.complete && pin.naturalWidth) ctx.drawImage(pin, spX - 16, spY - 30, 32, 32);
      ctx.restore();
    }



    this.surfaceCache.endFrame();
    if(surfaceMode || pending){
      const text=this.surfaceCache.loading||pending?t('atlas.surface.loading'):t(cellSize>=72?'atlas.surface.detail':'atlas.surface.average');
      ctx.font=`13px ${CANVAS_FONT}`;ctx.fillStyle='rgba(36,28,19,.78)';
      ctx.fillRect(12,height-78,ctx.measureText(text).width+20,24);ctx.fillStyle=PAPER_TEXT;ctx.fillText(text,22,height-61);
    }
    ctx.restore();

    // 回调通知外部状态与统计
    if (this.onStatsUpdate) {
      const climatePercents = climateCounts.map(c =>
        visibleCount > 0 ? (c / visibleCount) * 100 : 0
      ) as [number, number, number, number, number];
      const matchedPercent = visibleCount > 0 ? (matchedCount / visibleCount) * 100 : 0;

      this.onStatsUpdate({
        centerCx: this.centerCx - Math.round(this.panX / cellSize),
        centerCz: this.centerCz - Math.round(this.panY / cellSize),
        scale: this.scale,
        visibleChunks: visibleCount,
        climateCounts,
        climatePercents,
        matchedCount,
        matchedPercent,
        activeFilterName: this.getActiveFilterLabel(),
        level: 'chunks',
      });
    }
  }

  private drawClimateBackdrop(ctx:CanvasRenderingContext2D,width:number,height:number,cell:number):void{
    const size=this.generator.generation.planet.equatorChunks;
    const left=this.centerCx-(width/2+this.panX)/cell,top=this.worldZ(this.mapZ(this.centerCz)-(height/2+this.panY)/cell);
    const step=Math.max(1,Math.ceil(Math.sqrt(width*height/384)/cell));
    const startX=Math.floor(left/step)*step,startZ=Math.max(-size/4,Math.floor(top/step)*step);
    const endX=left+width/cell,endZ=Math.min(size/4,this.worldZ(this.mapZ(this.centerCz)+(height/2-this.panY)/cell));
    for(let cz=startZ;cz<endZ;cz+=step)for(let cx=startX;cx<endX;cx+=step){
      const climate=this.generator.getClimate(cx+step/2,Math.min(size/4-.5,cz+step/2));
      ctx.fillStyle=CLIMATES[climate.index].color;
      ctx.fillRect(width/2+this.panX+(cx-this.centerCx)*cell,height/2+this.panY+(this.mapZ(cz)-this.mapZ(this.centerCz))*cell,
        step*cell+.25,(this.mapZ(Math.min(cz+step,size/4))-this.mapZ(cz))*cell+.25);
    }
  }

  private drawFlatDetails(ctx:CanvasRenderingContext2D,width:number,height:number,coordinates:Iterable<[number,number]>):void{
    const cell=this.baseCellSize*this.scale,size=this.generator.generation.planet.equatorChunks;
    const counts:[number,number,number,number,number]=[0,0,0,0,0];let visible=0,matched=0;
    const riverTiles:{summary:MapSummary;x:number;y:number;w:number;h:number}[]=[];
    const rivers=!this.showClimate&&cell>=Math.max(12,Math.sqrt(width*height/1024));
    for(const [cx,cz] of coordinates){
      if(cz<-size/4||cz>=size/4||this.isMercator&&Math.abs((cz+.5)/size*Math.PI*2)>MERCATOR_LIMIT)continue;
      const wrapped=cx+Math.round((this.centerCx-this.panX/cell-cx)/size)*size;
      const x=width/2+this.panX+(wrapped-this.centerCx)*cell,y=height/2+this.panY+(this.mapZ(cz)-this.mapZ(this.centerCz))*cell;
      const tileHeight=(this.mapZ(cz+1)-this.mapZ(cz))*cell;
      if(x+cell<0||y+tileHeight<0||x>width||y>height)continue;
      const image=!this.showSatellite&&this.refinementReady&&cell>=72&&!this.showClimate&&!this.showChunkColors;
      const detail=image||this.showChunkInfo&&this.refinementReady||rivers;
      const tile=this.surfaceCache.get(cx,cz,(x-width/2)**2+(y-height/2)**2,image,false,detail,rivers&&(!this.refinementReady||cell<48)&&!image&&!(this.showChunkInfo&&this.refinementReady));
      const info=tile?.summary?.info??this.generator.cachedChunkInfo(cx,cz);
      ctx.fillStyle=tile?.average??'rgba(145,121,83,.13)';ctx.fillRect(x,y,cell+.25,tileHeight+.25);
      if(tile?.image&&cell>=24&&!this.showClimate&&!this.showChunkColors)ctx.drawImage(tile.image,x,y,cell+.25,tileHeight+.25);
      if(info&&(this.showClimate||this.showChunkColors)){ctx.fillStyle=`rgb(${overviewColor(info,this.showClimate,false,this.showChunkColors,true).join(',')})`;ctx.fillRect(x,y,cell+.25,tileHeight+.25);}
      if(this.showSatellite&&info&&!this.showClimate&&!this.showChunkColors){const rgb=this.generator.getSatelliteColor(cx,cz,false);if(rgb!==null){ctx.fillStyle='#'+rgb.toString(16).padStart(6,'0');ctx.fillRect(x,y,cell+.25,tileHeight+.25);}}
      if(this.layerEnabled('contour')&&!this.showSatellite&&info&&!(tile?.image&&!this.showClimate&&!this.showChunkColors&&cell>=24)){
        this.drawContourChunk(ctx,info,x,y,cell+.25,tileHeight+.25);
      }
      if(rivers&&!(tile?.image&&!this.showChunkColors)&&tile?.summary)riverTiles.push({summary:tile.summary,x,y,w:cell+.25,h:tileHeight+.25});
      if(!info)continue;
      counts[info.climate]++;visible++;if(this.isChunkMatched(info)){matched++;if(this.filterTypeId!==null||this.filterCategory!==null){ctx.strokeStyle=CINNABAR_MARK;ctx.strokeRect(x,y,cell,tileHeight);}}
      if(this.showChunkInfo&&cell>=36){ctx.fillStyle=PAPER_TEXT;ctx.font=`10px ${CANVAS_FONT}`;ctx.fillText(String(info.type),x+3,y+12);}
    }
    if(rivers){
      ctx.save();ctx.beginPath();for(const r of riverTiles)ctx.rect(r.x,r.y,r.w,r.h);ctx.clip();
      const minX=Math.floor(this.centerCx+(-width/2-this.panX)/cell),maxX=Math.ceil(this.centerCx+(width/2-this.panX)/cell);
      const minZ=Math.floor(this.worldZ(this.mapZ(this.centerCz)+(-height/2-this.panY)/cell)),maxZ=Math.ceil(this.worldZ(this.mapZ(this.centerCz)+(height/2-this.panY)/cell));
      this.drawRiverLayer(ctx,width,height,cell,minX,maxX,minZ,maxZ);
      for(const r of riverTiles)this.drawFlowArrow(ctx,r.summary.info,r.x,r.y,cell,r.h);
      ctx.restore();
    }
    this.surfaceCache.endFrame();this.paintCivilizationLayer(ctx,width,height,cell);this.drawRainClouds(ctx,width,height,cell);
    ctx.fillStyle='#695638';ctx.font=`13px ${CANVAS_FONT}`;ctx.fillText(t(this.isMercator?'atlas.canvas.mercatorHint':'atlas.canvas.projectionHint'),20,28);
    const at=this.getCenterCoordinates();
    this.onStatsUpdate?.({centerCx:at.cx,centerCz:at.cz,scale:this.scale,visibleChunks:visible,climateCounts:counts,
      climatePercents:counts.map(n=>visible?n/visible*100:0) as [number,number,number,number,number],matchedCount:matched,
      matchedPercent:visible?matched/visible*100:0,activeFilterName:this.getActiveFilterLabel(),level:cell<12?'overview':'chunks'});
  }
  /** 高度图按高程上色，叠等高线阴影 */
  private drawContourChunk(ctx:CanvasRenderingContext2D,info:ChunkInfo,x:number,y:number,w:number,h:number):void{
    const unit=this.textureContourUnit();
    if(this.layerEnabled('height')){
      const o=elevationRampValue(info.elevation)*3;
      ctx.fillStyle=`rgb(${LAND[o]},${LAND[o+1]},${LAND[o+2]})`;
      ctx.fillRect(x,y,w,h);
      return;
    }
    if(!(unit>0)||info.elevation<=0)return;
    const band=bandPosition(info.elevation,unit),shade=terraceShade(band);
    ctx.fillStyle=`rgba(0,0,0,${Math.min(.3,(1-shade)*1.6)})`;
    ctx.fillRect(x,y,w,h);
    const cell=Math.floor(info.elevation/unit);
    const east=this.cachedMapInfo(info.cx+1,info.cz),south=this.cachedMapInfo(info.cx,info.cz+1);
    ctx.strokeStyle='rgba(38,30,20,.45)';ctx.lineWidth=1;
    if(east&&Math.floor(east.elevation/unit)!==cell){ctx.beginPath();ctx.moveTo(x+w,y);ctx.lineTo(x+w,y+h);ctx.stroke();}
    if(south&&Math.floor(south.elevation/unit)!==cell){ctx.beginPath();ctx.moveTo(x,y+h);ctx.lineTo(x+w,y+h);ctx.stroke();}
  }
  private drawFlowArrow(ctx:CanvasRenderingContext2D,info:ChunkInfo,x:number,y:number,cell:number,h:number):void{
    if(cell<10||!isRiverType(info.type,this.generator.generation.terrainRule==='planet-macro')||info.displayFlow<0)return;
    const flow=FLOW_DIRECTIONS[info.displayFlow];if(!flow)return;
    ctx.save();ctx.fillStyle='#ffffff';ctx.shadowColor='#000000';ctx.shadowBlur=3;
    ctx.font=`bold ${Math.max(10,Math.round(cell*.36))}px ${CANVAS_FONT}`;
    ctx.textAlign='center';ctx.textBaseline='middle';
    const name=`flow-${flow.dz<0?'n':flow.dz>0?'s':''}${flow.dx<0?'w':flow.dx>0?'e':''}`;
    const image=this.icons.get(name),size=Math.max(12,Math.min(48,cell*.6));
    if(image?.complete&&image.naturalWidth)ctx.drawImage(image,x+(cell-size)/2,y+(h-size)/2,size,size);
    else ctx.fillText(flow.arrow,x+cell*.5,y+h*.5);
    ctx.restore();
  }

  private drawRiverSummary(ctx:CanvasRenderingContext2D,summary:MapSummary,width:number,height:number,cell:number):void{
    const size=this.isPlanet?this.generator.generation.planet.equatorChunks:1;
    const offset=this.isPlanet?Math.round((this.centerCx-this.panX/cell-summary.info.cx)/size)*size:0;
    drawMapRiver(ctx,summary,cell,(x,z)=>[width/2+this.panX+(x+offset-this.centerCx)*cell,
      height/2+this.panY+(this.mapZ(z)-this.mapZ(this.centerCz))*cell],(x,z)=>this.cachedMapInfo(x,z),this.isPlanet?size:0,(x,z)=>this.surfaceCache.peek(x,z)?.summary);
  }

  private drawRiverLayer(ctx:CanvasRenderingContext2D,width:number,height:number,cell:number,minX:number,maxX:number,minZ:number,maxZ:number):void{
    const detailed=cell>=48&&this.refinementReady,padding=Math.ceil((MAX_RIVER_RADIUS+CHUNK_SIZE+14+MAX_BRAID_OFFSET+MAX_MOUTH_LENGTH+MAX_RIVER_BEND)/CHUNK_SIZE);
    const cells=this.centerCells(minX-padding,maxX+padding,minZ-padding,maxZ+padding,this.centerCx-this.panX/cell,this.centerCz-this.panY/cell);
    const ready=(x:number,z:number)=>{const tile=this.surfaceCache.peek(x,z);return !!tile?.summary&&tile.refined!==false&&tile.summary.refined!==false;};
    const coreReady=this.centerCells(minX-1,maxX+1,minZ-1,maxZ+1,0,0).every(([x,z])=>
      this.isPlanet&&(z<-this.generator.generation.planet.equatorChunks/4||z>=this.generator.generation.planet.equatorChunks/4)||ready(x,z));
    let prefetched=0;
    const drawn=new Set<string>();
    for(const [cx,cz] of cells){
      if(this.isPlanet&&(cz<-this.generator.generation.planet.equatorChunks/4||cz>=this.generator.generation.planet.equatorChunks/4))continue;
      const x=width/2+this.panX+(cx-this.centerCx)*cell,y=height/2+this.panY+(this.mapZ(cz)-this.mapZ(this.centerCz))*cell;
      const known=this.cachedMapInfo(cx,cz);
      const near=cx>=minX-1&&cx<=maxX+1&&cz>=minZ-1&&cz<=maxZ+1;
      if(!near&&ready(cx,cz)&&!known?.downstream)continue;
      if(!near&&cell<48&&!ready(cx,cz)){
        if(!coreReady||prefetched>=12)continue;
        prefetched++;
      }
      const next=known?.downstream?this.cachedMapInfo(known.downstream.cx,known.downstream.cz):null;
      const radius=known?.downstream?Math.min(MAX_RIVER_RADIUS,Math.max(known.riverWidth,next?.riverWidth??MAX_RIVER_RADIUS*2)/2):MAX_RIVER_RADIUS;
      const reach=(radius+MAX_MOUTH_LENGTH+MAX_RIVER_BEND+CHUNK_SIZE)/CHUNK_SIZE,margin=reach*cell;
      const top=height/2+this.panY+(this.mapZ(cz-reach)-this.mapZ(this.centerCz))*cell;
      const bottom=height/2+this.panY+(this.mapZ(cz+1+reach)-this.mapZ(this.centerCz))*cell;
      const intersects=x+cell+margin>=0&&x-margin<=width&&bottom>=0&&top<=height;
      const full=detailed&&!!known?.downstream&&intersects;
      const tile=this.surfaceCache.get(cx,cz,(x+cell/2-width/2)**2+(y+cell/2-height/2)**2,false,false,true,!full);
      if(!tile?.summary)continue;
      const summary=tile.summary,key=summary.cx+','+summary.cz;
      if(drawn.has(key))continue;drawn.add(key);
      this.drawRiverSummary(ctx,summary,width,height,cell);
    }
  }

  private drawFlatPlanet(ctx:CanvasRenderingContext2D,width:number,height:number):void {
    const cell=this.baseCellSize*this.scale,size=this.generator.generation.planet.equatorChunks;
    this.planetLayer.drawFlat(ctx,width,height,cell,this.centerCx,this.centerCz,this.panX,this.panY,this.overviewStyle(),this.isMercator);
    if(this.scale>=.25){
      const left=Math.floor(this.centerCx+(-width/2-this.panX)/cell),right=Math.ceil(left+width/cell)+1;
      const top=Math.floor(this.worldZ(this.mapZ(this.centerCz)+(-height/2-this.panY)/cell)),bottom=Math.ceil(this.worldZ(this.mapZ(this.centerCz)+(height/2-this.panY)/cell));
      const coordinates=function*():Generator<[number,number]>{for(let z=top;z<=bottom;z++)for(let x=left;x<=right;x++)yield [((x+size/2)%size+size)%size-size/2,z];};
      this.drawFlatDetails(ctx,width,height,coordinates());
    }else{
      this.paintCivilizationLayer(ctx,width,height,cell);this.drawRainClouds(ctx,width,height,cell);
      ctx.fillStyle=PAPER_TEXT;ctx.font=`13px ${CANVAS_FONT}`;ctx.fillText(t(this.isMercator?'atlas.canvas.mercatorHint':'atlas.canvas.projectionHint'),20,28);this.drawMacroRiverHint(ctx);
      const at=this.getCenterCoordinates();this.onStatsUpdate?.({centerCx:at.cx,centerCz:at.cz,scale:this.scale,visibleChunks:0,climateCounts:[0,0,0,0,0],climatePercents:[0,0,0,0,0],matchedCount:0,matchedPercent:0,activeFilterName:this.getActiveFilterLabel(),level:'overview'});
    }
  }

  private drawMacroRiverHint(ctx:CanvasRenderingContext2D):void {
    if(this.generator.generation.terrainRule==='planet-macro'&&!this.survivalPreview&&!this.showClimate)ctx.fillText(t('atlas.canvas.macroRiversHint'),20,48);
  }

  /** 全览样式，纹理与底图共用同一标识 */
  private overviewStyle(): OverviewStyle {
    return { satellite:this.showSatellite,rivers:!this.showClimate,climate: this.showClimate, relief: this.layerEnabled('height'),
      reliefMode:this.textureMode(),sunHours:this.sunHours,sunWorld:this.sunWorld(), contourUnit:this.textureContourUnit(),
      climateFilter: this.activeClimateFilter,
      chunkColors: this.showChunkColors, filterKey: this.getActiveFilterLabel(),
      matches: (type: number) => this.isTypeMatched(type) };
  }

  /** 全览纹理只有一层，按开关挑一个近似档 */
  private textureMode():'contour'|'daylight'|'height'|'none'{
    if(this.layerEnabled('contour'))return 'contour';
    if(this.layerEnabled('daylight'))return 'daylight';
    if(this.layerEnabled('height'))return 'height';
    return 'none';
  }
  /** 全览与瓦片按同一缩放档查表 */
  private textureContourUnit():number{
    if(!this.layerEnabled('contour'))return 0;
    const dpr=Math.min(window.devicePixelRatio||1,2);
    const state=atlasCamera(this,this.canvas.width/dpr,this.canvas.height/dpr);
    return contourUnit(levelFor(state),this.generator.generation.macroColumns);
  }

  /** 先补中心邻域，再向四周展开 */
  private centerCells(minX:number,maxX:number,minZ:number,maxZ:number,cx:number,cz:number):[number,number][] {
    const cells:[number,number][]=[];
    for(let z=minZ;z<=maxZ;z++)for(let x=minX;x<=maxX;x++)cells.push([x,z]);
    return cells.sort((a,b)=>(a[0]-cx)**2+(a[1]-cz)**2-(b[0]-cx)**2-(b[1]-cz)**2);
  }

  private drawPlanetOverview(ctx: CanvasRenderingContext2D, width: number, height: number): void {
    const size = this.generator.generation.planet.equatorChunks, cell = this.baseCellSize * this.scale;
    const counts: [number, number, number, number, number] = [0, 0, 0, 0, 0];
    let samples = 0, matches = 0;
    const style = this.overviewStyle();
    if (this.isGlobe) {
      const coord = planetCoordinates(this.centerCx, this.centerCz, size);
      this.planetLayer.drawGlobe(ctx, width, height, this.globeRadius, coord.longitude, coord.latitude, style);
      // 对可见半球做稀疏统计；走已烘焙样本，免去逐点重算
      for (let y = 12; y < height; y += 24) for (let x = 12; x < width; x += 24) {
        const at = globePoint((x - width / 2) / this.globeRadius, (height / 2 - y) / this.globeRadius, coord.longitude, coord.latitude);
        const sample = at && this.planetLayer.sampleAt(at.longitude, at.latitude);
        if (!sample) continue;
        counts[sample.climate]++; samples++;
        if (this.isTypeMatched(sample.type)) matches++;
      }
    } else {
      const texture = cell < .7;
      if (texture) this.planetLayer.drawProjection(ctx, width, height, cell, this.centerCx, this.centerCz, this.panX, this.panY, style);
      // 瓦片过密时放大步长，把每帧采样压在上限内
      const budget = texture ? OVERVIEW_STAT_BUDGET : OVERVIEW_TILE_BUDGET;
      const dense = Math.ceil(Math.sqrt(Math.ceil(width / cell) * Math.ceil(height / cell) / budget));
      const step = Math.max(1, Math.ceil((texture ? 24 : 3) / cell), dense), tile = step * cell;
      const minX = this.centerCx + Math.floor((-width / 2 - this.panX) / cell / step) * step;
      const minZ = this.centerCz + Math.floor((-height / 2 - this.panY) / cell / step) * step;
      const maxX = this.centerCx + (width / 2 - this.panX) / cell;
      const maxZ = this.worldZ(this.mapZ(this.centerCz)+(height/2-this.panY)/cell);
      for (let z = minZ; z <= maxZ; z += step) for (let x = minX; x <= maxX; x += step) {
        if (z < -size / 4 || z >= size / 4) continue;
        const info = this.generator.getOverviewInfo(x + (step - 1) / 2, z + (step - 1) / 2);
        const sx = width / 2 + this.panX + (x - this.centerCx) * cell;
        const sy = height / 2 + this.panY + (z - this.centerCz) * cell;
        // 纹理档已有底图，这里只补统计
        if (!texture) {
          let color = overviewColor(info, this.showClimate, this.layerEnabled('height'),this.showChunkColors,this.generator.generation.terrainRule==='planet-macro');
          if(this.showSatellite&&!this.showClimate&&!this.showChunkColors){const rgb=this.generator.getSatelliteColor(info.cx,info.cz,false);if(rgb!==null)color=[rgb>>16,rgb>>8&255,rgb&255];}
          if (this.showClimate && this.activeClimateFilter !== null && info.climate !== this.activeClimateFilter) color = color.map(c => c * .35) as [number, number, number];
          ctx.fillStyle = `rgb(${color.join(',')})`; ctx.fillRect(sx, sy, tile + .5, tile + .5);
        }
        counts[info.climate]++; samples++;
        if (this.isChunkMatched(info)) {
          matches++;
          if (!texture) { ctx.strokeStyle = CINNABAR_MARK; ctx.lineWidth = 1; ctx.strokeRect(sx, sy, tile, tile); }
        }
      }
      ctx.strokeStyle = 'rgba(239,248,241,.2)'; ctx.lineWidth = 1;
      // 等距圆柱投影：全球图经纬线，赤道突出。
      for (let z = -size / 4; z <= size / 4; z += size / 12) {
        const sy = height / 2 + this.panY + (z - this.centerCz) * cell;
        ctx.beginPath(); ctx.moveTo(0, sy); ctx.lineTo(width, sy); ctx.stroke();
      }
      const left = this.centerCx + (-width / 2 - this.panX) / cell;
      for (let x = Math.floor(left / (size / 12)) * size / 12; x <= maxX; x += size / 12) {
        const sx = width / 2 + this.panX + (x - this.centerCx) * cell;
        ctx.beginPath(); ctx.moveTo(sx, Math.max(0, height / 2 + this.panY + (-size / 4 - this.centerCz) * cell));
        ctx.lineTo(sx, Math.min(height, height / 2 + this.panY + (size / 4 - this.centerCz) * cell)); ctx.stroke();
      }
    }
    this.paintCivilizationLayer(ctx, width, height, cell);
    this.drawRainClouds(ctx, width, height, cell);
    ctx.fillStyle = PAPER_TEXT; ctx.font = `13px ${CANVAS_FONT}`; ctx.textAlign = 'left';
    ctx.fillText(this.isGlobe?t('atlas.canvas.globeHint'):t('atlas.canvas.projectionHint'),20,28);this.drawMacroRiverHint(ctx);

    const center = this.getCenterCoordinates();
    this.onStatsUpdate?.({ centerCx: Math.round(center.cx), centerCz: Math.round(center.cz), scale: this.scale,
      visibleChunks: samples, climateCounts: counts,
      climatePercents: counts.map(n => samples ? n / samples * 100 : 0) as [number, number, number, number, number],
      matchedCount: matches, matchedPercent: samples ? matches / samples * 100 : 0,
      activeFilterName: this.getActiveFilterLabel(), level: this.isGlobe ? 'globe' : 'overview' });
  }

  /** 玩家位置与朝向箭头，与 F12 小地图同形 */
  private drawPlayerMarker(ctx: CanvasRenderingContext2D, width: number, height: number, cell: number): void {
    const marker = this.playerMarker?.();
    if (!marker) return;
    if (this.isGlobe) { this.drawGlobePlayerMarker(ctx, width, height, marker); return; }
    const size = this.generator.generation.planet.equatorChunks;
    let cx = marker.x / CHUNK_SIZE;
    // 星球经度循环：取离视口中心最近的一圈
    if (this.isPlanet) cx += Math.round((this.centerCx-this.panX/cell-cx)/size)*size;
    const cz = marker.z / CHUNK_SIZE;
    this.strokePlayerArrow(ctx, width / 2 + this.panX + (cx - this.centerCx) * cell,
      height / 2 + this.panY + (this.mapZ(cz) - this.mapZ(this.centerCz)) * cell,
      Math.atan2(-Math.cos(marker.yaw), -Math.sin(marker.yaw)));
  }

  /** 球体正射：位置走经纬，朝向取球面切线 */
  private drawGlobePlayerMarker(ctx: CanvasRenderingContext2D, width: number, height: number, marker: AtlasPlayerMarker): void {
    const size = this.generator.generation.planet.equatorChunks;
    const view = planetCoordinates(this.centerCx, this.centerCz, size);
    const at = planetCoordinates(marker.x / CHUNK_SIZE - .5, marker.z / CHUNK_SIZE - .5, size);
    const rad = Math.PI / 180, dl = (at.longitude - view.longitude) * rad;
    const phi = at.latitude * rad, origin = view.latitude * rad;
    const depth = Math.sin(origin) * Math.sin(phi) + Math.cos(origin) * Math.cos(phi) * Math.cos(dl);
    if (depth < 0) return;
    const cosPhi = Math.cos(phi), sinPhi = Math.sin(phi), sinDl = Math.sin(dl), cosDl = Math.cos(dl);
    const cosOrigin = Math.cos(origin), sinOrigin = Math.sin(origin);
    const radius = this.globeRadius, k = Math.PI * 2 / size;
    const fx = -Math.sin(marker.yaw) / CHUNK_SIZE, fz = -Math.cos(marker.yaw) / CHUNK_SIZE;
    const dx = k * radius * (cosPhi * cosDl * fx + sinPhi * sinDl * fz);
    const dy = k * radius * (-sinOrigin * cosPhi * sinDl * fx + (cosOrigin * cosPhi + sinOrigin * sinPhi * cosDl) * fz);
    this.strokePlayerArrow(ctx, width / 2 + radius * cosPhi * sinDl,
      height / 2 - radius * (cosOrigin * sinPhi - sinOrigin * cosPhi * cosDl), Math.atan2(dy, dx));
  }

  private strokePlayerArrow(ctx: CanvasRenderingContext2D, x: number, y: number, angle: number): void {
    ctx.save();
    ctx.translate(x, y); ctx.rotate(angle);
    ctx.beginPath();
    ctx.moveTo(11, 0); ctx.lineTo(-7, -7); ctx.lineTo(-3, 0); ctx.lineTo(-7, 7);
    ctx.closePath();
    ctx.fillStyle = CINNABAR_MARK;
    ctx.strokeStyle = '#ffffff';
    ctx.lineWidth = 2;
    ctx.shadowColor = 'rgba(0, 0, 0, .55)';
    ctx.shadowBlur = 3;
    ctx.fill();
    ctx.stroke();
    ctx.restore();
  }

  // 颜色淡化辅助
  private dimHexColor(hex: string, factor: number): string {
    const c = hex.replace('#', '');
    const num = parseInt(c, 16);
    const r = Math.round(((num >> 16) & 255) * factor + 36 * (1 - factor));
    const g = Math.round(((num >> 8) & 255) * factor + 28 * (1 - factor));
    const b = Math.round((num & 255) * factor + 19 * (1 - factor));
    return `rgb(${r}, ${g}, ${b})`;
  }

  // ---- 鼠标与触控事件监听 ----

  // 接入独立输入控制器
  private initEvents(): void {
    const host: AtlasInputHost = {
      active: this.active,
      getChunkAtPoint: (x, y, requestInfo) => this.getChunkAtPoint(x, y, requestInfo),
      onHover: (chunk) => {
        if (!this.hoveredChunk || this.hoveredChunk.cx !== chunk?.cx || this.hoveredChunk.cz !== chunk?.cz) {
          this.hoveredChunk = chunk;
          if (!this.isPlanet || (!this.isGlobe && this.baseCellSize * this.scale >= 12)) this.requestRender();
        }
      },
      onSelect: (chunk) => {
        this.selectedChunk = chunk;
        if (this.onChunkSelect) this.onChunkSelect(this.artificial.info(chunk.cx, chunk.cz));
      },
      onActivate: (chunk) => {
        const info = this.artificial.info(chunk.cx, chunk.cz);
        if (this.onChunkActivate) {
          this.onChunkActivate(info);
          return;
        }
        if (!this.isPlanet) return;
        this.centerCx = chunk.cx;
        this.centerCz = chunk.cz;
        this.panX = 0;
        this.panY = 0;
        this.planetView = this.preferredOverview === 'mercator' ? 'mercator' : 'projection';
        this.scale = 1;
        this.selectedChunk = chunk;
        this.onChunkSelect?.(info);
        this.requestRender();
      },
      onDestination: (x, y, chunk) => {
        this.destination(x, y, chunk);
      },
      panBy: (dx, dy) => {
        this.panBy(dx, dy);
      },
      zoomBy: (factor, clientX, clientY) => {
        this.zoomBy(factor, clientX, clientY);
      },
      requestRender: () => {
        this.requestRender();
      },
      setCursor: (cursor) => {
        this.canvas.style.cursor = this.survivalPreview?'default':cursor;
      },
    };
    Object.defineProperty(host, 'active', { get: () => this.active });
    this.input = new AtlasInputController(this.canvas, host);
  }

  private paintCivilizationLayer(ctx: CanvasRenderingContext2D, width: number, height: number, cell: number): void {
    if (!this.layerEnabled('civilization')) return;
    ctx.save(); ctx.globalAlpha = this.layerOpacity('civilization');
    this.drawTerritoryLayer(ctx, width, height, cell);
    ctx.restore();
  }

  /** 文明层：置旗地与领地范围 */
  drawTerritoryLayer(ctx: CanvasRenderingContext2D, width: number, height: number, cell: number): void {
    if (!this.artificial.settings.enabled && !this.artificial.playerFactionId) return;
    const size = this.generator.generation.planet.equatorChunks;
    const s = this.artificial.generator.cellSize;
    const bounds = this.isGlobe ? [-size / 2, -size / 4, size / 2 - 1, size / 4 - 1]
      : [this.centerCx + (-width / 2 - this.panX) / cell, this.worldZ(this.mapZ(this.centerCz)+(-height/2-this.panY)/cell),
        this.centerCx + (width / 2 - this.panX) / cell, this.worldZ(this.mapZ(this.centerCz)+(height/2-this.panY)/cell)];
    const key = bounds.map(n => Math.floor(n / s)).join(',');
    if (!this.survivalPreview&&key !== this.territoryDiscoveryKey) {
      this.territoryDiscoveryKey = key;
      this.territoryDiscovery = this.artificial.discoverRegion(bounds[0], bounds[1], bounds[2], bounds[3]);
    }
    if (this.territoryDiscovery) {
      if (this.territoryDiscovery.next().done) this.territoryDiscovery = null;
      else this.requestRender();
    }
    const center = planetCoordinates(this.centerCx, this.centerCz, size);
    const rad = Math.PI / 180, origin = center.latitude * rad;
    drawTerritories(ctx, this.artificial, (x, z) => {
      if (this.isGlobe) {
        const phi = -z / size * Math.PI * 2, dl = x / size * Math.PI * 2 - center.longitude * rad;
        const depth = Math.sin(origin) * Math.sin(phi) + Math.cos(origin) * Math.cos(phi) * Math.cos(dl);
        if (depth < 0) return null;
        return [width / 2 + this.globeRadius * Math.cos(phi) * Math.sin(dl),
          height / 2 - this.globeRadius * (Math.cos(origin) * Math.sin(phi) - Math.sin(origin) * Math.cos(phi) * Math.cos(dl))];
      }
      const dx = this.isPlanet ? this.artificial.generator.wrap(x-this.centerCx+this.panX/cell) : x-this.centerCx;
      return [width/2+(this.isPlanet?0:this.panX)+dx*cell, height / 2 + this.panY + (this.mapZ(z) - this.mapZ(this.centerCz)) * cell];
    }, width, height, cell >= 20 && !this.showChunkInfo,this.survivalPreview);
  }

  /** 降水场直接反算画布坐标，不触碰区块采样 */
  private drawRainClouds(ctx: CanvasRenderingContext2D, width: number, height: number, cell: number, showLegend=true): void {
    const weather = this.precipitation;
    if (!this.showRainClouds || !weather) return;
    ctx.save();ctx.globalAlpha=this.rainOpacity;
    const size = this.generator.generation.planet.equatorChunks, TAU = Math.PI * 2;
    const center = this.isGlobe ? { lon: (this.centerCx + .5) / size * TAU, lat: -(this.centerCz + .5) / size * TAU }
      : { lon: (this.centerCx - this.panX / cell) / size * TAU, lat: -this.worldZ(this.mapZ(this.centerCz) - this.panY / cell) / size * TAU };
    const state = { width, height, view: (this.isGlobe ? 'globe' : this.isMercator ? 'mercator' : 'projection') as 'globe' | 'mercator' | 'projection', ...center,
      scale: this.isGlobe ? this.globeRadius : cell * size / TAU };
    const key = `${JSON.stringify(state)}:${weather.version}:${weather.day()}:${weather.frameStamp()}`;
    if (this.cloudLayer?.key !== key) {
      const layer = this.cloudLayer ?? (this.cloudLayer = { canvas: document.createElement('canvas'), key: '' });
      const factor = Math.min(1, Math.sqrt(24000 / (width * height)));
      const w = Math.max(1, Math.ceil(width * factor)), h = Math.max(1, Math.ceil(height * factor));
      if (layer.canvas.width !== w) layer.canvas.width = w;
      if (layer.canvas.height !== h) layer.canvas.height = h;
      const pixels = new Uint8ClampedArray(w * h * 4);
      for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
        const px = (x + .5) * width / w, py = (y + .5) * height / h, point = unproject(state, px, py);
        if (!point) continue;
        let footprint = (this.generator.generation.planet.equatorChunks * CHUNK_SIZE) / state.scale * Math.max(width / w, height / h);
        if (this.isGlobe) { const r2 = ((px - width / 2) ** 2 + (py - height / 2) ** 2) / state.scale ** 2; footprint /= Math.max(.2, Math.sqrt(Math.max(0, 1 - r2))); }
        else if (this.isMercator) footprint *= Math.max(.04, Math.cos(point.lat));
        const sample = weather.sample(point.lon, point.lat, footprint), i = (y * w + x) * 4;
        const rain = sample.precipitation !== 'none';
        const color = rain ? (sample.precipitation === 'snow' ? [168, 155, 207] : sample.precipitation === 'mixed' ? [176, 216, 232] : [88, 155, 218]) : [222, 228, 232];
        pixels[i] = color[0]; pixels[i + 1] = color[1]; pixels[i + 2] = color[2];
        pixels[i + 3] = 255 * (rain ? .12 + sample.intensity * .88 : Math.max(0, sample.cloudCover - .42) * .24);
      }
      layer.canvas.getContext('2d')!.putImageData(new ImageData(pixels, w, h), 0, 0);
      layer.key = key;
    }
    ctx.save();
    if (this.isGlobe) { ctx.beginPath(); ctx.arc(width / 2, height / 2, state.scale, 0, TAU); ctx.clip(); }
    ctx.imageSmoothingEnabled = true; ctx.drawImage(this.cloudLayer!.canvas, 0, 0, width, height);
    ctx.restore();
    if(!showLegend){ctx.restore();return;}
    ctx.save(); ctx.textAlign = 'left'; ctx.font = `13px ${CANVAS_FONT}`;
    const legend = t('atlas.rainClouds.legend');
    const legendWidth = ctx.measureText(legend).width + 20, legendX = Math.max(12, width - legendWidth - 12);
    const tools = this.canvas.closest('.game-map')?.querySelector('.game-map-tools');
    const legendY = tools ? Math.max(12, tools.getBoundingClientRect().bottom - this.canvas.getBoundingClientRect().top + 12) : 12;
    ctx.fillStyle = 'rgba(36,28,19,.85)'; ctx.fillRect(legendX, legendY, legendWidth, 26);
    ctx.fillStyle = PAPER_TEXT; ctx.fillText(legend, legendX + 10, legendY + 18); ctx.restore();
    ctx.restore();
  }

  private destination(px:number,py:number,chunk:{cx:number;cz:number}):void {
    if(!this.onDestination)return;
    const dpr=Math.min(window.devicePixelRatio||1,2),cell=this.baseCellSize*this.scale;
    this.onDestination(this.isGlobe?{x:(chunk.cx+.5)*CHUNK_SIZE,z:(chunk.cz+.5)*CHUNK_SIZE}:
      {x:(this.centerCx+(px-this.canvas.width/dpr/2-this.panX)/cell)*CHUNK_SIZE,
        z:this.worldZ(this.mapZ(this.centerCz)+(py-this.canvas.height/dpr/2-this.panY)/cell)*CHUNK_SIZE});
  }
  private cachedMapInfo(cx:number,cz:number):ChunkInfo|null|undefined{
    if(this.isPlanet){const size=this.generator.generation.planet.equatorChunks;cx=((cx+size/2)%size+size)%size-size/2;}
    return this.surfaceCache.peek(cx,cz)?.summary?.info??this.generator.cachedChunkInfo(cx,cz);
  }
  private getChunkAtPoint(px:number,py:number,requestInfo=true):{cx:number;cz:number}|null {
    if(this.survivalPreview&&this.isPlanet)return null;
    const dpr=Math.min(window.devicePixelRatio||1,2),width=this.canvas.width/dpr,height=this.canvas.height/dpr;
    const cell=this.baseCellSize*this.scale,size=this.generator.generation.planet.equatorChunks;
    let cx:number,cz:number;
    if(this.isPlanet){
      const point=unproject(atlasCamera(this,width,height),px,py);if(!point)return null;
      cx=Math.floor(point.lon/(Math.PI*2)*size+1e-9);
      cx=((cx+size/2)%size+size)%size-size/2;
      cz=Math.max(-size/4,Math.min(size/4-1,Math.floor(-point.lat/(Math.PI*2)*size+1e-9)));
    }else{
      cx=Math.floor(this.centerCx+(px-width/2-this.panX)/cell);
      cz=Math.floor(this.centerCz+(py-height/2-this.panY)/cell);
    }
    if(this.exploration&&!this.exploration.has(cx,cz))return null;
    if(this.worldKey&&!this.cachedMapInfo(cx,cz)){
      if(requestInfo){this.surfaceCache.get(cx,cz,0,false);this.surfaceCache.endFrame();}return null;
    }
    return {cx,cz};
  }
  destroy(): void {
    this.destroyed = true;
    this.input?.dispose();
    disposeGameAtlas(this);
    clearTimeout(this.refinementTimer);
    this.surfaceCache.dispose();
    this.events.abort();
    if (this.resizeObserver) {
      this.resizeObserver.disconnect();
      this.resizeObserver = null;
    }
  }
}
