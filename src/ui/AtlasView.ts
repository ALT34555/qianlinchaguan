import { iconUrl } from './Icons';
import { WorldGenerator, FLOW_DIRECTIONS, type ChunkInfo } from '../systems/world/WorldGenerator';
import { snowLine } from '../systems/world/TerrainLayers';
import { getChunkTypeDef, formatChunkId, isRiverType } from '../systems/world/ChunkTypes';
import { CLIMATES, planetCoordinates } from '../systems/world/WorldSettings';
import { PlanetAtlasLayer, globePoint, overviewColor, type PlanetView } from './PlanetAtlasLayer';
import { chunkName } from '../i18n/content';
import { t } from '../i18n';
import { CHUNK_SIZE } from '../core/config';
import { MAX_BRAID_OFFSET, MAX_MOUTH_LENGTH, MAX_RIVER_RADIUS } from '../systems/world/RiverChannels';
import {MAX_RIVER_BEND} from '../systems/world/RiverGeometry';
import {AtlasSurfaceCache} from './AtlasSurfaceCache';
import { ArtificialWorld, type TerritoryChunkInfo } from '../systems/world/artificial/ArtificialWorld';
import { drawTerritories } from './TerritoryAtlas';

/** 画布配色 */
const INK_BG = '#241c13';
const PAPER_TEXT = '#eadfc0';
const CINNABAR_MARK = '#c2333f';
const AMBER_MARK = '#e0b04a';
const CANVAS_FONT = '"Qianlin Song", "Songti SC", serif';

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

export interface AtlasViewOptions {
  artificial?: ArtificialWorld;
  canvas: HTMLCanvasElement;
  generator: WorldGenerator;
  onChunkActivate?: (info: TerritoryChunkInfo) => void;
  onChunkSelect?: (info: TerritoryChunkInfo) => void;
  onStatsUpdate?: (stats: ViewportStats) => void;
}

export class AtlasView {
  private readonly icons = new Map<string, HTMLImageElement>();
  private readonly canvas: HTMLCanvasElement;
  private readonly ctx: CanvasRenderingContext2D;
  private generator: WorldGenerator;
  private artificial: ArtificialWorld;
  private territoryDiscovery: Generator<void> | null = null;
  private territoryDiscoveryKey = '';

  // 视图控制：以中心区块坐标与像素偏移
  private centerCx = 0;
  private centerCz = 0;
  private panX = 0;
  private panY = 0;
  private scale = 1.0;
  private readonly baseCellSize = 36;
  private planetView: PlanetView = 'globe';
  private preferredOverview: PlanetView = 'globe';
  private readonly planetLayer: PlanetAtlasLayer;
  private readonly surfaceCache: AtlasSurfaceCache;
  private showChunkColors=false;

  // 状态控制（进入2D地图界面后，默认只显示色块）
  private showChunkInfo = false;     // 显示/隐藏区块信息（编号、类型）
  private showProjection = false;    // 显示/隐藏具体投影（地势阴影浮雕与水系流向）
  private showClimate = false;       // 显示气候分类
  private activeClimateFilter: number | null = null; // 单个气候高亮（0..4）

  // 区块筛选控制（红色轮廓线）
  private filterTypeId: number | null = null;
  private filterCategory: string | null = null;

  // 交互状态
  private selectedChunk: { cx: number; cz: number } | null = null;
  private hoveredChunk: { cx: number; cz: number } | null = null;
  private isDragging = false;
  private dragStartX = 0;
  private dragStartY = 0;
  private lastDragX = 0;
  private lastDragY = 0;
  private hasMoved = false;

  private onChunkSelect?: (info: TerritoryChunkInfo) => void;
  private onChunkActivate?: (info: TerritoryChunkInfo) => void;
  private onStatsUpdate?: (stats: ViewportStats) => void;
  private resizeObserver: ResizeObserver | null = null;
  private renderScheduled = false;
  private readonly events = new AbortController();
  private destroyed = false;
  private active = true;

