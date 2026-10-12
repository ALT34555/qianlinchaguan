import { WorldGenerator, type ChunkInfo } from '../systems/world/WorldGenerator';
import { getChunkTypeDef } from '../systems/world/ChunkTypes';
import { CLIMATES } from '../systems/world/WorldSettings';
import { snowLine } from '../systems/world/TerrainLayers';

export type PlanetView = 'globe' | 'projection';
export interface OverviewStyle {
  chunkColors?:boolean;
  climate: boolean; relief: boolean; climateFilter: number | null;
  filterKey: string; matches: (type: number) => boolean;
}
const RAD = Math.PI / 180;

/** 全览纹理完整分辨率 */
const OVERVIEW_WIDTH = 720;
const OVERVIEW_HEIGHT = 360;
/** 首屏粗纹理宽度，高度取其一半 */
const OVERVIEW_COARSE_WIDTH = 96;
/** 每帧烘焙时间预算（毫秒） */
const OVERVIEW_BAKE_BUDGET = 20;
/** 尚未烘焙到的像素底色 */
const UNBAKED: readonly [number, number, number] = [47, 74, 92];

/** 正射球体的反投影；背面与球体外没有可点击地块。 */
export function globePoint(u: number, v: number, longitude: number, latitude: number): { longitude: number; latitude: number } | null {
  if (u * u + v * v > 1) return null;
  const depth = Math.sqrt(Math.max(0, 1 - u * u - v * v)), phi = latitude * RAD;
  const lat = Math.asin(Math.max(-1, Math.min(1, v * Math.cos(phi) + depth * Math.sin(phi))));
  const lon = longitude * RAD + Math.atan2(u, depth * Math.cos(phi) - v * Math.sin(phi));
  return { longitude: ((lon / RAD + 540) % 360) - 180, latitude: lat / RAD };
}

/** 海面与内陆静水（湖、封冻静水）都算水面 */
export const isStillWater = (info: ChunkInfo): boolean => info.elevation <= 0 || info.lakeLevel !== null;

/** 打包成 0xRRGGBB：逐像素烘焙用，避免每像素分配数组 */
export function overviewColorValue(type: number, climate: number, temperature: number, elevation: number,
  lakeLevel: number | null, palette: boolean, relief: boolean, chunkColors = false): number {
  let hex = palette ? CLIMATES[climate].color : getChunkTypeDef(type).mapColor;
  // 连续色：内陆静水不随植被或雪线转色
  if (!palette && !chunkColors) {
    hex = elevation <= 0 ? '#397aa1' : lakeLevel !== null ? '#3f86ad'
      : temperature < 0 || elevation > snowLine(temperature) ? '#e3ebe1'
      : type === 104 || type === 4 ? '#c5b078' : elevation > 768 ? '#8e947a' : '#73965e';
  }
  const value = parseInt(hex.slice(1), 16);
  const light = relief ? elevation <= 0 ? 1 - Math.min(.4, -elevation / 280)
    : 1 + Math.min(.2, elevation / 4000) : 1;
  return (Math.min(255, Math.round((value >> 16) * light)) << 16) |
    (Math.min(255, Math.round((value >> 8 & 255) * light)) << 8) |
    Math.min(255, Math.round((value & 255) * light));
}

export function overviewColor(info: ChunkInfo, climate: boolean, relief: boolean,chunkColors=false): [number, number, number] {
  const value = overviewColorValue(info.type, info.climate, info.temperature, info.elevation, info.lakeLevel, climate, relief, chunkColors);
  return [value >> 16, value >> 8 & 255, value & 255];
}

/** 逐帧烘焙的纹理层：样本与样式分离，换样式只需重新着色 */
interface BakeTier {
  readonly width: number;
  readonly height: number;
  readonly canvas: HTMLCanvasElement;
  readonly image: ImageData;
  /** 原始样本：类型 / 气候 */
  readonly kinds: Uint16Array;
  /** 原始样本：气温 / 海拔 */
  readonly values: Float32Array;
  sampled: number;
  colored: number;
  /** 已着色行对应的样式标识 */
  key: string;
}

