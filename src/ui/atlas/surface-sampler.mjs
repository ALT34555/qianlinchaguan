import {TILE, TAU, clamp} from './tiles.mjs';
import {sampleSurface, macroAt, macroAtMany} from '../../systems/world/planet-macro/backend.mjs';
import {sampleIslandCoast} from '../../systems/world/planet-macro/island-coasts.mjs';
import {sampleCoast,coastTint} from '../../systems/world/planet-macro/coast-templates.mjs';
import {mainRiverWidth} from '../../systems/world/planet-macro/river-continuity.mjs';
import {macroCurveGeometry as macroCurve} from '../../systems/world/planet-macro/source-rivers.mjs';
import {continuousSatelliteColor, continuousBiomeColor} from '../../systems/world/planet-macro/satellite.mjs';
import {coverRGB} from '../../systems/world/planet-macro/surface-cover.mjs';
import {macroRiverThreshold} from '../../systems/world/planet-macro/river-dimensions.mjs';
import {iceCoverage, ICE_RGB} from './ice-cover.mjs';
import {flatColor, dayFactor, daylightPixel, elevationRampValue, snowValue, depthValue, CLIMATE_RGB, SEA_RGB, temperatureRGB, DEPTH_SEA, DEPTH_LAKE} from './relief.mjs';
import {bandPosition, terraceShade, MIN_SEPARATION_PX} from './contour.mjs';
import {landTypeId} from '../../systems/world/planet-macro/chunk-metadata.mjs';

const waterColors = [[0, 0, 0], [34, 79, 103], [51, 113, 139], [64, 139, 166], [64, 139, 166]];
const riverWidths = [1.8, 2.4, 3.2, 4.2, 5.4];
const OVERVIEW_FIELDS = ['height', 'temperature', 'rainfall', 'evaporation'];
// 正式地表采样启用层级，之前统一用连续基底
const SURFACE_LEVEL = 18;
// L16起按米/像素混入局部岸色
const LOCAL_COVER_METERS = 8, LOCAL_COVER_STEP = 16;

// 宏观三位编码：仅区块信息与语义，不参与底图着色
export function macroCodeAt(w,cell) {
  const span=w.circumference/w.columns,x=cell%w.columns,y=Math.floor(cell/w.columns),height=w.height[cell];
  let code;
    if (height <= 0 || w.water[cell] === 1) {
      code = 32;
    } else {
      const at = (a, b) => {
        let col = a, row = b;
        if (row < 0) { row = -row - 1; col += w.columns / 2; }
        if (row >= w.rows) { row = w.rows * 2 - row - 1; col += w.columns / 2; }
        return row * w.columns + ((col % w.columns) + w.columns) % w.columns;
      };
      const left = w.height[at(x - 1, y)], right = w.height[at(x + 1, y)];
      const up = w.height[at(x, y - 1)], down = w.height[at(x, y + 1)];
      const cos = Math.max(0.01, Math.cos((0.5 - (y + 0.5) / w.rows) * Math.PI));
      const sx = (right - left) / (2 * span * cos), sz = (down - up) / (2 * span);
      const slope = Math.max(Math.abs(sx), Math.abs(sz));
      let low = height, high = height, total = 0;
      for (let dz = -1; dz <= 1; dz++) {
        for (let dx = -1; dx <= 1; dx++) {
          const val = w.height[at(x + dx, y + dz)];
          if (val < low) low = val;
          if (val > high) high = val;
          total += val;
        }
      }
      const relief = Math.min(high - low, slope * 64 * Math.SQRT2);
      const relative = (height - (total - height) / 8) * (32 / span) ** 2;
      code = landTypeId(w.temperature[cell], w.rainfall[cell], w.evaporation[cell], relief, slope, relative);
    }
  return code;
}

export function riverStrokePixels(flow, threshold, z) {
  if (z < 7 || z > 13) return 0;
  const ratio = flow / Math.max(1e-9, threshold);
  const tier = ratio < 1 ? 0 : ratio < 3 ? 1 : ratio < 10 ? 2 : ratio < 30 ? 3 : 4;
  return riverWidths[tier] * (1 + (z - 7) * 0.12);
}