  constructor(options: AtlasViewOptions) {
    this.canvas = options.canvas;
    for (const name of ['flag-pin', 'flow-e', 'flow-ne', 'flow-n', 'flow-nw', 'flow-w', 'flow-sw', 'flow-s', 'flow-se']) {
      const image = new Image(); image.onload = () => { if (!this.destroyed) this.requestRender(); };
      image.src = iconUrl(name); this.icons.set(name, image);
    }
    this.ctx = this.canvas.getContext('2d')!;
    this.generator = options.generator;
    this.artificial = options.artificial ?? new ArtificialWorld(this.generator);
    this.planetLayer = new PlanetAtlasLayer(this.generator);
    this.surfaceCache=new AtlasSurfaceCache(this.generator,()=>{if(this.active)this.requestRender();});
    this.onChunkSelect = options.onChunkSelect;
    this.onChunkActivate = options.onChunkActivate;
    this.onStatsUpdate = options.onStatsUpdate;

    this.initEvents();
    this.setupResize();
    this.centerSpawn();
    if (this.isPlanet) { this.planetView = 'globe'; this.showOverview(); }
  }

  setGenerator(generator: WorldGenerator, artificial?: ArtificialWorld): void {
    const view = this.planetView;
    this.generator = generator;
    this.artificial = artificial ?? new ArtificialWorld(generator);
    this.territoryDiscovery = null; this.territoryDiscoveryKey = '';
    this.planetLayer.setGenerator(generator);
    this.surfaceCache.setGenerator(generator);
    this.centerSpawn();
    if (this.isPlanet) { this.planetView = view; this.showOverview(); }
    this.requestRender();
  }

  centerSpawn(): void {
    const spawn = (this.artificial.playerFactionId ? this.artificial.territories.get(this.artificial.playerFactionId) : undefined) ?? this.generator.findSpawnChunk();
    this.centerAt(spawn.cx, spawn.cz);
  }

  centerAt(cx: number, cz: number): void {
    this.scale = 1;
    this.planetView = 'projection';
    this.centerCx = cx;
    this.centerCz = cz;
    this.panX = 0;
    this.panY = 0;
    this.selectedChunk = { cx, cz };
    if (this.onChunkSelect) {
      this.onChunkSelect(this.artificial.info(cx, cz));
    }
    this.requestRender();
  }

  setActive(value: boolean): void {
    this.active = value;
    this.isDragging = false;
    this.canvas.style.cursor = 'grab';
    if (value) this.requestRender();
  }

  resetView(): void {
    if (this.isPlanet) { this.planetView = this.preferredOverview; this.showOverview(); return; }
    this.scale = 1.0;
    this.panX = 0;
    this.panY = 0;
    this.requestRender();
  }

