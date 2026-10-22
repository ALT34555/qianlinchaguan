import {iceColorValue} from './atlas/ice-cover.mjs';
import {rasterMacroRivers} from './MacroRiverAtlas';
import type { ChunkInfo } from '../systems/world/WorldContract';
import type { WorldGeneratorBase as WorldGenerator } from '../systems/world/WorldContract';
import { getChunkTypeDef } from '../systems/world/ChunkTypes';
import { CLIMATES } from '../systems/world/WorldSettings';
import { snowLine } from '../systems/world/TerrainLayers';
import { mercatorZ,MERCATOR_LIMIT } from './AtlasProjection';
import { elevationRampValue,snowValue,LAND,dayProfile,dayFactor,applyDaylight,normalizeSunHours } from './atlas/relief.mjs';
import { contourUnit,bandPosition,terraceShade } from './atlas/contour.mjs';

export type PlanetView = 'globe' | 'projection' | 'mercator';
export interface OverviewStyle {
  rivers?:boolean;
  satellite?:boolean;
  chunkColors?:boolean;
  climate: boolean; relief: boolean; climateFilter: number | null;
  filterKey: string; matches: (type: number) => boolean;
  /** 地势档：无光影/拟真/高度/等高线 */
  reliefMode?:string;
  sunHours?:number;
  sunWorld?:unknown;
  /** 等高距（米），0 或缺省时用档位表 */
  contourUnit?:number;
}
const RAD = Math.PI / 180;
/** 雪色，供高程带混色 */
const SNOW_LIGHT = 246;

/** 全览纹理完整分辨率 */
const OVERVIEW_WIDTH = 720;
const OVERVIEW_HEIGHT = 360;
/** 首屏粗纹理宽度，高度取其一半 */
const OVERVIEW_COARSE_WIDTH = 96;
/** 每帧烘焙时间预算（毫秒） */
const OVERVIEW_BAKE_BUDGET = 5;
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

/** 打包 0xRRGGBB 供逐像素烘焙 */
export function overviewColorValue(type: number, climate: number, temperature: number, elevation: number,
  lakeLevel: number | null, palette: boolean, relief: boolean, chunkColors = false,macro=false): number {
  // 等高线档：按高程分带上色，不再叠加统一光照
  if (relief && !palette && !chunkColors) {
    if (elevation <= 0) return 0x397aa1;
    if (lakeLevel !== null) return 0x3f86ad;
    const at = elevationRampValue(elevation) * 3, amount = snowValue(temperature) / 255;
    const mix = (base: number, peak: number) => Math.round(base + (peak - base) * amount);
    return (mix(LAND[at], SNOW_LIGHT) << 16) | (mix(LAND[at + 1], SNOW_LIGHT) << 8) | mix(LAND[at + 2], SNOW_LIGHT);
  }
  let hex = palette ? CLIMATES[climate].color : getChunkTypeDef(type,macro).mapColor;
  // 连续色：内陆静水不随植被或雪线转色
  if (!palette && !chunkColors) {
    hex = elevation <= 0 ? '#397aa1' : lakeLevel !== null ? '#3f86ad'
      : temperature < 0 || elevation > snowLine(temperature) ? '#e3ebe1'
      : type === 104 || type === 4 ? '#c5b078' : elevation > 768 ? '#8e947a' : '#73965e';
  }
  return parseInt(hex.slice(1), 16);
}

export function overviewColor(info: ChunkInfo, climate: boolean, relief: boolean,chunkColors=false,macro=false): [number, number, number] {
  const value = overviewColorValue(info.type, info.climate, info.temperature, info.elevation, info.lakeLevel, climate, relief, chunkColors,macro);
  return [value >> 16, value >> 8 & 255, value & 255];
}

/** 逐帧烘焙纹理层，样本与样式分离 */
interface BakeTier {
  readonly width: number;
  readonly height: number;
  readonly canvas: HTMLCanvasElement;
  readonly image: ImageData;
  /** 原始样本：类型 / 气候 */
  readonly kinds: Uint16Array;
  /** 原始样本：气温 / 海拔 */
  readonly values: Float32Array;
  readonly lakeLevels:Float32Array;
  sampled: number;
  colored: number;
  /** 已着色行对应的样式标识 */
  key: string;
  readyKey:string;
  readyImage:Uint8ClampedArray|null;
}