export function sampleOverview(w, wx, wz, skipWaterSlope = false) {
  const span = w.circumference / w.columns;
  const x = Math.floor((wx / w.circumference + 0.5) * w.columns);
  const y = clamp(Math.floor((wz / w.circumference + 0.25) * w.columns), 0, w.rows - 1);
  const i = y * w.columns + ((x % w.columns) + w.columns) % w.columns;
  const fields = macroAtMany(w, wx, wz, OVERVIEW_FIELDS);
  const shore=sampleIslandCoast(w,wx,wz,fields.height)??sampleCoast(w,wx,wz,fields.height,fields.height);
  const height = shore?.height??fields.height;
  const kind = w.water[i] === 2 ? 2 : height <= 0 ? 1 : 0;
  const cos = Math.max(0.01, Math.cos(wz / w.circumference * TAU));
  return {
    height,
    kind,
    temperature: fields.temperature+(shore&&height>0?(Math.max(0,fields.height)-height)*.0065:0),
    macro: {
      height,
      rainfall: fields.rainfall,
      evaporation: fields.evaporation,
      eastSlope: skipWaterSlope && kind ? 0 : (macroAt(w, wx + span, wz) - macroAt(w, wx - span, wz)) / (2 * span * cos),
      southSlope: skipWaterSlope && kind ? 0 : (macroAt(w, wx, wz + span) - macroAt(w, wx, wz - span)) / (2 * span),
    },
  };
}

export function overviewRiver(w, cell) {
  const receiver = w.receiver[cell];
  if (receiver < 0 || !mainRiverWidth(w,cell)) return null;
  const point = i => ({
    wx: ((i % w.columns + 0.5) / w.columns - 0.5) * w.circumference,
    wz: (Math.floor(i / w.columns) + 0.5) / w.columns * w.circumference - w.circumference / 4,
  });
  return {receiver, points: [point(cell), point(receiver)], halfWidth: mainRiverWidth(w,cell)};
}

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

const satScratch = [0, 0, 0], bioScratch = [0, 0, 0];
const smoothPole = v => { const t = Math.max(0, Math.min(1, v)); return t * t * (3 - 2 * t); };
const mixVal = (a, b, t) => a + (b - a) * t;
const cellIdxAt = (ix, iy, W, H) => {
  iy = ((iy % (H * 2)) + H * 2) % (H * 2);
  if (iy >= H) { iy = H * 2 - iy - 1; ix += W >> 1; }
  return iy * W + ((ix % W) + W) % W;
};

// 本瓦片坡度
export function slopeAt(fields, x, y, dx, dy) {
  const x0 = clamp(x - dx, 0, TILE - 1), x1 = clamp(x + dx, 0, TILE - 1);
  const y0 = clamp(y - dy, 0, TILE - 1), y1 = clamp(y + dy, 0, TILE - 1);
  const a = fields.heights[y0 * TILE + x0], b = fields.heights[y1 * TILE + x1];
  if (a < -9000 || b < -9000) return 0;
  return (b - a) / Math.max(1, (x1 - x0) + (y1 - y0));
}

// 等高带明度计算
export function contourShade(fields, i, unit) {
  const band = bandPosition(fields.heights[i], unit);
  if (band < 0) return 1;
  const x = i % TILE, y = (i - x) / TILE;
  const gx = Math.abs(slopeAt(fields, x, y, 1, 0)), gy = Math.abs(slopeAt(fields, x, y, 0, 1));
  const gradient = Math.hypot(gx, gy);
  if (gradient > 0 && unit / gradient < MIN_SEPARATION_PX) return 1;
  let shade = terraceShade(band);
  if (gradient > 0) {
    const distance = Math.min(band, 1 - band) * unit / gradient;
    if (distance < 1.4) shade *= 1 - (1 - distance / 1.4) * 0.34;
  }
  return shade;
}

// 地表采样服务
export class SurfaceSampler {
  constructor(world, budget = 48 * 2 ** 20) {
    this.world = world;
    this.budget = budget;
    this.bytes = 0;
    this.cache = new Map();
    this.samples = 0;
    this.hits = 0;
    this.colorSampleCount = 0;
    this.shorelineGeometryCount = 0;
    this.dataLayerCounts = { height: 0, temperature: 0, climate: 0, ice: 0, depth: 0, contour: 0 };
    this.grid4mCache = new Map();
    this.gridBytes=0;
    this.riverThreshold = macroRiverThreshold(
      world.options.equatorChunks / world.columns,
      world.options.precipitation
    );
    this.rivers = [];
    this.riversByRow = Array.from({length: world.rows}, () => []);
    for (let i = 0; i < world.height.length; i++) {
      if (mainRiverWidth(world,i) > 0 && world.receiver[i] >= 0) {
        this.rivers.push(i);
        this.riversByRow[Math.floor(i / world.columns)].push(i);
      }
    }
  }