  zoomBy(factor: number, clientX?: number, clientY?: number): void {
    const rect = this.canvas.getBoundingClientRect();
    const cx = clientX !== undefined ? clientX - rect.left : this.canvas.clientWidth / 2;
    const cy = clientY !== undefined ? clientY - rect.top : this.canvas.clientHeight / 2;

    const oldScale = this.scale;
    const newScale = Math.max(this.isPlanet ? this.overviewScale : .35, Math.min(8, oldScale * factor));
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
        this.planetView = 'projection';
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
  private get overviewScale(): number {
    const size = this.generator.generation.planet.equatorChunks;
    return Math.min(this.canvas.clientWidth / size, this.canvas.clientHeight / (size / 2)) * .88 / this.baseCellSize;
  }
  private get globeRadius(): number {
    return Math.min(this.canvas.clientWidth, this.canvas.clientHeight) * .43 * this.scale / this.overviewScale;
  }
  private get isGlobe(): boolean { return this.isPlanet && this.planetView === 'globe'; }

  setPlanetView(view: PlanetView): void {
    if (!this.isPlanet) return;
    const center = this.getCenterCoordinates();
    this.centerCx = Math.round(center.cx); this.centerCz = Math.round(center.cz);
    this.panX = 0; this.panY = 0; this.planetView = view;
    this.preferredOverview = view;
    this.scale = this.overviewScale;
    this.requestRender();
  }

  showOverview(): void {
    this.panX = 0; this.panY = 0; this.scale = this.overviewScale;
    if (this.planetView === 'globe') {
      const size = this.generator.generation.planet.equatorChunks;
      this.centerCx = size * 20 / 360 - .5; this.centerCz = -size * 20 / 360 - .5;
    } else { this.centerCx = 0; this.centerCz = 0; }
    this.requestRender();
  }

  private panBy(dx: number, dy: number): void {
    if (this.isGlobe) {
      const size = this.generator.generation.planet.equatorChunks;
      this.centerCx -= dx / this.globeRadius * size / (Math.PI * 2);
      this.centerCz = Math.max(-size / 4 + 1, Math.min(size / 4 - 1,
        this.centerCz - dy / this.globeRadius * size / (Math.PI * 2)));
    } else {
      this.panX += dx; this.panY += dy;
      if (this.isPlanet) {
        const size = this.generator.generation.planet.equatorChunks, cell = this.baseCellSize * this.scale;
        this.panY = Math.max((this.centerCz - size / 4) * cell, Math.min((this.centerCz + size / 4) * cell, this.panY));
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

  setShowProjection(show: boolean): void {
    this.showProjection = show;
    this.requestRender();
  }

  getShowProjection(): boolean {
    return this.showProjection;
  }

  setShowClimate(show: boolean): void {
    this.showClimate = show;
    this.requestRender();
  }

  getShowClimate(): boolean {
    return this.showClimate;
  }

  setShowChunkColors(show:boolean):void{this.showChunkColors=show;this.requestRender();}
  getShowChunkColors():boolean{return this.showChunkColors;}

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
    return this.artificial.info(this.selectedChunk.cx, this.selectedChunk.cz);
  }

  getScalePercent(): number {
    return Math.round(this.scale * 100);
  }

  getCenterCoordinates(): { cx: number; cz: number } {
    const cellSize = this.baseCellSize * this.scale;
    const cx = this.centerCx - Math.round(this.panX / cellSize);
    const cz = this.centerCz - Math.round(this.panY / cellSize);
    return { cx, cz };
  }

  // ---- 渲染调度 ----

  requestRender(): void {
    if (this.renderScheduled || this.destroyed) return;
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
  }

  private isChunkMatched(info: ChunkInfo): boolean {
    if (this.filterTypeId !== null) {
      return info.type === this.filterTypeId;
    }
    if (this.filterCategory) {
      switch (this.filterCategory) {
        case 'river':
          return isRiverType(info.type);
        case 'water':
          return info.type === 2 || isRiverType(info.type) || info.type % 100 === 2 || info.type === 105 || info.type===11;
        case 'mountain':
          return (
            info.type === 3 ||
            info.type === 8 ||
            info.type % 100 === 3 ||
            info.type % 100 === 8 ||
            info.type === 406
          );
        case 'snow':
          return info.type === 406 || info.type === 409;
        case 'forest':
          return (
            info.type === 6 ||
            info.type % 100 === 4 ||
            info.type % 100 === 5 ||
            info.type % 100 === 6 ||
            info.type === 109
          );
      }
    }
    return false;
  }

  private getActiveFilterLabel(): string {
    if (this.filterTypeId !== null) {
      const def = getChunkTypeDef(this.filterTypeId);
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
    this.surfaceCache.beginFrame();
    const ctx = this.ctx;
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    const width = this.canvas.width / dpr;
    const height = this.canvas.height / dpr;

    ctx.save();
    ctx.scale(dpr, dpr);

    // 背景：与视口外框同为暖墨色，让地图色块浮出来
    ctx.fillStyle = INK_BG;
    ctx.fillRect(0, 0, width, height);

    if (this.isPlanet && (this.isGlobe || this.baseCellSize * this.scale < 12)) {
      this.drawPlanetOverview(ctx, width, height);
      ctx.restore(); return;
    }

    const cellSize = this.baseCellSize * this.scale;
    const surfaceMode=!this.showChunkColors&&!this.showClimate&&cellSize>=Math.max(24,Math.sqrt(width*height/512));
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

    // 预先批量请求可见区块流向
    const wantFlow = this.showProjection;
    if (wantFlow) this.generator.displayFlowWanted = true;
    try {
    for (let cz = minCz; cz <= maxCz; cz++) {
      for (let cx = minCx; cx <= maxCx; cx++) {
        if (this.isPlanet && (cz < -this.generator.generation.planet.equatorChunks / 4 || cz >= this.generator.generation.planet.equatorChunks / 4)) continue;
        const sx = halfW + this.panX + (cx - this.centerCx) * cellSize;
        const sy = halfH + this.panY + (cz - this.centerCz) * cellSize;

        if (sx + cellSize < 0 || sx > width || sy + cellSize < 0 || sy > height) {
          continue;
        }

        const info = this.artificial.info(cx, cz);
        visibleChunks.push({ info, sx, sy });

        visibleCount++;
        climateCounts[info.climate]++;
        terrainCounts.set(info.type, (terrainCounts.get(info.type) ?? 0) + 1);

        if (isFiltering && this.isChunkMatched(info)) {
          matchedCount++;
        }
      }
    }
    } finally {
      if (wantFlow) this.generator.displayFlowWanted = false;
    }

    // 1. 绘制基础底色（默认模式下只显示色块）
    for (const item of visibleChunks) {
      const { info, sx, sy } = item;
      const def = getChunkTypeDef(info.type);

      let color = this.showChunkColors ? def.mapColor : `rgb(${overviewColor(info,false,false).join(',')})`;
      const tile=surfaceMode?this.surfaceCache.get(info.cx,info.cz,(sx+cellSize/2-width/2)**2+(sy+cellSize/2-height/2)**2):undefined;
      if(tile)color=tile.average;
      if (this.showClimate) {
        color = CLIMATES[info.climate].color;
        // 如果激活了单项气候高亮，则弱化其他气候色块
        if (this.activeClimateFilter !== null && info.climate !== this.activeClimateFilter) {
          color = this.dimHexColor(color, 0.28);
        }
      }

      ctx.fillStyle = color;
      ctx.fillRect(sx, sy, cellSize, cellSize);
      if(tile&&cellSize>=72){ctx.imageSmoothingEnabled=false;ctx.drawImage(tile.image,sx,sy,cellSize,cellSize);}

      // 区块网格线（微弱分割）
      ctx.strokeStyle = 'rgba(0, 0, 0, 0.12)';
      ctx.lineWidth = 1;
      ctx.strokeRect(sx, sy, cellSize, cellSize);
    }

    // 2. 显示具体投影（地势起伏浮雕阴影与河流走向投
    if (this.showProjection && !surfaceMode) {
      for (const item of visibleChunks) {
        const { info, sx, sy } = item;
        const cx = info.cx;
        const cz = info.cz;

        // 计算相邻区块的高程差
        const eastH = this.artificial.info(cx + 1, cz).elevation;
        const westH = this.artificial.info(cx - 1, cz).elevation;
        const southH = this.artificial.info(cx, cz + 1).elevation;
        const northH = this.artificial.info(cx, cz - 1).elevation;

        const dx = (eastH - westH) * 0.5;
        const dz = (southH - northH) * 0.5;
        // 西北光矢量：(-0.707, -0.707)
        const slopeLighting = (-dx * 0.707 - dz * 0.707) * 0.012;

        if (slopeLighting > 0) {
          // 向阳坡光照投影
          const alpha = Math.min(0.38, slopeLighting * 0.45);
          ctx.fillStyle = `rgba(255, 255, 255, ${alpha.toFixed(3)})`;
          ctx.fillRect(sx, sy, cellSize, cellSize);
        } else if (slopeLighting < 0) {
          // 背阴坡阴影投影
          const alpha = Math.min(0.42, -slopeLighting * 0.48);
          ctx.fillStyle = `rgba(0, 0, 0, ${alpha.toFixed(3)})`;
          ctx.fillRect(sx, sy, cellSize, cellSize);
        }

        // 深水体加暗投影
        if (info.elevation <= 0) {
          const depthAlpha = Math.min(0.28, Math.abs(info.elevation) / 90);
          ctx.fillStyle = `rgba(3, 20, 50, ${depthAlpha.toFixed(3)})`;
          ctx.fillRect(sx, sy, cellSize, cellSize);
        }

        // 高海拔冷光山脊投影
        const snowHeight = snowLine(info.temperature);
        if (info.elevation > snowHeight) {
          const peakAlpha = Math.min(0.32, (info.elevation - snowHeight) / 480);
          ctx.fillStyle = `rgba(240, 248, 255, ${peakAlpha.toFixed(3)})`;
          ctx.fillRect(sx, sy, cellSize, cellSize);
        }

      }

    }

    // 局部地图默认显示实际中心线及河宽
    if((!surfaceMode||cellSize<72)&&!this.showClimate&&cellSize>=6){
      ctx.save();
      ctx.strokeStyle = '#38bdf8';
      ctx.lineCap = 'round';
      ctx.lineJoin = 'round';
      const riverPadding = Math.ceil((MAX_RIVER_RADIUS + CHUNK_SIZE + 14 + MAX_BRAID_OFFSET + MAX_MOUTH_LENGTH+MAX_RIVER_BEND) / CHUNK_SIZE);
      for (let cz = minCz - riverPadding; cz <= maxCz + riverPadding; cz++) {
        for (let cx = minCx - riverPadding; cx <= maxCx + riverPadding; cx++) {
          if (this.isPlanet && (cz < -this.generator.generation.planet.equatorChunks / 4 || cz >= this.generator.generation.planet.equatorChunks / 4)) continue;
          const sx = halfW + this.panX + (cx - this.centerCx) * cellSize;
          const sy = halfH + this.panY + (cz - this.centerCz) * cellSize;
          for (const path of this.generator.getRiverPaths(cx, cz)) {
            for (let i = 1; i < path.length; i++) {
              const a = path[i - 1], b = path[i];
              ctx.lineWidth = Math.max(1, (a.width + b.width) * cellSize / CHUNK_SIZE);
              ctx.beginPath();
              ctx.moveTo(sx + (a.x / CHUNK_SIZE - cx) * cellSize, sy + (a.z / CHUNK_SIZE - cz) * cellSize);
              ctx.lineTo(sx + (b.x / CHUNK_SIZE - cx) * cellSize, sy + (b.z / CHUNK_SIZE - cz) * cellSize);
              ctx.stroke();
            }
          }
        }
      }
      // 所有河区块都标出流向；放大到能分辨区块时才画
      if (this.showProjection && cellSize >= 10) for (const {info, sx, sy} of visibleChunks) {
        if (!isRiverType(info.type) || info.displayFlow < 0) continue;
        ctx.fillStyle = '#ffffff';
        ctx.shadowColor = '#000000';
        ctx.shadowBlur = 3;
        ctx.font = `bold ${Math.max(10, Math.round(cellSize * .36))}px ${CANVAS_FONT}`;
        ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
        const flow = FLOW_DIRECTIONS[info.displayFlow];
        const name = `flow-${flow.dz < 0 ? 'n' : flow.dz > 0 ? 's' : ''}${flow.dx < 0 ? 'w' : flow.dx > 0 ? 'e' : ''}`;
        const image = this.icons.get(name), size = Math.max(12, Math.min(48, cellSize * .6));
        if (image?.complete && image.naturalWidth) ctx.drawImage(image, sx + (cellSize-size)/2, sy + (cellSize-size)/2, size, size);
        else ctx.fillText(flow.arrow, sx + cellSize * .5, sy + cellSize * .5);
      }
      ctx.restore();
    }

    // 3. 显示区块信息（编号、类型）
    if (this.showChunkInfo) {
      for (const item of visibleChunks) {
        const { info, sx, sy } = item;
        const def = getChunkTypeDef(info.type);
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

    this.drawTerritoryLayer(ctx, width, height, cellSize);

    // 7. 出生点标记（金色罗盘星）
    const spawn = (this.artificial.playerFactionId ? this.artificial.territories.get(this.artificial.playerFactionId) : undefined) ?? this.generator.findSpawnChunk();
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
    if(surfaceMode){
      const text=this.surfaceCache.loading?t('atlas.surface.loading'):t(cellSize>=72?'atlas.surface.detail':'atlas.surface.average');
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

  private drawPlanetOverview(ctx: CanvasRenderingContext2D, width: number, height: number): void {
    const size = this.generator.generation.planet.equatorChunks, cell = this.baseCellSize * this.scale;
    const counts: [number, number, number, number, number] = [0, 0, 0, 0, 0];
    let samples = 0, matches = 0;
    const style = { climate: this.showClimate, relief: this.showProjection, climateFilter: this.activeClimateFilter,chunkColors:this.showChunkColors,
      filterKey: this.getActiveFilterLabel(), matches: (info: ChunkInfo) => this.isChunkMatched(info) };
    if (this.isGlobe) {
      const coord = planetCoordinates(this.centerCx, this.centerCz, size);
      this.planetLayer.drawGlobe(ctx, width, height, this.globeRadius, coord.longitude, coord.latitude, style);
      // 对可见半球做稀疏统计
      for (let y = 12; y < height; y += 24) for (let x = 12; x < width; x += 24) {
        const chunk = this.getChunkAtPoint(x, y);
        if (!chunk) continue;
        const info = this.generator.getOverviewInfo(chunk.cx, chunk.cz);
        counts[info.climate]++; samples++;
        if (this.isChunkMatched(info)) matches++;
      }
    } else {
      const texture = cell < .7;
      if (texture) this.planetLayer.drawProjection(ctx, width, height, cell, this.centerCx, this.centerCz, this.panX, this.panY, style);
      const step = Math.max(1, Math.ceil((texture ? 24 : 3) / cell)), tile = step * cell;
      const minX = this.centerCx + Math.floor((-width / 2 - this.panX) / cell / step) * step;
      const minZ = this.centerCz + Math.floor((-height / 2 - this.panY) / cell / step) * step;
      const maxX = this.centerCx + (width / 2 - this.panX) / cell;
      const maxZ = this.centerCz + (height / 2 - this.panY) / cell;
      for (let z = minZ; z <= maxZ; z += step) for (let x = minX; x <= maxX; x += step) {
        if (z < -size / 4 || z >= size / 4) continue;
        const info = this.generator.getOverviewInfo(x + (step - 1) / 2, z + (step - 1) / 2);
        const sx = width / 2 + this.panX + (x - this.centerCx) * cell;
        const sy = height / 2 + this.panY + (z - this.centerCz) * cell;
        let color = overviewColor(info, this.showClimate, this.showProjection,this.showChunkColors);
        if (this.showClimate && this.activeClimateFilter !== null && info.climate !== this.activeClimateFilter) color = color.map(c => c * .35) as [number, number, number];
        if (!texture) { ctx.fillStyle = `rgb(${color.join(',')})`; ctx.fillRect(sx, sy, tile + .5, tile + .5); }
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
    this.drawTerritoryLayer(ctx, width, height, cell);
    ctx.fillStyle = PAPER_TEXT; ctx.font = `13px ${CANVAS_FONT}`; ctx.textAlign = 'left';
    ctx.fillText(this.isGlobe ? t('atlas.canvas.globeHint') : t('atlas.canvas.projectionHint'), 20, 28);
    const center = this.getCenterCoordinates();
    this.onStatsUpdate?.({ centerCx: Math.round(center.cx), centerCz: Math.round(center.cz), scale: this.scale,
      visibleChunks: samples, climateCounts: counts,
      climatePercents: counts.map(n => samples ? n / samples * 100 : 0) as [number, number, number, number, number],
      matchedCount: matches, matchedPercent: samples ? matches / samples * 100 : 0,
      activeFilterName: this.getActiveFilterLabel(), level: this.isGlobe ? 'globe' : 'overview' });
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

  private initEvents(): void {
    const canvas = this.canvas;

    // 鼠标按下：启动拖拽
    canvas.addEventListener('mousedown', (e) => {
      if (!this.active) return;
      if (e.button !== 0) return;
      this.isDragging = true;
      this.hasMoved = false;
      this.dragStartX = e.clientX;
      this.dragStartY = e.clientY;
      this.lastDragX = e.clientX;
      this.lastDragY = e.clientY;
      canvas.style.cursor = 'grabbing';
    }, { signal: this.events.signal });

    // 鼠标移动：平移视口或更新悬停
    window.addEventListener('mousemove', (e) => {
      if (!this.active) return;
      if (this.isDragging) {
        const dx = e.clientX - this.lastDragX;
        const dy = e.clientY - this.lastDragY;
        if (Math.hypot(e.clientX - this.dragStartX, e.clientY - this.dragStartY) > 3) {
          this.hasMoved = true;
        }
        this.panBy(dx, dy);
        this.lastDragX = e.clientX;
        this.lastDragY = e.clientY;
        this.requestRender();
      } else {
        const rect = canvas.getBoundingClientRect();
        if (
          e.clientX >= rect.left &&
          e.clientX <= rect.right &&
          e.clientY >= rect.top &&
          e.clientY <= rect.bottom
        ) {
          const chunk = this.getChunkAtPoint(e.clientX - rect.left, e.clientY - rect.top);
          if (!chunk) { this.hoveredChunk = null; return; }
          if (
            !this.hoveredChunk ||
            this.hoveredChunk.cx !== chunk.cx ||
            this.hoveredChunk.cz !== chunk.cz
          ) {
            this.hoveredChunk = chunk;
            if (!this.isPlanet || !this.isGlobe && this.baseCellSize * this.scale >= 12) this.requestRender();
          }
        } else if (this.hoveredChunk) {
          this.hoveredChunk = null;
          this.requestRender();
        }
      }
    }, { signal: this.events.signal });

    // 鼠标松开：若无明显拖动则视为点击选中
    window.addEventListener('mouseup', (e) => {
      if (!this.active) return;
      if (!this.isDragging) return;
      this.isDragging = false;
      canvas.style.cursor = 'grab';

      if (!this.hasMoved) {
        const rect = canvas.getBoundingClientRect();
        if (
          e.clientX >= rect.left &&
          e.clientX <= rect.right &&
          e.clientY >= rect.top &&
          e.clientY <= rect.bottom
        ) {
          const chunk = this.getChunkAtPoint(e.clientX - rect.left, e.clientY - rect.top);
          if (!chunk) return;
          this.selectedChunk = chunk;
          const info = this.artificial.info(chunk.cx, chunk.cz);
          if (this.onChunkSelect) {
            this.onChunkSelect(info);
          }
          this.requestRender();
        }
      }
    }, { signal: this.events.signal });

    // 滚轮缩放：以鼠标指针为中心平滑缩放
    canvas.addEventListener(
      'wheel',
      (e) => {
        if (!this.active) return;
        e.preventDefault();
        const factor = e.deltaY < 0 ? 1.15 : 0.87;
        this.zoomBy(factor, e.clientX, e.clientY);
      },
      { passive: false, signal: this.events.signal }
    );

    canvas.addEventListener('dblclick', (e) => {
      if (!this.active) return;
      const rect = canvas.getBoundingClientRect(), chunk = this.getChunkAtPoint(e.clientX - rect.left, e.clientY - rect.top);
      if (!chunk) return;
      if (this.onChunkActivate) { this.onChunkActivate(this.artificial.info(chunk.cx, chunk.cz)); return; }
      if (!this.isPlanet) return;
      this.centerCx = chunk.cx; this.centerCz = chunk.cz; this.panX = 0; this.panY = 0;
      this.planetView = 'projection'; this.scale = 1;
      this.selectedChunk = chunk; this.onChunkSelect?.(this.artificial.info(chunk.cx, chunk.cz));
      this.requestRender();
    }, { signal: this.events.signal });

    // 触控支持（移动端/触摸板）
    let touchStartDist = 0;
    let lastTap: { time: number; x: number; y: number } | null = null;
    canvas.addEventListener(
      'touchstart',
      (e) => {
        if (!this.active) return;
        if (e.touches.length === 1) {
          this.isDragging = true;
          this.hasMoved = false;
          this.dragStartX = e.touches[0].clientX;
          this.dragStartY = e.touches[0].clientY;
          this.lastDragX = e.touches[0].clientX;
          this.lastDragY = e.touches[0].clientY;
        } else if (e.touches.length === 2) {
          this.isDragging = false;
          lastTap = null; this.hasMoved = true;
          touchStartDist = Math.hypot(
            e.touches[0].clientX - e.touches[1].clientX,
            e.touches[0].clientY - e.touches[1].clientY
          );
        }
      },
      { passive: true, signal: this.events.signal }
    );

    canvas.addEventListener(
      'touchmove',
      (e) => {
        if (!this.active) return;
        if (e.touches.length === 1 && this.isDragging) {
          const dx = e.touches[0].clientX - this.lastDragX;
          const dy = e.touches[0].clientY - this.lastDragY;
          this.panBy(dx, dy);
          this.lastDragX = e.touches[0].clientX;
          this.lastDragY = e.touches[0].clientY;
          if (Math.hypot(e.touches[0].clientX - this.dragStartX, e.touches[0].clientY - this.dragStartY) > 8) this.hasMoved = true;
          this.requestRender();
        } else if (e.touches.length === 2 && touchStartDist > 0) {
          const dist = Math.hypot(
            e.touches[0].clientX - e.touches[1].clientX,
            e.touches[0].clientY - e.touches[1].clientY
          );
          const factor = dist / touchStartDist;
          touchStartDist = dist;
          const midX = (e.touches[0].clientX + e.touches[1].clientX) / 2;
          const midY = (e.touches[0].clientY + e.touches[1].clientY) / 2;
          this.zoomBy(factor, midX, midY);
        }
      },
      { passive: true, signal: this.events.signal }
    );

    canvas.addEventListener(
      'touchend',
      (e) => {
        if (!this.active) return;
        if (this.isDragging && !this.hasMoved && e.changedTouches.length === 1) {
          const rect = canvas.getBoundingClientRect();
          const t = e.changedTouches[0];
          const chunk = this.getChunkAtPoint(t.clientX - rect.left, t.clientY - rect.top);
          if (chunk) {
            this.selectedChunk = chunk;
            const info = this.artificial.info(chunk.cx, chunk.cz);
            if (this.onChunkSelect) this.onChunkSelect(info);
            const now = performance.now();
            if (lastTap && now - lastTap.time < 350 && Math.hypot(t.clientX - lastTap.x, t.clientY - lastTap.y) < 24) {
              lastTap = null;
              if (this.onChunkActivate) this.onChunkActivate(info); else this.centerAt(chunk.cx, chunk.cz);
            } else lastTap = { time: now, x: t.clientX, y: t.clientY };
            this.requestRender();
          }
        }
        this.isDragging = false;
        touchStartDist = 0;
      },
      { passive: true, signal: this.events.signal }
    );
    canvas.addEventListener('touchcancel', () => {
      this.isDragging = false; this.hasMoved = true; touchStartDist = 0; lastTap = null;
    }, { signal: this.events.signal });
  }

  // 像素坐标转换为区块坐标 (cx, cz)
  private drawTerritoryLayer(ctx: CanvasRenderingContext2D, width: number, height: number, cell: number): void {
    if (!this.artificial.settings.enabled && !this.artificial.playerFactionId) return;
    const size = this.generator.generation.planet.equatorChunks;
    const s = this.artificial.generator.cellSize;
    const bounds = this.isGlobe ? [-size / 2, -size / 4, size / 2 - 1, size / 4 - 1]
      : [this.centerCx + (-width / 2 - this.panX) / cell, this.centerCz + (-height / 2 - this.panY) / cell,
        this.centerCx + (width / 2 - this.panX) / cell, this.centerCz + (height / 2 - this.panY) / cell];
    const key = bounds.map(n => Math.floor(n / s)).join(',');
    if (key !== this.territoryDiscoveryKey) {
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
      const dx = this.isPlanet ? this.artificial.generator.wrap(x - this.centerCx) : x - this.centerCx;
      return [width / 2 + this.panX + dx * cell, height / 2 + this.panY + (z - this.centerCz) * cell];
    }, width, height, cell >= 20 && !this.showChunkInfo);
  }

  private getChunkAtPoint(px: number, py: number): { cx: number; cz: number } | null {
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    const width = this.canvas.width / dpr;
    const height = this.canvas.height / dpr;
    const cellSize = this.baseCellSize * this.scale;
    const halfW = width / 2;
    const halfH = height / 2;

    if (this.isGlobe) {
      const size = this.generator.generation.planet.equatorChunks;
      const center = planetCoordinates(this.centerCx, this.centerCz, size);
      const coord = globePoint((px - halfW) / this.globeRadius, (halfH - py) / this.globeRadius, center.longitude, center.latitude);
      return coord ? { cx: Math.floor(coord.longitude / 360 * size),
        cz: Math.max(-size / 4, Math.min(size / 4 - 1, Math.floor(-coord.latitude / 360 * size))) } : null;
    }

    const cx = Math.floor(this.centerCx + (px - halfW - this.panX) / cellSize);
    const cz = Math.floor(this.centerCz + (py - halfH - this.panY) / cellSize);
    if (this.isPlanet && (cz < -this.generator.generation.planet.equatorChunks / 4 || cz >= this.generator.generation.planet.equatorChunks / 4)) return null;
    return { cx, cz };
  }

  destroy(): void {
    this.destroyed = true;
    this.surfaceCache.dispose();
    this.events.abort();
    if (this.resizeObserver) {
      this.resizeObserver.disconnect();
      this.resizeObserver = null;
    }
  }
}