function createTier(width: number, height: number): BakeTier {
  const canvas = document.createElement('canvas');
  canvas.width = width; canvas.height = height;
  const pixels = width * height;
  return { width, height, canvas, image: new ImageData(width, height),
    kinds: new Uint16Array(pixels * 2), values: new Float32Array(pixels * 2), sampled: 0, colored: 0, key: '' };
}

export class PlanetAtlasLayer {
  /** 完整分辨率层，逐帧补齐 */
  private readonly full:BakeTier;
  /** 粗屏层，先于完整分辨率完成 */
  private readonly coarse = createTier(OVERVIEW_COARSE_WIDTH, OVERVIEW_COARSE_WIDTH / 2);
  private readonly globeCanvas = document.createElement('canvas');
  /** 球体反投影缓存：视角不变时纹理更新只需重抄像素 */
  private mapKey = '';
  private mapFull: Int32Array | null = null;
  private mapCoarse: Int32Array | null = null;
  private mapLight: Float32Array | null = null;
  private raster: ImageData | null = null;
  private rasterKey = '';
  constructor(private generator: WorldGenerator, private readonly redraw: () => void = () => {},private readonly coarseOnly=false) {
    this.full=coarseOnly?this.coarse:createTier(OVERVIEW_WIDTH,OVERVIEW_HEIGHT);
  }

  setGenerator(generator: WorldGenerator): void {
    this.generator = generator;
    this.rasterKey = '';
    for (const tier of [this.full, this.coarse]) {
      tier.sampled = 0; tier.colored = 0; tier.key = ''; tier.image.data.fill(0);
    }
  }

  /** 纹理标识：样式与筛选一起决定是否需重新着色 */
  private styleKey(style: OverviewStyle): string {
    return `${style.climate},${style.relief},${style.climateFilter},${style.filterKey},${style.chunkColors}`;
  }

  /** 当前样式是否已有完整纹理；分帧生成垫底只用成品 */
  hasTexture(style: OverviewStyle): boolean {
    return this.full.key === this.styleKey(style) && this.full.colored >= this.full.height;
  }

  /** 读取已烘焙的原始样本，供球体统计免去逐点重算 */
  sampleAt(longitude: number, latitude: number): { type: number; climate: number } | null {
    const tier = this.full;
    const row = Math.max(0, Math.min(tier.height - 1, Math.floor((90 - latitude) / 180 * tier.height)));
    if (row >= tier.colored) return null;
    const col = Math.floor((longitude + 180) / 360 * tier.width) % tier.width;
    const i = row * tier.width + col;
    return {type: tier.kinds[i * 2], climate: tier.kinds[i * 2 + 1]};
  }

  /** 采样一行原始信息，与样式无关，换样式不必重采 */
  private sampleRow(tier: BakeTier, row: number, size: number): void {
    for (let x = 0; x < tier.width; x++) {
      const info = this.generator.getOverviewInfo((x + .5) / tier.width * size - size / 2 - .5,
        (row + .5) / tier.height * size / 2 - size / 4 - .5);
      const i = row * tier.width + x;
      tier.kinds[i * 2] = info.type; tier.kinds[i * 2 + 1] = info.climate;
      tier.values[i * 2] = info.temperature; tier.values[i * 2 + 1] = info.elevation;
    }
  }

  /** 按当前样式着色一行 */
  private colorRow(tier: BakeTier, row: number, style: OverviewStyle): void {
    const kinds = tier.kinds, values = tier.values, data = tier.image.data;
    for (let x = 0; x < tier.width; x++) {
      const i = row * tier.width + x, type = kinds[i * 2], climate = kinds[i * 2 + 1];
      let value = overviewColorValue(type, climate, values[i * 2], values[i * 2 + 1], null,
        style.climate, style.relief, style.chunkColors);
      if (style.climate && style.climateFilter !== null && climate !== style.climateFilter) {
        value = (Math.round((value >> 16) * .35) << 16) | (Math.round((value >> 8 & 255) * .35) << 8) | Math.round((value & 255) * .35);
      }
      if (style.filterKey && style.matches(type)) {
        value = (Math.min(255, Math.round((value >> 16) * .4 + 160)) << 16) |
          Math.round((value >> 8 & 255) * .4) << 8 | Math.round((value & 255) * .4);
      }
      const at = i * 4;
      data[at] = value >> 16; data[at + 1] = value >> 8 & 255; data[at + 2] = value & 255; data[at + 3] = 255;
    }
  }