  cacheStats() {
    return {fieldBytes:this.bytes,gridBytes:this.gridBytes,totalBytes:this.bytes+this.gridBytes,
      fieldLimit:this.budget,gridEntries:this.grid4mCache.size};
  }
  clear(){this.lastColor=null;this.cache.clear();this.grid4mCache.clear();this.bytes=0;this.gridBytes=0;}
  clearFields(){this.cache.clear();this.bytes=0;}
  trim() {
    while(this.cache.size&&this.cacheStats().totalBytes>this.budget){const k=this.cache.keys().next().value;this.bytes-=this.cache.get(k).bytes;this.cache.delete(k);}
    while(this.grid4mCache.size&&this.cacheStats().totalBytes>this.budget){const k=this.grid4mCache.keys().next().value;this.gridBytes-=this.grid4mCache.get(k).bytes;this.grid4mCache.delete(k);}
  }
  retain(key,data) {
    const bytes=Object.values(data).reduce((sum,v)=>sum+(ArrayBuffer.isView(v)?v.byteLength:0),0);
    this.bytes-=this.cache.get(key)?.bytes??0;this.cache.delete(key);data.bytes=bytes;
    if(bytes<=this.budget){this.cache.set(key,data);this.bytes+=bytes;this.trim();}
    return data;
  }

  // 世界网格颜色采样
  sample4mColorSync(wx,wz,step=4) {
    const last=this.lastColor;
    if(last&&last.step===step&&wx>=last.minX&&wx<last.maxX&&wz>=last.minZ&&wz<last.maxZ){this.hits++;return last.entry;}
    const w=this.world,C=w.circumference,wrap=x=>((x+C/2)%C+C)%C-C/2;
    const z=Math.max(-C/4,Math.min(C/4-Number.EPSILON,wz)),gz=Math.floor(z/step);
    const repWz=Math.max(-C/4+.5,Math.min(C/4-.5,(gz+.5)*step));
    const columns=Math.max(1,Math.floor(C*Math.max(0,Math.cos(repWz/C*TAU))/step));
    const sx=C/columns,gx=Math.floor(wrap(wx)/sx),wrapped=((gx%columns)+columns)%columns;
    const key=`${step}:${wrapped},${gz}`;
    const remember=entry=>{this.lastColor={step,minX:gx*sx,maxX:(gx+1)*sx,minZ:gz*step,maxZ:(gz+1)*step,entry};return entry;};
    const cached=this.grid4mCache.get(key);
    if(cached){this.hits++;this.grid4mCache.delete(key);this.grid4mCache.set(key,cached);return remember(cached);}
    const canonicalIndex=wrapped<Math.ceil(columns/2)?wrapped:wrapped-columns;
    const repWx=wrap((canonicalIndex+.5)*sx),point=sampleSurface(w,repWx,repWz,true,false,false,false);
    this.colorSampleCount++;this.samples++;
    const rain=point.macro?.rainfall??0,evap=point.macro?.evaporation??0;
    const satellite=point.surfaceColor===undefined?continuousSatelliteColor(point.temperature,rain,evap,0,0,0):coverRGB(point);
    const biome=continuousBiomeColor(point.temperature,rain,evap);
    const values=new Float64Array([point.kind,point.height,point.temperature,repWx,repWz,sx,step]);
    const entry={values,satelliteRGB:Uint8ClampedArray.from(satellite),biomeRGB:Uint8ClampedArray.from(biome),
      get kind(){return values[0];},get height(){return values[1];},get temperature(){return values[2];}};
    entry.bytes=values.byteLength+entry.satelliteRGB.byteLength+entry.biomeRGB.byteLength+key.length*2;
    this.grid4mCache.set(key,entry);this.gridBytes+=entry.bytes;
    const limit=Math.min(2*2**20,this.budget/4);
    while(this.grid4mCache.size&&(this.gridBytes>limit||this.grid4mCache.size>32768)){const k=this.grid4mCache.keys().next().value;this.gridBytes-=this.grid4mCache.get(k).bytes;this.grid4mCache.delete(k);}
    this.trim();return remember(entry);
  }

  async sample4mColor(wx,wz,palette='satellite',cancelled=()=>false,waitForShore,step=4) {
    if(cancelled())return null;
    return this.sample4mColorSync(wx,wz,step);
  }

