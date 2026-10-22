import {TILE, TAU, clamp} from './tiles.mjs';
import {sampleSurface, macroAt, macroAtMany} from '../../systems/world/planet-macro/backend.mjs';
import {macroCurveGeometry as macroCurve} from '../../systems/world/planet-macro/source-rivers.mjs';
import {continuousSatelliteColor, continuousBiomeColor} from '../../systems/world/planet-macro/satellite.mjs';
import {coverRGB} from '../../systems/world/planet-macro/surface-cover.mjs';
import {macroRiverThreshold} from '../../systems/world/planet-macro/river-dimensions.mjs';
import {iceCoverage, ICE_RGB} from './ice-cover.mjs';
import {LakeShorePending} from '../../systems/world/planet-macro/lake-shores.mjs';
import {flatColor, dayFactor, daylightPixel, elevationRampValue, snowValue, depthValue, CLIMATE_RGB, SEA_RGB, temperatureRGB, DEPTH_SEA, DEPTH_LAKE} from './relief.mjs';
import {bandPosition, terraceShade, MIN_SEPARATION_PX} from './contour.mjs';
import {landTypeId} from '../../systems/world/planet-macro/chunk-metadata.mjs';
import macroDefs from '../../../content/data/world/macro_chunk_types.json' with {type:'json'};
import chunkDefs from '../../../content/data/world/chunk_types.json' with {type:'json'};

const waterColors = [[0, 0, 0], [34, 79, 103], [51, 113, 139], [64, 139, 166], [64, 139, 166]];
const riverWidths = [1.8, 2.4, 3.2, 4.2, 5.4];
const OVERVIEW_FIELDS = ['height', 'temperature', 'rainfall', 'evaporation'];

const hexToRGB = hex => [parseInt(hex.slice(1, 3), 16), parseInt(hex.slice(3, 5), 16), parseInt(hex.slice(5, 7), 16)];

// 编码色静态映射表
const SATELLITE_CODE_MAP = new Map();
const BIOME_CODE_MAP = new Map();

for (const def of [...chunkDefs, ...macroDefs]) {
  if (def.mapColor) {
    const rgb = hexToRGB(def.mapColor);
    SATELLITE_CODE_MAP.set(def.id, rgb);
    SATELLITE_CODE_MAP.set(def.code, rgb);
    SATELLITE_CODE_MAP.set(Number(def.code), rgb);
  }
}

const BIOME_CLASS_BASE = [
  [57, 122, 161],
  [180, 205, 215],
  [105, 155, 125],
  [95, 165, 80],
  [170, 165, 80],
  [45, 130, 60],
];

for (const def of macroDefs) {
  const landClass = def.landClass ?? Math.floor(def.id / 100);
  const material = def.material ?? (Math.floor(def.id / 10) % 10);
  const terrain = def.terrain ?? (def.id % 10);
  if (landClass === 0 || def.id === 2 || def.id === 32) {
    BIOME_CODE_MAP.set(def.id, SEA_RGB);
    BIOME_CODE_MAP.set(def.code, SEA_RGB);
    BIOME_CODE_MAP.set(Number(def.code), SEA_RGB);
  } else {
    const base = BIOME_CLASS_BASE[Math.max(1, Math.min(5, landClass))] || BIOME_CLASS_BASE[3];
    let r = base[0], g = base[1], b = base[2];
    if (material === 1) {
      r = Math.round(r * 0.65 + 140 * 0.35);
      g = Math.round(g * 0.65 + 145 * 0.35);
      b = Math.round(b * 0.65 + 145 * 0.35);
    } else if (material === 3) {
      r = Math.round(r * 0.4 + 235 * 0.6);
      g = Math.round(g * 0.4 + 242 * 0.6);
      b = Math.round(b * 0.4 + 248 * 0.6);
    } else if (material === 4) {
      r = Math.round(r * 0.5 + 210 * 0.5);
      g = Math.round(g * 0.5 + 185 * 0.5);
      b = Math.round(b * 0.5 + 120 * 0.5);
    } else if (material === 5) {
      r = Math.round(r * 0.7 + 150 * 0.3);
      g = Math.round(g * 0.7 + 140 * 0.3);
      b = Math.round(b * 0.7 + 130 * 0.3);
    }
    const tMult = terrain === 4 ? 0.9 : terrain === 2 ? 0.95 : terrain === 3 ? 1.05 : 1.0;
    const rgb = [Math.round(clamp(r * tMult, 0, 255)), Math.round(clamp(g * tMult, 0, 255)), Math.round(clamp(b * tMult, 0, 255))];
    BIOME_CODE_MAP.set(def.id, rgb);
    BIOME_CODE_MAP.set(def.code, rgb);
    BIOME_CODE_MAP.set(Number(def.code), rgb);
  }
}