function createTier(width: number, height: number): BakeTier {
  const canvas = document.createElement('canvas');
  canvas.width = width; canvas.height = height;
  const pixels = width * height;
  return { width, height, canvas, image: new ImageData(width, height),
    kinds: new Uint16Array(pixels * 2), values: new Float32Array(pixels * 2),lakeLevels:new Float32Array(pixels).fill(NaN), sampled: 0, colored: 0, key: '',readyKey:'',readyImage:null };
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
      tier.readyKey='';tier.readyImage=null;
    }
  }

  /** 样式/地势/等高距决定是否重着色 */
  private styleKey(style: OverviewStyle): string {
    const mode=style.relief?(style.reliefMode??'height'):'none';
    const stamp=mode==='sun'?':'+normalizeSunHours(style.sunHours).toFixed(2):'';
    const unit=mode==='contour'?':'+(style.contourUnit??0):'';
    return [style.climate,style.relief,mode+stamp+unit,style.climateFilter,style.filterKey,
      style.chunkColors,style.rivers!==false,style.satellite].join(',');
  }

  /** 当前样式是否已有完整纹理；分帧生成垫底只用成品 */
  hasTexture(style: OverviewStyle): boolean {
    return this.full.readyKey === this.styleKey(style);
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
      tier.values[i * 2] = info.temperature; tier.values[i * 2 + 1] = info.elevation;tier.lakeLevels[i]=info.lakeLevel??NaN;
    }
  }

  /** 按当前样式着色一行 */
  private colorRow(tier: BakeTier, row: number, style: OverviewStyle): void {
    const kinds = tier.kinds, values = tier.values, data = tier.image.data;
    const mode=style.relief?(style.reliefMode??'height'):'none';
    const size=this.generator.generation.planet.equatorChunks;
    const daylight=mode==='sun'?dayProfile(normalizeSunHours(style.sunHours),style.sunWorld):null;
    // 全览只有 L4 附近，等高距取该档
    const unit=mode==='contour'?(style.contourUnit&&style.contourUnit>0?style.contourUnit:contourUnit(4,this.generator.generation.macroColumns)):0;
    const rgb=[0,0,0];
    for (let x = 0; x < tier.width; x++) {
      const i = row * tier.width + x, type = kinds[i * 2], climate = kinds[i * 2 + 1];
      let value = overviewColorValue(type, climate, values[i * 2], values[i * 2 + 1],Number.isFinite(tier.lakeLevels[i])?tier.lakeLevels[i]:null,
        style.climate, this.modeUsesRamp(mode), style.chunkColors,this.generator.generation.terrainRule==='planet-macro');
      if(style.satellite&&!style.climate&&!style.chunkColors){
        value=this.generator.getSatelliteColor((x+.5)/tier.width*size-size/2-.5,(row+.5)/tier.height*size/2-size/4-.5,false)??value;
      }
      if(!style.climate&&!style.chunkColors&&(values[i*2+1]<=0||Number.isFinite(tier.lakeLevels[i])))value=iceColorValue(value,values[i*2]);
      if(mode==='contour'&&unit>0&&!style.climate&&!style.chunkColors&&values[i*2+1]>0){
        const west=values[this.wrapIndex(tier,x-1,row)*2+1],east=values[this.wrapIndex(tier,x+1,row)*2+1];
        const northRow=Math.max(0,row-1),southRow=Math.min(tier.height-1,row+1);
        const north=values[(northRow*tier.width+x)*2+1],south=values[(southRow*tier.width+x)*2+1];
        const band=bandPosition(values[i*2+1],unit),gradient=Math.hypot(east-west,south-north)/2;
        const distance=gradient>0?Math.min(band,1-band)*unit/gradient:9;
        const factor=terraceShade(band)*(distance<1.4?1-(1-distance/1.4)*.34:1);
        value=(Math.min(255,Math.round((value>>16)*factor))<<16)|(Math.min(255,Math.round((value>>8&255)*factor))<<8)|Math.min(255,Math.round((value&255)*factor));
      }
      // 昼夜：整幅纹理按经纬度受光，海面同样变暗
      if(daylight&&!style.climate&&!style.chunkColors){
        const light=dayFactor(daylight,(90-(row+.5)/tier.height*180)*Math.PI/180,((x+.5)/tier.width*360-180)*Math.PI/180);
        rgb[0]=value>>16;rgb[1]=value>>8&255;rgb[2]=value&255;
        applyDaylight(rgb,light.day,light.dusk);
        value=(Math.round(rgb[0])<<16)|(Math.round(rgb[1])<<8)|Math.round(rgb[2]);
      }
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
  /** 经度循环取列 */
  private wrapIndex(tier:BakeTier,x:number,row:number):number{
    return row*tier.width+((x%tier.width)+tier.width)%tier.width;
  }
  /** 高程色带只用于高度图 */
  private modeUsesRamp(mode:string):boolean{return mode==='height';}

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
    if (progressed && tier.colored === tier.height){
      const rivers=!this.coarseOnly&&!style.climate&&style.rivers!==false?this.generator.getMacroRiverOverview?.():null;
      if(rivers)rasterMacroRivers(tier.image.data,tier.width,tier.height,rivers,!!style.filterKey&&style.matches(5));
      tier.canvas.getContext('2d')!.putImageData(tier.image, 0, 0);
      tier.readyKey=key;tier.readyImage=tier.image.data.slice();
    }
    return tier.colored < tier.height;
  }

  /** 先粗后细推进烘焙，未完成则续画 */
  private advance(style: OverviewStyle, detailed = true): void {
    const key = this.styleKey(style), size = this.generator.generation.planet.equatorChunks;
    const deadline = performance.now() + OVERVIEW_BAKE_BUDGET;
    if (this.step(this.coarse, key, style, size, deadline)) { this.redraw(); return; }
    if (detailed&&!this.coarseOnly&&this.step(this.full, key, style, size, deadline)) this.redraw();
  }

  /** 新样式完成前继续显示成品 */
  private displayTier(style:OverviewStyle):BakeTier|null{
    const key=this.styleKey(style);
    if(this.full.readyKey===key)return this.full;
    if(this.coarse.readyKey===key)return this.coarse;
    return this.full.readyImage?this.full:this.coarse.readyImage?this.coarse:null;
  }

  drawProjection(ctx: CanvasRenderingContext2D, width: number, height: number, cell: number,
    centerX: number, centerZ: number, panX: number, panY: number, style: OverviewStyle, detailed = true): void {
    this.advance(style, detailed);
    const size = this.generator.generation.planet.equatorChunks, worldWidth = size * cell;
    const originX = width / 2 + panX + (-size / 2 - centerX) * cell;
    const originY = height / 2 + panY + (-size / 4 - centerZ) * cell;
    const tier=this.displayTier(style);
    ctx.imageSmoothingEnabled = !style.climate;
    const first = Math.floor(-originX / worldWidth);
    for (let copy = first; originX + copy * worldWidth < width; copy++) {
      const x = originX + copy * worldWidth, y = originY, h = worldWidth / 2;
      if(tier)ctx.drawImage(tier.canvas,x,y,worldWidth,h);
    }
  }

  drawFlat(ctx:CanvasRenderingContext2D,width:number,height:number,cell:number,centerX:number,centerZ:number,panX:number,panY:number,style:OverviewStyle,mercator=true):void {
    if(!mercator){this.drawProjection(ctx,width,height,cell,centerX,centerZ,panX,panY,style);return;}
    this.advance(style);
    const tier=this.displayTier(style);
    if(!tier)return;
    ctx.imageSmoothingEnabled = !style.climate;
    const size=this.generator.generation.planet.equatorChunks,worldWidth=size*cell;
    const originX=width/2+panX+(-size/2-centerX)*cell,originY=height/2+panY-mercatorZ(centerZ,size)*cell;
    const cutoff=MERCATOR_LIMIT*size/(Math.PI*2);
    for(let row=0;row<tier.height;row++){
      const z0=Math.max(-cutoff,(row/tier.height-.5)*size/2),z1=Math.min(cutoff,((row+1)/tier.height-.5)*size/2);
      if(z0>=z1)continue;
      const y0=originY+mercatorZ(z0,size)*cell,y1=originY+mercatorZ(z1,size)*cell;
      if(y1<0||y0>height)continue;
      for(let copy=Math.floor(-originX/worldWidth);originX+copy*worldWidth<width;copy++)ctx.drawImage(tier.canvas,0,row,tier.width,1,originX+copy*worldWidth,y0,worldWidth,y1-y0+.25);
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
    const full = this.full, coarse = this.coarse;
    const tier=this.displayTier(style);
    const useFull=tier===full,useCoarse=tier===coarse;
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
    const rasterKey = `${mapKey}|${tier?.readyKey}|${useFull}|${useCoarse}`;
    if (this.rasterKey !== rasterKey) {
      for (let i = 0; i < mapFull.length; i++) {
        const dst = i * 4, light = mapLight[i];
        if (mapFull[i] < 0) { raster.data[dst + 3] = 0; continue; }
        // 完整纹理整体替换
        let source: Uint8ClampedArray | null = null, src = 0;
        if (useFull) { source = full.readyImage; src = mapFull[i] * 4; }
        else if (useCoarse) { source = coarse.readyImage; src = mapCoarse[i] * 4; }
        for (let c = 0; c < 3; c++) raster.data[dst + c] = source ? Math.min(255, source[src + c] * light) : UNBAKED[c];
        raster.data[dst + 3] = 255;
      }
      this.globeCanvas.getContext('2d')!.putImageData(raster, 0, 0);
      this.rasterKey = rasterKey;
    }
    ctx.imageSmoothingEnabled = !style.climate;
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