  // 连续基底：全层级共用字段，低层双线性导出
  async fillContinuousBase(tile, data, stride, cancelled, {independentWater = false, localCover = 0} = {}) {
    const w = this.world, n = 2 ** tile.z, C = w.circumference, W = w.columns, H = w.rows;
    const cellSpan = C / W;
    const H_ = w.height, T_ = w.temperature, R_ = w.rainfall, E_ = w.evaporation, poles = w.poles;
    const ph = poles.height, pt = poles.temperature, pr = poles.rainfall, pe = poles.evaporation;
    const lakeFromMacro = tile.z < 7;
    let deadline = performance.now() + 8;
    for (let row = 0; row < TILE; row += stride) {
      if (performance.now() >= deadline) {
        await pause();
        if (cancelled()) return null;
        deadline = performance.now() + 8;
      }
      const wz = (tile.y + (row + 0.5) / TILE) / n * C - C / 4;
      const cellY = clamp(Math.floor((wz / C + 0.25) * W), 0, H - 1);
      const y = (wz / C + 0.25) * W - 0.5, iy = Math.floor(y), ty = y - iy;
      const cos = Math.max(0.01, Math.cos(wz / C * TAU));
      const poleDist = Math.min(Math.abs(wz + C / 4), Math.abs(wz - C / 4));
      const pole = poleDist < cellSpan ? smoothPole(poleDist / cellSpan) : -1, poleId = wz < 0 ? 0 : 1;
      const wzS1 = wz + cellSpan, wzS0 = wz - cellSpan;
      const yS1 = (wzS1 / C + 0.25) * W - 0.5, iyS1 = Math.floor(yS1), tyS1 = yS1 - iyS1;
      const yS0 = (wzS0 / C + 0.25) * W - 0.5, iyS0 = Math.floor(yS0), tyS0 = yS0 - iyS0;
      const pdS1 = Math.min(Math.abs(wzS1 + C / 4), Math.abs(wzS1 - C / 4)), poleS1 = pdS1 < cellSpan ? smoothPole(pdS1 / cellSpan) : -1, pidS1 = wzS1 < 0 ? 0 : 1;
      const pdS0 = Math.min(Math.abs(wzS0 + C / 4), Math.abs(wzS0 - C / 4)), poleS0 = pdS0 < cellSpan ? smoothPole(pdS0 / cellSpan) : -1, pidS0 = wzS0 < 0 ? 0 : 1;
      let prevIx = -999999999, prevSlopeIx = -999999999;
      let a = 0, b = 0, c = 0, d = 0;
      let aE1 = 0, bE1 = 0, cE1 = 0, dE1 = 0, aE0 = 0, bE0 = 0, cE0 = 0, dE0 = 0;
      let aS1 = 0, bS1 = 0, cS1 = 0, dS1 = 0, aS0 = 0, bS0 = 0, cS0 = 0, dS0 = 0;

      for (let col = 0; col < TILE; col += stride) {
        const wx = (tile.x + (col + 0.5) / TILE) / n * C - C / 2;
        const x = (wx / C + 0.5) * W - 0.5, ix = Math.floor(x), tx = x - ix;
        if (ix !== prevIx) {
          prevIx = ix;
          a = cellIdxAt(ix, iy, W, H); b = cellIdxAt(ix + 1, iy, W, H);
          c = cellIdxAt(ix, iy + 1, W, H); d = cellIdxAt(ix + 1, iy + 1, W, H);
        }
        let height = mixVal(mixVal(H_[a], H_[b], tx), mixVal(H_[c], H_[d], tx), ty);
        let temp = mixVal(mixVal(T_[a], T_[b], tx), mixVal(T_[c], T_[d], tx), ty);
        let rain = mixVal(mixVal(R_[a], R_[b], tx), mixVal(R_[c], R_[d], tx), ty);
        let evap = mixVal(mixVal(E_[a], E_[b], tx), mixVal(E_[c], E_[d], tx), ty);
        if (pole >= 0) {
          height = mixVal(ph[poleId], height, pole);
          temp = mixVal(pt[poleId], temp, pole);
          rain = mixVal(pr[poleId], rain, pole);
          evap = mixVal(pe[poleId], evap, pole);
        }
        const macroHeight=height,island=sampleIslandCoast(w,wx,wz,height),coast=island?null:sampleCoast(w,wx,wz,height,macroHeight),shore=island??coast;
        height=shore?.height??height;
        if(shore&&height>0)temp+=(Math.max(0,macroHeight)-height)*.0065;
        const cellX = Math.floor((wx / C + 0.5) * W);
        const cellIdx = cellY * W + ((cellX % W) + W) % W;
        const rawKind = lakeFromMacro && w.water[cellIdx] === 2 ? 2 : height <= 0 ? 1 : 0;
        let eastSlope = 0, southSlope = 0;
        if (!rawKind) {
          const xE1 = ((wx + cellSpan) / C + 0.5) * W - 0.5, ixE1 = Math.floor(xE1), txE1 = xE1 - ixE1;
          const xE0 = ((wx - cellSpan) / C + 0.5) * W - 0.5, ixE0 = Math.floor(xE0), txE0 = xE0 - ixE0;
          if (ix !== prevSlopeIx && ixE1 === ix + 1 && ixE0 === ix - 1) {
            prevSlopeIx = ix;
            aE1 = b; bE1 = cellIdxAt(ix + 2, iy, W, H); cE1 = d; dE1 = cellIdxAt(ix + 2, iy + 1, W, H);
            aE0 = cellIdxAt(ix - 1, iy, W, H); bE0 = a; cE0 = cellIdxAt(ix - 1, iy + 1, W, H); dE0 = c;
            aS1 = cellIdxAt(ix, iyS1, W, H); bS1 = cellIdxAt(ix + 1, iyS1, W, H); cS1 = cellIdxAt(ix, iyS1 + 1, W, H); dS1 = cellIdxAt(ix + 1, iyS1 + 1, W, H);
            aS0 = cellIdxAt(ix, iyS0, W, H); bS0 = cellIdxAt(ix + 1, iyS0, W, H); cS0 = cellIdxAt(ix, iyS0 + 1, W, H); dS0 = cellIdxAt(ix + 1, iyS0 + 1, W, H);
          } else if (ix !== prevSlopeIx) {
            aE1 = cellIdxAt(ixE1, iy, W, H); bE1 = cellIdxAt(ixE1 + 1, iy, W, H); cE1 = cellIdxAt(ixE1, iy + 1, W, H); dE1 = cellIdxAt(ixE1 + 1, iy + 1, W, H);
            aE0 = cellIdxAt(ixE0, iy, W, H); bE0 = cellIdxAt(ixE0 + 1, iy, W, H); cE0 = cellIdxAt(ixE0, iy + 1, W, H); dE0 = cellIdxAt(ixE0 + 1, iy + 1, W, H);
            aS1 = cellIdxAt(ix, iyS1, W, H); bS1 = cellIdxAt(ix + 1, iyS1, W, H); cS1 = cellIdxAt(ix, iyS1 + 1, W, H); dS1 = cellIdxAt(ix + 1, iyS1 + 1, W, H);
            aS0 = cellIdxAt(ix, iyS0, W, H); bS0 = cellIdxAt(ix + 1, iyS0, W, H); cS0 = cellIdxAt(ix, iyS0 + 1, W, H); dS0 = cellIdxAt(ix + 1, iyS0 + 1, W, H);
          }
          let hE1 = mixVal(mixVal(H_[aE1], H_[bE1], txE1), mixVal(H_[cE1], H_[dE1], txE1), ty);
          let hE0 = mixVal(mixVal(H_[aE0], H_[bE0], txE0), mixVal(H_[cE0], H_[dE0], txE0), ty);
          if (pole >= 0) { hE1 = mixVal(ph[poleId], hE1, pole); hE0 = mixVal(ph[poleId], hE0, pole); }
          let hS1 = mixVal(mixVal(H_[aS1], H_[bS1], tx), mixVal(H_[cS1], H_[dS1], tx), tyS1);
          let hS0 = mixVal(mixVal(H_[aS0], H_[bS0], tx), mixVal(H_[cS0], H_[dS0], tx), tyS0);
          if (poleS1 >= 0) hS1 = mixVal(ph[pidS1], hS1, poleS1);
          if (poleS0 >= 0) hS0 = mixVal(ph[pidS0], hS0, poleS0);
          eastSlope = (hE1 - hE0) / (2 * cellSpan * cos);
          southSlope = (hS1 - hS0) / (2 * cellSpan);
        }
        // 湖泊由全层级模板遮罩着色
        const kind = rawKind === 2 ? 0 : rawKind;
        const slope = Math.max(Math.abs(eastSlope), Math.abs(southSlope));
        const paintSea = kind === 1 && !independentWater;
        const satellite = paintSea ? SEA_RGB : continuousSatelliteColor(temp, rain, evap, slope * 64 * Math.SQRT2, slope, 0, satScratch);
        if(coast&&!kind&&temp>1){const rgb=coastTint(coast,satellite);for(let k=0;k<3;k++)satellite[k]=rgb[k];}
        const biome = paintSea ? SEA_RGB : continuousBiomeColor(temp, rain, evap, bioScratch);
        const snow = snowValue(temp);
        const depth = kind === 1 ? depthValue(0, height, DEPTH_SEA) : kind === 2 ? depthValue(w.spill ? w.spill[cellIdx] : 0, height, DEPTH_LAKE) : 0;
        const climate = temp >= 26 ? 0 : temp >= 18 ? 1 : temp >= 10 ? 2 : temp >= 0 ? 3 : 4;
        const temperature = Math.max(-128, Math.min(127, Math.round(temp)));
        const iceVal = Math.round(iceCoverage(temp) * 255);
        if (localCover > 0 && !kind) {
          const entry = this.sample4mColorSync(wx, wz, LOCAL_COVER_STEP);
          if (entry && !entry.kind) {
            for (let k = 0; k < 3; k++) satellite[k] += (entry.satelliteRGB[k] - satellite[k]) * localCover;
            for (let k = 0; k < 3; k++) biome[k] += (entry.biomeRGB[k] - biome[k]) * localCover;
          }
        }
        this.samples++;
        this.colorSampleCount++;
        for (let dy = 0; dy < stride; dy++) {
          for (let dx = 0; dx < stride; dx++) {
            const py = row + dy, px = col + dx;
            if (py >= TILE || px >= TILE) continue;
            const i = py * TILE + px;
            data.heights[i] = height;
            data.kinds[i] = kind;
            data.snow[i] = snow;
            data.depth[i] = depth;
            data.climate[i] = climate;
            data.temperature[i] = temperature;
            data.ice[i] = iceVal;
            const i3 = i * 3;
            data.satellite[i3] = satellite[0]; data.satellite[i3 + 1] = satellite[1]; data.satellite[i3 + 2] = satellite[2];
            data.biome[i3] = biome[0]; data.biome[i3 + 1] = biome[1]; data.biome[i3 + 2] = biome[2];
          }
        }
      }
    }
    return data;
  }