  /** 单层推进：先补采样再补着色，整体着色完成才换图 */
  private step(tier: BakeTier, key: string, style: OverviewStyle, size: number, deadline: number): boolean {
    if (tier.key !== key) { tier.key = key; tier.colored = 0; }
    let progressed = false;
    while (tier.sampled < tier.height && (!progressed || performance.now() < deadline)) {
      this.sampleRow(tier, tier.sampled, size); tier.sampled++; progressed = true;
    }
    while (tier.colored < tier.sampled && (!progressed || performance.now() < deadline)) {
      this.colorRow(tier, tier.colored, style); tier.colored++; progressed = true;
    }
    if (progressed && tier.colored === tier.height) tier.canvas.getContext('2d')!.putImageData(tier.image, 0, 0);
    return tier.colored < tier.height;
  }

  /** 先粗后细推进烘焙，未完成则续画 */
  private advance(style: OverviewStyle, detailed = true): void {
    const key = this.styleKey(style), size = this.generator.generation.planet.equatorChunks;
    const deadline = performance.now() + OVERVIEW_BAKE_BUDGET;
    if (this.step(this.coarse, key, style, size, deadline)) { this.redraw(); return; }
    if (detailed&&!this.coarseOnly&&this.step(this.full, key, style, size, deadline)) this.redraw();
  }

  drawProjection(ctx: CanvasRenderingContext2D, width: number, height: number, cell: number,
    centerX: number, centerZ: number, panX: number, panY: number, style: OverviewStyle, detailed = true): void {
    this.advance(style, detailed);
    const key = this.styleKey(style), size = this.generator.generation.planet.equatorChunks, worldWidth = size * cell;
    const originX = width / 2 + panX + (-size / 2 - centerX) * cell;
    const originY = height / 2 + panY + (-size / 4 - centerZ) * cell;
    const complete = this.hasTexture(style);
    const coarse = this.coarse.key === key && this.coarse.colored === this.coarse.height;
    const full = this.full.key === key && this.full.colored === this.full.height;
    ctx.imageSmoothingEnabled = true;
    const first = Math.floor(-originX / worldWidth);
    for (let copy = first; originX + copy * worldWidth < width; copy++) {
      const x = originX + copy * worldWidth, y = originY, h = worldWidth / 2;
      if (coarse && !complete) ctx.drawImage(this.coarse.canvas, x, y, worldWidth, h);
      if (full) ctx.drawImage(this.full.canvas, x, y, worldWidth, h);
    }
  }