const UNKNOWN_LAND_RGB = [128, 128, 128];
const UNKNOWN_BIOME_RGB = [110, 150, 110];

// 纯类型编码映射：卫星配色
export function codeToSatelliteRGB(code) {
  const num = Number(code);
  if (num === 0) return [46, 46, 46];
  if (num === 2 || num === 32) return SEA_RGB;
  return SATELLITE_CODE_MAP.get(code) ?? SATELLITE_CODE_MAP.get(num) ?? UNKNOWN_LAND_RGB;
}

// 纯类型编码映射：生态配色
export function codeToBiomeRGB(code) {
  const num = Number(code);
  if (num === 0) return [46, 46, 46];
  if (num === 2 || num === 32) return SEA_RGB;
  return BIOME_CODE_MAP.get(code) ?? BIOME_CODE_MAP.get(num) ?? UNKNOWN_BIOME_RGB;
}

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
  const fields = macroAtMany(w, wx, wz, OVERVIEW_FIELDS), height = fields.height;
  const kind = w.water[i] === 2 ? 2 : height <= 0 ? 1 : 0;
  const cos = Math.max(0.01, Math.cos(wz / w.circumference * TAU));
  return {
    height,
    kind,
    temperature: fields.temperature,
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
  if (receiver < 0 || !w.riverWidth[cell]) return null;
  const point = i => ({
    wx: ((i % w.columns + 0.5) / w.columns - 0.5) * w.circumference,
    wz: (Math.floor(i / w.columns) + 0.5) / w.columns * w.circumference - w.circumference / 4,
  });
  return {receiver, points: [point(cell), point(receiver)], halfWidth: w.riverWidth[cell]};
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
    this.classificationCount = 0;
    this.colorSampleCount = 0;
    this.shorelineGeometryCount = 0;
    this.dataLayerCounts = { height: 0, temperature: 0, climate: 0, ice: 0, depth: 0, contour: 0 };
    this.grid4mCache = new Map();
    this.gridBytes=0;
    this.macroCodes = null;
    this.macroSparse=new Map();
    this.riverThreshold = macroRiverThreshold(
      world.options.equatorChunks / world.columns,
      world.options.precipitation
    );
    this.rivers = [];
    this.riversByRow = Array.from({length: world.rows}, () => []);
    for (let i = 0; i < world.height.length; i++) {
      if (world.riverWidth[i] > 0 && world.receiver[i] >= 0) {
        this.rivers.push(i);
        this.riversByRow[Math.floor(i / world.columns)].push(i);
      }
    }
  }

  cacheStats() {
    const codeBytes=(this.macroCodes?.byteLength??0)+this.macroSparse.size*16;
    return {fieldBytes:this.bytes,gridBytes:this.gridBytes,codeBytes,totalBytes:this.bytes+this.gridBytes+codeBytes,
      fieldLimit:this.budget,gridEntries:this.grid4mCache.size};
  }
  clear(){this.cache.clear();this.grid4mCache.clear();this.macroCodes=null;this.macroSparse.clear();this.bytes=0;this.gridBytes=0;}
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

  // 按需获取宏观类型编码并缓存
  getMacroCode(wx, wz) {
    const w = this.world;
    const x = Math.floor((wx / w.circumference + 0.5) * w.columns);
    const y = clamp(Math.floor((wz / w.circumference + 0.25) * w.columns), 0, w.rows - 1);
    const cell = y * w.columns + ((x % w.columns) + w.columns) % w.columns;

    let codes=this.macroCodes;
    if(!codes&&w.columns*w.rows*2<=this.budget/4)codes=this.macroCodes=new Uint16Array(w.columns*w.rows);
    let code=codes?codes[cell]:(this.macroSparse.get(cell)??0);
    if(code!==0){this.hits++;return code;}

    this.classificationCount++;
    this.samples++;
    code=macroCodeAt(w,cell);
    if(codes)codes[cell]=code;
    else {
      this.macroSparse.delete(cell);this.macroSparse.set(cell,code);
      while(this.macroSparse.size>Math.max(1,Math.min(4096,Math.floor(this.budget/64))))this.macroSparse.delete(this.macroSparse.keys().next().value);
    }
    return code;
  }

  // 世界网格颜色采样
  sample4mColorSync(wx,wz,step=4) {
    const w=this.world,C=w.circumference,wrap=x=>((x+C/2)%C+C)%C-C/2;
    const z=Math.max(-C/4,Math.min(C/4-Number.EPSILON,wz)),gz=Math.floor(z/step);
    const repWz=Math.max(-C/4+.5,Math.min(C/4-.5,(gz+.5)*step));
    const columns=Math.max(1,Math.floor(C*Math.max(0,Math.cos(repWz/C*TAU))/step));
    const sx=C/columns,gx=Math.floor(wrap(wx)/sx),wrapped=((gx%columns)+columns)%columns;
    const key=`${step}:${wrapped},${gz}`;
    const cached=this.grid4mCache.get(key);
    if(cached){this.hits++;this.grid4mCache.delete(key);this.grid4mCache.set(key,cached);return cached;}
    const canonicalIndex=wrapped<Math.ceil(columns/2)?wrapped:wrapped-columns;
    const repWx=wrap((canonicalIndex+.5)*sx),point=sampleSurface(w,repWx,repWz,true,false,false,false);
    this.colorSampleCount++;this.samples++;
    const satellite=point.surfaceColor===undefined?codeToSatelliteRGB(this.getMacroCode(repWx,repWz)):coverRGB(point);
    const biome=codeToBiomeRGB(this.getMacroCode(repWx,repWz));
    const values=new Float64Array([point.kind,point.height,point.temperature,repWx,repWz,sx,step]);
    const entry={values,satelliteRGB:Uint8ClampedArray.from(satellite),biomeRGB:Uint8ClampedArray.from(biome),
      get kind(){return values[0];},get height(){return values[1];},get temperature(){return values[2];}};
    entry.bytes=values.byteLength+entry.satelliteRGB.byteLength+entry.biomeRGB.byteLength+key.length*2;
    this.grid4mCache.set(key,entry);this.gridBytes+=entry.bytes;
    const limit=Math.min(2*2**20,this.budget/4);
    while(this.grid4mCache.size&&(this.gridBytes>limit||this.grid4mCache.size>32768)){const k=this.grid4mCache.keys().next().value;this.gridBytes-=this.grid4mCache.get(k).bytes;this.grid4mCache.delete(k);}
    this.trim();return entry;
  }

  async sample4mColor(wx,wz,palette='satellite',cancelled=()=>false,waitForShore,step=4) {
    if(cancelled())return null;
    return this.sample4mColorSync(wx,wz,step);
  }

  async modernFields(tile,focusId,cancelled,stride,options) {
    const logicalLevel=options.logicalLevel??tile.z,policy=logicalLevel<=17?'code':'surface';
    const key=`${this.cacheKeyFor(tile,focusId,stride)}/${policy}/${!!options.independentWater}`;
    const base=!options.layer||['base','satellite','biome'].includes(options.layer);
    let data=this.cache.get(key);
    if(data){this.hits++;this.cache.delete(key);this.cache.set(key,data);}
    else {
      const count=TILE*TILE;data={heights:new Float32Array(count),kinds:new Uint8Array(count),snow:new Uint8Array(count),depth:new Uint8Array(count),
        rivers:new Uint8Array(count),climate:new Uint8Array(count),temperature:new Int8Array(count),ice:new Uint8Array(count),
        satellite:new Uint8ClampedArray(count*3),biome:new Uint8ClampedArray(count*3),meters:this.world.circumference/2**tile.z/TILE};
    }
    const w=this.world,n=2**tile.z,step=Math.max(16,2**Math.floor(Math.log2(Math.max(16,data.meters))));
    if(base&&!data.hasBase) {
      let deadline=performance.now()+8;
      for(let row=0;row<TILE;row+=stride) {
        if(performance.now()>=deadline){await pause();if(cancelled())return null;deadline=performance.now()+8;}
        const wz=(tile.y+(row+.5)/TILE)/n*w.circumference-w.circumference/4,cy=clamp(Math.floor((wz/w.circumference+.25)*w.columns),0,w.rows-1);let prevCell=-1,sat,bio;
        for(let col=0;col<TILE;col+=stride) {
          const wx=(tile.x+(col+.5)/TILE)/n*w.circumference-w.circumference/2;

          if(policy==='code'){const cx=((Math.floor((wx/w.circumference+.5)*w.columns)%w.columns)+w.columns)%w.columns,cell=cy*w.columns+cx;if(cell!==prevCell){prevCell=cell;const code=this.getMacroCode(wx,wz);sat=codeToSatelliteRGB(code);bio=codeToBiomeRGB(code);}else this.hits++;}
          else {const entry=this.sample4mColorSync(wx,wz,step);sat=entry.satelliteRGB;bio=entry.biomeRGB;}
          for(let dy=0;dy<stride&&row+dy<TILE;dy++)for(let dx=0;dx<stride&&col+dx<TILE;dx++) {
            const i=(row+dy)*TILE+col+dx;
            for(let k=0;k<3;k++){data.satellite[i*3+k]=sat[k];data.biome[i*3+k]=bio[k];}
          }
        }
      }
      data.hasBase=true;data.colorStepM=policy==='surface'?step:null;if(!options.independentWater&&!data.hasRivers){await this.drawRivers(data,tile,cancelled);if(cancelled())return null;data.hasRivers=true;}
    }
    this.fillKinds(data, tile, stride);
    this.ensureLayerData(data,tile,stride,options);
    if(cancelled())return null;
    return this.retain(key,data);
  }

  // 补水类别(海1,湖2)供水层判读
  fillKinds(data, tile, stride = 1) {
    if (data.hasKinds) return;
    const w = this.world, n = 2 ** tile.z;
    for (let row = 0; row < TILE; row += stride) {
      const wz = (tile.y + (row + 0.5) / TILE) / n * w.circumference - w.circumference / 4;
      const cy = clamp(Math.floor((wz / w.circumference + .25) * w.columns), 0, w.rows - 1);
      for (let col = 0; col < TILE; col += stride) {
        const wx = (tile.x + (col + 0.5) / TILE) / n * w.circumference - w.circumference / 2;
        const cx = ((Math.floor((wx / w.circumference + .5) * w.columns) % w.columns) + w.columns) % w.columns;
        const cell = cy * w.columns + cx;
        const kind = w.water[cell] === 2 ? 2 : w.height[cell] <= 0 || w.water[cell] === 1 ? 1 : 0;
        for (let dy = 0; dy < stride && row + dy < TILE; dy++) {
          for (let dx = 0; dx < stride && col + dx < TILE; dx++) data.kinds[(row + dy) * TILE + col + dx] = kind;
        }
      }
    }
    data.hasKinds = true;
  }

  // 按需填充缓存瓦片图层字段
  ensureLayerData(data, tile, stride = 1, options = {}) {
    const layer = options.layer;
    if (!layer || layer === 'base' || layer === 'satellite' || layer === 'biome') return;
    const w = this.world, n = 2 ** tile.z;

    if (layer === 'temperature' && !data.hasTemp) {
      this.dataLayerCounts.temperature++;
      for (let row = 0; row < TILE; row += stride) {
        for (let col = 0; col < TILE; col += stride) {
          const wx = (tile.x + (col + 0.5) / TILE) / n * w.circumference - w.circumference / 2;
          const wz = (tile.y + (row + 0.5) / TILE) / n * w.circumference - w.circumference / 4;
          const x = Math.floor((wx / w.circumference + 0.5) * w.columns);
          const y = clamp(Math.floor((wz / w.circumference + 0.25) * w.columns), 0, w.rows - 1);
          const cell = y * w.columns + ((x % w.columns) + w.columns) % w.columns;
          const t = Math.max(-128, Math.min(127, Math.round(w.temperature[cell])));
          for (let dy = 0; dy < stride; dy++) {
            for (let dx = 0; dx < stride; dx++) {
              const py = row + dy, px = col + dx;
              if (py < TILE && px < TILE) data.temperature[py * TILE + px] = t;
            }
          }
        }
      }
      data.hasTemp = true;
    } else if (layer === 'climate' && !data.hasClimate) {
      this.dataLayerCounts.climate++;
      for (let row = 0; row < TILE; row += stride) {
        for (let col = 0; col < TILE; col += stride) {
          const wx = (tile.x + (col + 0.5) / TILE) / n * w.circumference - w.circumference / 2;
          const wz = (tile.y + (row + 0.5) / TILE) / n * w.circumference - w.circumference / 4;
          const x = Math.floor((wx / w.circumference + 0.5) * w.columns);
          const y = clamp(Math.floor((wz / w.circumference + 0.25) * w.columns), 0, w.rows - 1);
          const cell = y * w.columns + ((x % w.columns) + w.columns) % w.columns;
          const t = w.temperature[cell];
          const c = t >= 26 ? 0 : t >= 18 ? 1 : t >= 10 ? 2 : t >= 0 ? 3 : 4;
          for (let dy = 0; dy < stride; dy++) {
            for (let dx = 0; dx < stride; dx++) {
              const py = row + dy, px = col + dx;
              if (py < TILE && px < TILE) data.climate[py * TILE + px] = c;
            }
          }
        }
      }
      data.hasClimate = true;
    } else if ((layer === 'height' || layer === 'contour') && !data.hasHeight) {
      this.dataLayerCounts.height++;
      for (let row = 0; row < TILE; row += stride) {
        for (let col = 0; col < TILE; col += stride) {
          const wx = (tile.x + (col + 0.5) / TILE) / n * w.circumference - w.circumference / 2;
          const wz = (tile.y + (row + 0.5) / TILE) / n * w.circumference - w.circumference / 4;
          const x = Math.floor((wx / w.circumference + 0.5) * w.columns);
          const y = clamp(Math.floor((wz / w.circumference + 0.25) * w.columns), 0, w.rows - 1);
          const cell = y * w.columns + ((x % w.columns) + w.columns) % w.columns;
          const h=macroAt(w,wx,wz),t=macroAt(w,wx,wz,'temperature');
          const s = snowValue(t);
          const kind=h<=0?1:w.water[cell]===2?2:0;
          const d = kind === 1 ? depthValue(0, h, DEPTH_SEA) : kind === 2 ? depthValue(w.spill[cell], h, DEPTH_LAKE) : 0;
          for (let dy = 0; dy < stride; dy++) {
            for (let dx = 0; dx < stride; dx++) {
              const py = row + dy, px = col + dx;
              if (py < TILE && px < TILE) {
                const i = py * TILE + px;
                data.heights[i] = h;
                data.snow[i] = s;
                data.depth[i] = d;
              }
            }
          }
        }
      }
      data.hasHeight = true;
    } else if (layer === 'ice' && !data.hasIce) {
      this.dataLayerCounts.ice++;
      for (let row = 0; row < TILE; row += stride) {
        for (let col = 0; col < TILE; col += stride) {
          const wx = (tile.x + (col + 0.5) / TILE) / n * w.circumference - w.circumference / 2;
          const wz = (tile.y + (row + 0.5) / TILE) / n * w.circumference - w.circumference / 4;
          const x = Math.floor((wx / w.circumference + 0.5) * w.columns);
          const y = clamp(Math.floor((wz / w.circumference + 0.25) * w.columns), 0, w.rows - 1);
          const cell = y * w.columns + ((x % w.columns) + w.columns) % w.columns;
          const ice = Math.round(iceCoverage(w.temperature[cell]) * 255);
          for (let dy = 0; dy < stride; dy++) {
            for (let dx = 0; dx < stride; dx++) {
              const py = row + dy, px = col + dx;
              if (py < TILE && px < TILE) data.ice[py * TILE + px] = ice;
            }
          }
        }
      }
      data.hasIce = true;
    } else if (layer === 'depth' && !data.hasDepth) {
      this.dataLayerCounts.depth++;
      for (let row = 0; row < TILE; row += stride) {
        for (let col = 0; col < TILE; col += stride) {
          const wx=(tile.x+(col+.5)/TILE)/n*w.circumference-w.circumference/2,wz=(tile.y+(row+.5)/TILE)/n*w.circumference-w.circumference/4;
          const cx=((Math.floor((wx/w.circumference+.5)*w.columns)%w.columns)+w.columns)%w.columns;
          const cz=clamp(Math.floor((wz/w.circumference+.25)*w.columns),0,w.rows-1),cell=cz*w.columns+cx;
          const h=macroAt(w,wx,wz),kind=h<=0?1:w.water[cell]===2?2:0;
          const d = kind === 1 ? depthValue(0, h, DEPTH_SEA) : kind === 2 ? depthValue(w.spill[cell], h, DEPTH_LAKE) : 0;
          for (let dy = 0; dy < stride; dy++) {
            for (let dx = 0; dx < stride; dx++) {
              const py = row + dy, px = col + dx;
              if (py < TILE && px < TILE) data.depth[py * TILE + px] = d;
            }
          }
        }
      }
      data.hasDepth = true;
    }
  }

  // 缓存键包含世界特征、瓦片、焦点与步长
  cacheKeyFor(tile, focusId, stride) {
    const base = tile.z >= 13 && focusId !== null ? `${tile.key}/lake:${focusId}` : tile.key;
    return stride > 1 ? `${base}/s${stride}` : base;
  }

  async getFields(tile, focusId, cancelled = () => false, stride = 1, waitForShore, options = {}) {
    if((options.logicalLevel??tile.z)>=12)return this.modernFields(tile,focusId,cancelled,stride,options);
    const cacheKey = this.cacheKeyFor(tile, focusId, stride);
    const fullKey = this.cacheKeyFor(tile, focusId, 1);
    const previous = this.cache.get(cacheKey) ?? (stride > 1 ? this.cache.get(fullKey) : undefined);

    if (previous) {
      const key = this.cache.has(cacheKey) ? cacheKey : fullKey;
      this.cache.delete(key);
      this.cache.set(key, previous);
      this.hits++;
      this.ensureLayerData(previous, tile, stride, options);
      return previous;
    }

    const w = this.world, n = 2 ** tile.z, span = w.circumference / n, meters = span / TILE, count = TILE * TILE;
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

    const isBaseRequest = !options.layer || options.layer === 'base' || options.layer === 'satellite' || options.layer === 'biome';
    const isHeightOrContour = options.layer === 'height' || options.layer === 'contour';
    const isTemp = options.layer === 'temperature';
    const isClimate = options.layer === 'climate';
    const isIce = options.layer === 'ice';
    const isDepth = options.layer === 'depth';

    if (isHeightOrContour) this.dataLayerCounts.height++;
    if (isTemp) this.dataLayerCounts.temperature++;
    if (isClimate) this.dataLayerCounts.climate++;
    if (isIce) this.dataLayerCounts.ice++;
    if (isDepth) this.dataLayerCounts.depth++;

    data.hasTemp = isTemp;
    data.hasClimate = isClimate;
    data.hasHeight = isHeightOrContour;
    data.hasIce = isIce;
    data.hasDepth = isDepth;

    {
      // L2~L11：宏观底图采样
      const C = w.circumference, W = w.columns, H = w.rows, cellSpan = C / W;
      const H_ = w.height, T_ = w.temperature, R_ = w.rainfall, E_ = w.evaporation, poles = w.poles;
      const ph = poles.height, pt = poles.temperature, pr = poles.rainfall, pe = poles.evaporation;
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
          let kind, height, temp, rain, evap, eastSlope = 0, southSlope = 0, waterLevel = 0, surfaceColor, sObj;
          if (true) {
            const x = (wx / C + 0.5) * W - 0.5, ix = Math.floor(x), tx = x - ix;
            if (ix !== prevIx) {
              prevIx = ix;
              a = cellIdxAt(ix, iy, W, H); b = cellIdxAt(ix + 1, iy, W, H);
              c = cellIdxAt(ix, iy + 1, W, H); d = cellIdxAt(ix + 1, iy + 1, W, H);
            }
            height = mixVal(mixVal(H_[a], H_[b], tx), mixVal(H_[c], H_[d], tx), ty);
            temp = mixVal(mixVal(T_[a], T_[b], tx), mixVal(T_[c], T_[d], tx), ty);
            rain = mixVal(mixVal(R_[a], R_[b], tx), mixVal(R_[c], R_[d], tx), ty);
            evap = mixVal(mixVal(E_[a], E_[b], tx), mixVal(E_[c], E_[d], tx), ty);
            if (pole >= 0) {
              height = mixVal(ph[poleId], height, pole);
              temp = mixVal(pt[poleId], temp, pole);
              rain = mixVal(pr[poleId], rain, pole);
              evap = mixVal(pe[poleId], evap, pole);
            }
            const cellX = Math.floor((wx / C + 0.5) * W);
            const cellIdx = cellY * W + ((cellX % W) + W) % W;
            const rawKind = w.water[cellIdx] === 2 ? 2 : height <= 0 ? 1 : 0;
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
            kind = tile.z >= 7 && tile.z < 13 && rawKind === 2 ? 0 : rawKind;
          } else {
            let s;
            while (!s) {
              try {
                s = sampleSurface(w, wx, wz, true, false, tile.z >= 13, true);
                if (tile.z >= 7 && tile.z < 13 && s.kind === 2) s.kind = 0;
              } catch (error) {
                if (!(error instanceof LakeShorePending)) throw error;
                if (!waitForShore || !await waitForShore(error.promise, cancelled)) return null;
              }
            }
            kind = s.kind; height = s.height; temp = s.temperature;
            rain = s.macro.rainfall; evap = s.macro.evaporation;
            eastSlope = s.macro.eastSlope; southSlope = s.macro.southSlope;
            waterLevel = Number.isFinite(s.water) ? s.water : 0;
            surfaceColor = s.surfaceColor; sObj = s;
          }
          this.samples++;
          this.colorSampleCount++;
          const slope = Math.max(Math.abs(eastSlope), Math.abs(southSlope));
          const stillWater = kind === 1 || kind === 2;
          const satellite = stillWater ? SEA_RGB : continuousSatelliteColor(temp, rain, evap, slope * 64 * Math.SQRT2, slope, 0, satScratch);
          const biome = stillWater ? SEA_RGB : continuousBiomeColor(temp, rain, evap, bioScratch);
          const snow = snowValue(temp);
          const depth = kind === 1 ? depthValue(0, height, DEPTH_SEA) : kind === 2 ? depthValue(waterLevel, height, DEPTH_LAKE) : 0;
          const climate = temp >= 26 ? 0 : temp >= 18 ? 1 : temp >= 10 ? 2 : temp >= 0 ? 3 : 4;
          const temperature = Math.max(-128, Math.min(127, Math.round(temp)));
          const iceVal = Math.round(iceCoverage(temp) * 255);
          if (surfaceColor !== undefined && !stillWater) {
            const blend = 1 - clamp((meters - 8) / 120, 0, 1), ground = coverRGB(sObj);
            for (let k = 0; k < 3; k++) satellite[k] = satellite[k] * (1 - blend) + ground[k] * blend;
          }
          for (let dy = 0; dy < stride; dy++) {
            for (let dx = 0; dx < stride; dx++) {
              const y = row + dy, x = col + dx;
              if (y >= TILE || x >= TILE) continue;
              const i = y * TILE + x;
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

      if (true) {
        await this.drawRivers(data, tile, cancelled);
        if (cancelled()) return null;
      }
    }

    return this.retain(cacheKey,data);
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
        const curve = coarse ? overviewRiver(w, cell) : macroCurve(w, cell);
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
    if (layer === 'height') {
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