  // 高层颜色改为正式采样，基底高程保持一致
  async fillSurfaceColors(tile, data, stride, cancelled) {
    const w = this.world, n = 2 ** tile.z, step = Math.max(LOCAL_COVER_STEP, 2 ** Math.floor(Math.log2(Math.max(LOCAL_COVER_STEP, data.meters))));
    let deadline = performance.now() + 8;
    for (let row = 0; row < TILE; row += stride) {
      if (performance.now() >= deadline) {
        await pause();
        if (cancelled()) return null;
        deadline = performance.now() + 8;
      }
      const wz = (tile.y + (row + 0.5) / TILE) / n * w.circumference - w.circumference / 4;
      for (let col = 0; col < TILE; col += stride) {
        const wx = (tile.x + (col + 0.5) / TILE) / n * w.circumference - w.circumference / 2;
        const entry = this.sample4mColorSync(wx, wz, step);
        for (let dy = 0; dy < stride; dy++) {
          for (let dx = 0; dx < stride; dx++) {
            const py = row + dy, px = col + dx;
            if (py >= TILE || px >= TILE) continue;
            const i3 = (py * TILE + px) * 3;
            data.satellite[i3] = entry.satelliteRGB[0]; data.satellite[i3 + 1] = entry.satelliteRGB[1]; data.satellite[i3 + 2] = entry.satelliteRGB[2];
            data.biome[i3] = entry.biomeRGB[0]; data.biome[i3 + 1] = entry.biomeRGB[1]; data.biome[i3 + 2] = entry.biomeRGB[2];
          }
        }
      }
    }
    return data;
  }

