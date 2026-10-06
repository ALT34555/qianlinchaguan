import { WorldGenerator, type ChunkInfo } from '../systems/world/WorldGenerator';
import { getChunkTypeDef } from '../systems/world/ChunkTypes';
import { CLIMATES } from '../systems/world/WorldSettings';
import { snowLine } from '../systems/world/TerrainLayers';

export type PlanetView = 'globe' | 'projection';
export interface OverviewStyle {
  chunkColors?:boolean;
  climate: boolean; relief: boolean; climateFilter: number | null;
  filterKey: string; matches: (info: ChunkInfo) => boolean;
}
const RAD = Math.PI / 180;

/** 正射球体的反投影；背面与球体外没有可点击地块。 */
export function globePoint(u: number, v: number, longitude: number, latitude: number): { longitude: number; latitude: number } | null {
  if (u * u + v * v > 1) return null;
  const depth = Math.sqrt(Math.max(0, 1 - u * u - v * v)), phi = latitude * RAD;
  const lat = Math.asin(Math.max(-1, Math.min(1, v * Math.cos(phi) + depth * Math.sin(phi))));
  const lon = longitude * RAD + Math.atan2(u, depth * Math.cos(phi) - v * Math.sin(phi));
  return { longitude: ((lon / RAD + 540) % 360) - 180, latitude: lat / RAD };
}

export function overviewColor(info: ChunkInfo, climate: boolean, relief: boolean,chunkColors=false): [number, number, number] {
  let hex = climate ? CLIMATES[info.climate].color : getChunkTypeDef(info.type).mapColor;
  // 全球层使用连续的海深/植被/高程色
  if (!climate && !chunkColors) {
    hex = info.elevation <= 0 ? '#397aa1' : info.temperature < 0 || info.elevation > snowLine(info.temperature) ? '#e3ebe1'
      : info.type === 104 || info.type === 4 ? '#c5b078' : info.elevation > 768 ? '#8e947a' : '#73965e';
  }
  const value = parseInt(hex.slice(1), 16);
  const light = relief ? info.elevation <= 0 ? 1 - Math.min(.4, -info.elevation / 280)
    : 1 + Math.min(.2, info.elevation / 4000) : 1;
  return [value >> 16, value >> 8 & 255, value & 255].map(v => Math.min(255, Math.round(v * light))) as [number, number, number];
}

export class PlanetAtlasLayer {
  private texture: ImageData | null = null;
  private textureKey = '';
  private textureCanvas = document.createElement('canvas');
  private globeCanvas = document.createElement('canvas');
  constructor(private generator: WorldGenerator) {}

  setGenerator(generator: WorldGenerator): void { this.generator = generator; this.texture = null; }

  private getTexture(style: OverviewStyle): ImageData {
    const key = `${style.climate},${style.relief},${style.climateFilter},${style.filterKey},${style.chunkColors}`;
    if (this.texture && key === this.textureKey) return this.texture;
    const w = 720, h = 360, data = new ImageData(w, h), size = this.generator.generation.planet.equatorChunks;
    for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
      const info = this.generator.getOverviewInfo((x + .5) / w * size - size / 2 - .5,
        (y + .5) / h * size / 2 - size / 4 - .5);
      const i = (y * w + x) * 4;
      let color = overviewColor(info, style.climate, style.relief,style.chunkColors);
      if (style.climate && style.climateFilter !== null && info.climate !== style.climateFilter) color = color.map(c => Math.round(c * .35)) as [number, number, number];
      if (style.filterKey && style.matches(info)) color = [Math.min(255, color[0] * .4 + 160), color[1] * .4, color[2] * .4];
      data.data.set([...color, 255], i);
    }
    this.texture = data; this.textureKey = key;
    this.textureCanvas.width = w; this.textureCanvas.height = h;
    this.textureCanvas.getContext('2d')!.putImageData(data, 0, 0);
    return data;
  }

  drawProjection(ctx: CanvasRenderingContext2D, width: number, height: number, cell: number,
    centerX: number, centerZ: number, panX: number, panY: number, style: OverviewStyle): void {
    this.getTexture(style);
    const size = this.generator.generation.planet.equatorChunks, worldWidth = size * cell;
    const originX = width / 2 + panX + (-size / 2 - centerX) * cell;
    const originY = height / 2 + panY + (-size / 4 - centerZ) * cell;
    ctx.imageSmoothingEnabled = true;
    const first = Math.floor(-originX / worldWidth);
    for (let copy = first; originX + copy * worldWidth < width; copy++) {
      ctx.drawImage(this.textureCanvas, originX + copy * worldWidth, originY, worldWidth, worldWidth / 2);
    }
  }

  drawGlobe(ctx: CanvasRenderingContext2D, width: number, height: number, radius: number,
    longitude: number, latitude: number, style: OverviewStyle): void {
    const texture = this.getTexture(style);
    const stride = 2, w = Math.ceil(width / stride), h = Math.ceil(height / stride);
    this.globeCanvas.width = w; this.globeCanvas.height = h;
    const raster = new ImageData(w, h);
    for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
      const u = (x * stride - width / 2) / radius, v = (height / 2 - y * stride) / radius;
      const coord = globePoint(u, v, longitude, latitude);
      if (!coord) continue;
      const tx = Math.floor((coord.longitude + 180) / 360 * texture.width) % texture.width;
      const ty = Math.max(0, Math.min(texture.height - 1, Math.floor((90 - coord.latitude) / 180 * texture.height)));
      const src = (ty * texture.width + tx) * 4, dst = (y * w + x) * 4;
      const depth = Math.sqrt(Math.max(0, 1 - u * u - v * v));
      const light = .62 + .38 * depth - .12 * u + .06 * v;
      for (let c = 0; c < 3; c++) raster.data[dst + c] = Math.min(255, texture.data[src + c] * light);
      raster.data[dst + 3] = 255;
    }
    this.globeCanvas.getContext('2d')!.putImageData(raster, 0, 0);
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