  /** 视角不变时复用反投影：烘焙与换样式只重采样纹理 */
  private buildMap(width: number, height: number, radius: number, longitude: number, latitude: number,
    w: number, h: number, stride: number): void {
    const full = this.full, coarse = this.coarse;
    const mapFull = this.mapFull = new Int32Array(w * h).fill(-1);
    const mapCoarse = this.mapCoarse = new Int32Array(w * h).fill(-1);
    const mapLight = this.mapLight = new Float32Array(w * h);
    for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
      const u = (x * stride - width / 2) / radius, v = (height / 2 - y * stride) / radius;
      const coord = globePoint(u, v, longitude, latitude);
      if (!coord) continue;
      const lx = (coord.longitude + 180) / 360, ly = (90 - coord.latitude) / 180;
      const i = y * w + x;
      mapFull[i] = Math.min(full.height - 1, Math.floor(ly * full.height)) * full.width + Math.floor(lx * full.width) % full.width;
      mapCoarse[i] = Math.min(coarse.height - 1, Math.floor(ly * coarse.height)) * coarse.width + Math.floor(lx * coarse.width) % coarse.width;
      mapLight[i] = .62 + .38 * Math.sqrt(Math.max(0, 1 - u * u - v * v)) - .12 * u + .06 * v;
    }
  }

  drawGlobe(ctx: CanvasRenderingContext2D, width: number, height: number, radius: number,
    longitude: number, latitude: number, style: OverviewStyle): void {
    this.advance(style);
    const key = this.styleKey(style);
    const full = this.full, coarse = this.coarse;
    const useFull = full.key === key && full.colored === full.height;
    const useCoarse = coarse.key === key && coarse.colored === coarse.height;
    const stride = 2, w = Math.ceil(width / stride), h = Math.ceil(height / stride);
    if (this.globeCanvas.width !== w || this.globeCanvas.height !== h) {
      this.globeCanvas.width = w; this.globeCanvas.height = h;
    }
    const mapKey = `${width},${height},${radius},${longitude},${latitude}`;
    if (this.mapKey !== mapKey) { this.mapKey = mapKey; this.buildMap(width, height, radius, longitude, latitude, w, h, stride); }
    const mapFull = this.mapFull!, mapCoarse = this.mapCoarse!, mapLight = this.mapLight!;
    if (!this.raster || this.raster.width !== w || this.raster.height !== h) {
      this.raster = new ImageData(w, h); this.rasterKey = '';
    }
    const raster = this.raster;
    const rasterKey = `${mapKey}|${key}|${useFull}|${useCoarse}`;
    if (this.rasterKey !== rasterKey) {
      for (let i = 0; i < mapFull.length; i++) {
        const dst = i * 4, light = mapLight[i];
        if (mapFull[i] < 0) { raster.data[dst + 3] = 0; continue; }
        // 完整纹理整体替换
        let source: Uint8ClampedArray | null = null, src = 0;
        if (useFull) { source = full.image.data; src = mapFull[i] * 4; }
        else if (useCoarse) { source = coarse.image.data; src = mapCoarse[i] * 4; }
        for (let c = 0; c < 3; c++) raster.data[dst + c] = source ? Math.min(255, source[src + c] * light) : UNBAKED[c];
        raster.data[dst + 3] = 255;
      }
      this.globeCanvas.getContext('2d')!.putImageData(raster, 0, 0);
      this.rasterKey = rasterKey;
    }
    ctx.imageSmoothingEnabled = true;
    ctx.drawImage(this.globeCanvas, 0, 0, width, height);
    ctx.strokeStyle = 'rgba(151,208,222,.7)'; ctx.lineWidth = 1.5;
    ctx.beginPath(); ctx.arc(width / 2, height / 2, radius, 0, Math.PI * 2); ctx.stroke();
    // 经纬线同时表达赤道与球体朝向。
    ctx.strokeStyle = 'rgba(237,247,244,.18)'; ctx.lineWidth = 1;
    for (let lat = -60; lat <= 60; lat += 30) this.globeLine(ctx, width, height, radius, longitude, latitude,
      Array.from({ length: 181 }, (_, i) => [i * 2 - 180, lat]));
    for (let lon = -180; lon < 180; lon += 30) this.globeLine(ctx, width, height, radius, longitude, latitude,
      Array.from({ length: 91 }, (_, i) => [lon, i * 2 - 90]));
  }

  private globeLine(ctx: CanvasRenderingContext2D, width: number, height: number, radius: number,
    lon0: number, lat0: number, points: number[][]): void {
    ctx.beginPath(); let active = false;
    for (const [lon, lat] of points) {
      const dl = (lon - lon0) * RAD, phi = lat * RAD, origin = lat0 * RAD;
      const depth = Math.sin(origin) * Math.sin(phi) + Math.cos(origin) * Math.cos(phi) * Math.cos(dl);
      if (depth < 0) { active = false; continue; }
      const x = width / 2 + radius * Math.cos(phi) * Math.sin(dl);
      const y = height / 2 - radius * (Math.cos(origin) * Math.sin(phi) - Math.sin(origin) * Math.cos(phi) * Math.cos(dl));
      if (active) ctx.lineTo(x, y); else ctx.moveTo(x, y);
      active = true;
    }
    ctx.stroke();
  }
}