  // 缓存键包含世界特征、瓦片、焦点与步长
  cacheKeyFor(tile, focusId, stride) {
    const base = tile.z >= 13 && focusId !== null ? `${tile.key}/lake:${focusId}` : tile.key;
    return stride > 1 ? `${base}/s${stride}` : base;
  }

  async getFields(tile, focusId, cancelled = () => false, stride = 1, waitForShore, options = {}) {
    const logicalLevel = options.logicalLevel ?? tile.z;
    const cacheKey = this.cacheKeyFor(tile, focusId, stride);
    const fullKey = this.cacheKeyFor(tile, focusId, 1);
    const previous = this.cache.get(cacheKey) ?? (stride > 1 ? this.cache.get(fullKey) : undefined);

    if (previous) {
      const key = this.cache.has(cacheKey) ? cacheKey : fullKey;
      this.cache.delete(key);
      this.cache.set(key, previous);
      this.hits++;
      return previous;
    }

    const w = this.world, span = w.circumference / 2 ** tile.z, meters = span / TILE, count = TILE * TILE;
    const data = {
      heights: new Float32Array(count),
      kinds: new Uint8Array(count),
      snow: new Uint8Array(count),
      depth: new Uint8Array(count),
      rivers: new Uint8Array(count),
      climate: new Uint8Array(count),
      temperature: new Int8Array(count),
      ice: new Uint8Array(count),
      satellite: new Uint8ClampedArray(count * 3),
      biome: new Uint8ClampedArray(count * 3),
      meters,
    };

    const layer = options.layer;
    if (layer === 'height' || layer === 'contour') this.dataLayerCounts.height++;
    else if (layer === 'temperature') this.dataLayerCounts.temperature++;
    else if (layer === 'climate') this.dataLayerCounts.climate++;
    else if (layer === 'ice') this.dataLayerCounts.ice++;
    else if (layer === 'depth') this.dataLayerCounts.depth++;

    // 米/像素决定何时混入局部岸色，层级只是默认策略
    const localCover = meters < LOCAL_COVER_METERS ? clamp((LOCAL_COVER_METERS - meters) / 6, 0, 1) : 0;
    const base = await this.fillContinuousBase(tile, data, stride, cancelled, {
      independentWater: !!options.independentWater,
      localCover,
    });
    if (!base) return null;
    if (logicalLevel >= SURFACE_LEVEL) {
      if (!await this.fillSurfaceColors(tile, data, stride, cancelled)) return null;
    }
    await this.drawRivers(data, tile, cancelled);
    if (cancelled()) return null;
    return this.retain(cacheKey, data);
  }

  async drawRivers(fields, tile, cancelled) {
    const w = this.world, n = 2 ** tile.z, span = w.circumference / n, meters = span / TILE, columns = w.columns / n;
    const centerX = (tile.x + 0.5) / n * w.circumference - w.circumference / 2, left = centerX - span / 2, top = tile.y / n * w.circumference - w.circumference / 4;
    const delta = x => x - Math.round(x / w.circumference) * w.circumference;
    const row0 = Math.max(0, Math.floor(tile.y * columns - 2)), row1 = Math.min(w.rows - 1, Math.ceil((tile.y + 1) * columns + 2));
    const maxDx = span / 2 + w.circumference / w.columns * 2;
    const coarse = meters > w.circumference / w.columns / 16;
    let deadline = performance.now() + 8;
    for (let row = row0; row <= row1; row++) {
      const rowRivers = this.riversByRow[row];
      for (let rIdx = 0; rIdx < rowRivers.length; rIdx++) {
        const cell = rowRivers[rIdx];
        const col = cell % w.columns, cx = (col + 0.5) / w.columns * w.circumference - w.circumference / 2;
        if (Math.abs(delta(cx - centerX)) > maxDx) continue;
        if (performance.now() >= deadline) {
          await pause();
          if (cancelled()) return;
          deadline = performance.now() + 8;
        }
        // 位图河道只画几何折线，湖岸求解放到矢量层
        let curve;
        try {
          curve = coarse ? overviewRiver(w, cell) : macroCurve(w, cell);
        } catch (error) {
          if (!error?.promise) throw error;
          curve = macroCurve(w, cell, true);
        }
        if (!curve) continue;
        for (let j = 1; j < curve.points.length; j++) {
          const a = curve.points[j - 1], b = curve.points[j];
          const ax = (centerX + delta(a.wx - centerX) - left) / meters, az = (a.wz - top) / meters;
          const bx = ax + delta(b.wx - a.wx) / meters, bz = (b.wz - top) / meters;
          const cos = Math.max(0.01, Math.cos((a.wz + b.wz) / 2 / w.circumference * TAU));
          const t = (j - 0.5) / (curve.points.length - 1);
          const flow = w.flow[cell] + (w.flow[curve.receiver] - w.flow[cell]) * t;
          const emphasis = riverStrokePixels(flow, this.riverThreshold, tile.z) / 3;
          const width = a.halfWidth !== undefined ? (a.halfWidth + b.halfWidth) / 2 : curve.halfWidth;
          const rx = Math.max(0.55, emphasis, width / meters / cos), ry = Math.max(0.55, emphasis, width / meters);
          const margin = tile.z >= 7 && tile.z <= 13 ? 1.5 : 1;
          const x0 = clamp(Math.floor(Math.min(ax, bx) - rx * margin - 1), 0, TILE - 1);
          const x1 = clamp(Math.ceil(Math.max(ax, bx) + rx * margin + 1), 0, TILE - 1);
          const y0 = clamp(Math.floor(Math.min(az, bz) - ry * margin - 1), 0, TILE - 1);
          const y1 = clamp(Math.ceil(Math.max(az, bz) + ry * margin + 1), 0, TILE - 1);
          const dx = (bx - ax) / rx, dy = (bz - az) / ry, length = dx * dx + dy * dy;
          for (let y = y0; y <= y1; y++) {
            for (let x = x0; x <= x1; x++) {
              const px = (x + 0.5 - ax) / rx, py = (y + 0.5 - az) / ry;
              const tParam = clamp((px * dx + py * dy) / Math.max(1e-12, length), 0, 1);
              const d = Math.hypot(px - dx * tParam, py - dy * tParam);
              if (d > 1.5) continue;
              const i = y * TILE + x;
              const alpha = clamp((1.5 - d) * Math.min(rx, ry), 0, 1) * 255;
              fields.rivers[i] = Math.max(fields.rivers[i], alpha);
            }
          }
        }
      }
    }
  }
}

// 图层着色器派生函数集合
export function shadeLayerPixels(layer, fields, options) {
  const pixels = new Uint8ClampedArray(TILE * TILE * 4);
  const out = [0, 0, 0];
  const {unit = 0, daylight = null, world = null, tile = null, mask = null, satellite = true,independentWater=false,waterMaskBytes=false,iceMask=null} = options;
  const base = satellite ? fields.satellite : fields.biome;

  for (let i = TILE * TILE - 1; i >= 0; i--) {
    const wet=mask?(waterMaskBytes?mask[i]/255:mask[i]):0;
    const rawKind=fields.kinds[i];
    const kind=independentWater?(wet>=.5?2:0):mask&&rawKind!==1?(wet>=.5?2:rawKind===2?0:rawKind):rawKind,snow=fields.snow[i];

    if (layer === 'contour') {
      const shade = kind || !(unit > 0) ? 1 : contourShade(fields, i, unit);
      pixels[i * 4] = pixels[i * 4 + 1] = pixels[i * 4 + 2] = Math.round(clamp(shade, 0, 1) * 255);
      pixels[i * 4 + 3] = 255;
      continue;
    }
    if (layer === 'daylight') {
      const x = i % TILE, y = (i - x) / TILE;
      const spanM = world.circumference / 2 ** tile.z;
      const leftWx = tile.x / 2 ** tile.z * world.circumference - world.circumference / 2;
      const topWz = tile.y / 2 ** tile.z * world.circumference - world.circumference / 4;
      const light = dayFactor(daylight, -(topWz + (y + 0.5) / TILE * spanM) / world.circumference * TAU,
        (leftWx + (x + 0.5) / TILE * spanM) / world.circumference * TAU);
      const rgb = daylightPixel(light);
      pixels[i * 4] = rgb[0];
      pixels[i * 4 + 1] = rgb[1];
      pixels[i * 4 + 2] = rgb[2];
      pixels[i * 4 + 3] = 255;
      continue;
    }

    let alpha = 255;
    if (layer === 'water') {
      const ice=iceMask?iceMask[i]/255:0,coverage=wet+ice-wet*ice;
      alpha=Math.round(coverage*255);
      for(let k=0;k<3;k++)out[k]=coverage?(waterColors[2][k]*wet*(1-ice)+ICE_RGB[k]*ice)/coverage:0;
    } else if (layer === 'height') {
      flatColor(kind, elevationRampValue(fields.heights[i]), out, snow, fields.depth[i]);
    } else if (layer === 'climate') {
      const c = CLIMATE_RGB[fields.climate[i]];
      out[0] = c[0]; out[1] = c[1]; out[2] = c[2];
      alpha = kind ? 0 : 255;
    } else if (layer === 'temperature') {
      const t = fields.temperature[i], c = temperatureRGB(t);
      out[0] = c[0]; out[1] = c[1]; out[2] = c[2];
      alpha = kind ? 0 : 255;
    } else if (layer === 'ice') {
      const water = kind===1||!mask&&kind ? 1 : Math.max(wet, fields.rivers[i] / 255);
      const ice = iceMask?iceMask[i]/255:fields.ice[i] / 255 * water;
      out[0] = ICE_RGB[0]; out[1] = ICE_RGB[1]; out[2] = ICE_RGB[2];
      alpha = Math.round(ice * 255);
      if (!alpha) { pixels[i * 4 + 3] = 0; continue; }
    } else if (layer === 'depth') {
      const d = fields.depth[i] / 255;
      out[0] = 8; out[1] = 30; out[2] = 52;
      alpha = kind ? Math.round(d * 200) : 0;
      if (!alpha) { pixels[i * 4 + 3] = 0; continue; }
    } else {
      out[0] = base[i * 3]; out[1] = base[i * 3 + 1]; out[2] = base[i * 3 + 2];
      if (!independentWater&&(kind === 1 || !mask&&kind === 2)) {
        out[0] = SEA_RGB[0]; out[1] = SEA_RGB[1]; out[2] = SEA_RGB[2];
      } else if (!independentWater&&kind === 3) {
        for (let k = 0; k < 3; k++) out[k] = out[k] + (waterColors[3][k] - out[k]) * 0.55;
      }
      const river=independentWater?0:fields.rivers[i]/255,coverage=independentWater?wet:mask?mask[i]:0;
      for (let k = 0; k < 3; k++) {
        let color = out[k];
        if (river) color = color * (1 - river * 0.85) + waterColors[3][k] * river * 0.85;
        out[k] = color * (1 - coverage) + waterColors[2][k] * coverage;
        if(independentWater&&iceMask){const ice=iceMask[i]/255;out[k]=out[k]*(1-ice)+ICE_RGB[k]*ice;}
      }
    }
    pixels[i * 4] = out[0];
    pixels[i * 4 + 1] = out[1];
    pixels[i * 4 + 2] = out[2];
    pixels[i * 4 + 3] = alpha;
  }
  return pixels;
}
