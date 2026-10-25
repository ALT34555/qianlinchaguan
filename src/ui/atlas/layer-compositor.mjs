import {MAX_CONTOUR_LEVEL} from './contour.mjs';
import {dayProfile, dayFactor, daylightPixel, normalizeSunHours} from './relief.mjs';
import {layerScreenGrid, projectLayerPixels} from './layer-projection.mjs';
import {ancestor} from './tile-plan.mjs';
import {TAU,wrap,visibleTiles, unproject} from './tiles.mjs';

import {STATIC_LAYER_LEVEL} from './static-layers.mjs';
import {drawBaselines} from './baseline.mjs';
import {drawChunkInformation} from './chunk-information.mjs';

// 基准线图层绘制器
export class BaselineLayerRenderer {
  constructor() {
    this.id = 'baseline';
    this.blendMode = 'source-over';
  }

  draw(ctx, {state}) {
    drawBaselines(ctx, state);
  }

  dispose() {}
}

// 日照图层绘制器
export class DaylightLayerRenderer {
  constructor() {
    this.id = 'daylight';
    this.blendMode = 'multiply';
  }

  draw(ctx, {state, layer, compositor, owner, key}) {
    const sun = normalizeSunHours(owner.sunHours);
    const sunWorld = owner.sunWorld?.();
    const cacheKey = `${key}:${sun}:${JSON.stringify(sunWorld)}`;
    const image = compositor.daylightImage(state, cacheKey, sun, sunWorld);
    ctx.drawImage(image, 0, 0, state.width, state.height);
  }

  dispose() {}
}

// 雨云图层绘制器
export class RainLayerRenderer {
  constructor() {
    this.id = 'rain';
    this.blendMode = 'source-over';
  }

  draw(ctx, {state, owner}) {
    owner.drawRainClouds?.(ctx, state.width, state.height, owner.baseCellSize * owner.scale, false);
  }

  dispose() {}
}

// 投影数据图层基类
class ProjectedDataLayerRenderer {
  constructor(id, blendMode = 'source-over', maxLevel = 99) {
    this.id = id;
    this.blendMode = blendMode;
    this.maxLevel = maxLevel;
  }

  getTargets(state, tiles) {
    return tiles;
  }

  draw(ctx, {state, tiles, store, focusId, style, compositor, key, layerTargets}) {
    const targets = layerTargets?.get(this.id) ?? this.getTargets(state, tiles);
    const found = new Map();
    for (const tile of targets) {
      const top = Math.min(tile.z, this.maxLevel);
      const bottom = this.id === 'contour' ? top : 2;
      for (let z = top; z >= bottom; z--) {
        const p = ancestor(tile, z);
        const cached = store.get(compositor.focused(style, p, focusId), p.key);
        if (cached) {
          found.set(p.key, cached);
          break;
        }
      }
    }
    const source = [...found.values()].sort((a, b) => a.z - b.z);
    const projectedKey = `${key}:${focusId}:${style}:${source.map(t => `${t.key}:${t.revision ?? 0}`).join(',')}`;
    const image = compositor.projectLayer(this.id, source, state, projectedKey);
    ctx.drawImage(image, 0, 0, state.width, state.height);
  }

  dispose() {}
}

// 等高线图层
export class ContourLayerRenderer extends ProjectedDataLayerRenderer {
  constructor() {
    super('contour', 'multiply', MAX_CONTOUR_LEVEL);
  }
}

// 水深图层（缓存L4）
export class DepthLayerRenderer extends ProjectedDataLayerRenderer {
  constructor() {
    super('depth', 'source-over', STATIC_LAYER_LEVEL);
  }

  getTargets(state) {
    return visibleTiles(state, STATIC_LAYER_LEVEL);
  }
}

// 气候分类图层
export class ClimateLayerRenderer {
  constructor() { this.id = 'climate'; this.blendMode = 'source-over'; }
  draw(ctx, {state, climateTiles = [], compositor, key}) {
    const image = compositor.projectLayer(this.id, climateTiles, state, `${key}:climate-plan`);
    ctx.drawImage(image, 0, 0, state.width, state.height);
  }
  dispose() {}
}

// 气温图层
export class TemperatureLayerRenderer extends ProjectedDataLayerRenderer {
  constructor() {
    super('temperature', 'source-over', 99);
  }
}

// 高程图层
export class HeightLayerRenderer extends ProjectedDataLayerRenderer {
  constructor() {
    super('height', 'source-over', 99);
  }
}

// 冰面图层
export class IceLayerRenderer extends ProjectedDataLayerRenderer {
  constructor() {
    super('ice', 'source-over', STATIC_LAYER_LEVEL);
  }

  getTargets(state) { return visibleTiles(state, STATIC_LAYER_LEVEL); }
}

// 文明层：置旗地与领地范围
export class CivilizationLayerRenderer {
  constructor() {
    this.id = 'civilization';
    this.blendMode = 'source-over';
  }

  draw(ctx, {state, owner}) {
    owner.drawTerritoryLayer?.(ctx, state.width, state.height, owner.baseCellSize * owner.scale);
  }

  dispose() {}
}

// 图层合成器：统一绘制入口与混合顺序
export class LayerCompositor {
  constructor() {
    this.projections = new Map();
    this.renderers = new Map([
      ['civilization', new CivilizationLayerRenderer()],
      ['chunks', {blendMode:'source-over',draw:drawChunkInformation}],
      ['baseline', new BaselineLayerRenderer()],
      ['daylight', new DaylightLayerRenderer()],
      ['rain', new RainLayerRenderer()],
      ['contour', new ContourLayerRenderer()],
      ['climate', new ClimateLayerRenderer()],
      ['temperature', new TemperatureLayerRenderer()],
      ['height', new HeightLayerRenderer()],
      ['ice', new IceLayerRenderer()],
      ['depth', new DepthLayerRenderer()],
    ]);
  }

  focused(style, tile, focusId) {
    return tile && tile.z >= 13 ? `${style}:lake:${focusId ?? 'none'}` : style;
  }

  projectLayer(id, tiles, state, key) {
    const cached = this.projections.get(id);
    if (cached?.key === key) return cached.canvas;
    const canvas = cached?.canvas ?? document.createElement('canvas');
    if(state.view==='projection'){
      const width=Math.ceil(state.width),height=Math.ceil(state.height);
      if(canvas.width!==width||canvas.height!==height){canvas.width=width;canvas.height=height;}
      const ctx=canvas.getContext('2d');ctx.clearRect(0,0,width,height);
      this.tileImages??=new Map();
      for(const tile of [...tiles].sort((a,b)=>a.z-b.z)){
        let image=this.tileImages.get(tile);
        if(!image){
          if(tile.pixels){const side=tile.side??128;image=document.createElement('canvas');image.width=image.height=side;
            image.getContext('2d').putImageData(new ImageData(tile.pixels,side,side),0,0);
          }else image=tile.canvas;
          this.tileImages.set(tile,image);
        }
        const span=TAU/2**tile.z,size=span*state.scale,period=TAU*state.scale;
        const center=wrap((tile.x+.5)*span-Math.PI-state.lon);
        const x=state.width/2+center*state.scale-size/2;
        const y=state.height/2-(Math.PI/2-tile.y*span-state.lat)*state.scale;
        const first=Math.ceil((-size-x)/period),last=Math.floor((width-x)/period);
        ctx.imageSmoothingEnabled=!tile.nearest;
        for(let k=first;k<=last;k++){
          const left=x+k*period;ctx.clearRect(left,y,size,size);
          if(tile.pixels)ctx.drawImage(image,left,y,size,size);
          else ctx.drawImage(image,1,1,image.width-2,image.height-2,left,y,size,size);
        }
      }
      while(this.tileImages.size>128){
        const [tile,image]=this.tileImages.entries().next().value;this.tileImages.delete(tile);
        if(tile.pixels){image.width=0;image.height=0;}
      }
      this.projections.set(id,{key,canvas});return canvas;
    }
    const cameraKey = JSON.stringify(state);
    if (this.screenGrid?.key !== cameraKey) {
      this.screenGrid = {key: cameraKey, ...layerScreenGrid(state)};
    }
    const {width, height} = this.screenGrid;
    if (canvas.width !== width || canvas.height !== height) {
      canvas.width = width;
      canvas.height = height;
    }
    const raster = cached?.raster ?? {};
    const pixels = projectLayerPixels(tiles, this.screenGrid, raster);
    const imgData = typeof ImageData !== 'undefined' ? new ImageData(pixels, width, height) : {data: pixels, width, height};
    canvas.getContext('2d').putImageData(imgData, 0, 0);
    this.projections.set(id, {key, canvas, raster});
    return canvas;
  }

  daylightImage(state, key, hours, world) {
    if (this.daylight?.key === key) return this.daylight.canvas;
    const canvas = this.daylight?.canvas ?? document.createElement('canvas');
    const factor = Math.min(1, Math.sqrt(120000 / (state.width * state.height)));
    const width = Math.max(1, Math.ceil(state.width * factor));
    const height = Math.max(1, Math.ceil(state.height * factor));
    canvas.width = width;
    canvas.height = height;
    const pixels = new Uint8ClampedArray(width * height * 4);
    const profile = dayProfile(hours, world);
    for (let y = 0; y < height; y++) {
      for (let x = 0; x < width; x++) {
        const point = unproject(state, (x + 0.5) * state.width / width, (y + 0.5) * state.height / height);
        const i = (y * width + x) * 4;
        if (!point) {
          pixels[i] = pixels[i + 1] = pixels[i + 2] = 255;
          pixels[i + 3] = 255;
          continue;
        }
        const factorLight = dayFactor(profile, point.lat, point.lon);
        const rgb = daylightPixel(factorLight);
        pixels[i] = rgb[0];
        pixels[i + 1] = rgb[1];
        pixels[i + 2] = rgb[2];
        pixels[i + 3] = 255;
      }
    }
    const imgData = typeof ImageData !== 'undefined' ? new ImageData(pixels, width, height) : {data: pixels, width, height};
    canvas.getContext('2d').putImageData(imgData, 0, 0);
    this.daylight = {key, canvas};
    return canvas;
  }

  // 列表在前画在上层：倒序绘制
  draw(ctx, state, enabledLayers, context) {
    const activeIds = new Set(enabledLayers.map(l => l.id));
    for (const id of this.projections.keys()) {
      if (!activeIds.has(id) && id !== 'base' && id !== 'backdrop' && id !== 'river-water') this.projections.delete(id);
    }
    if (!activeIds.has('daylight')) {
      this.daylight = undefined;
    }

    const effectiveCompositor = context.compositor ?? this;

    for (const layer of [...enabledLayers].reverse()) {
      const renderer = this.renderers.get(layer.id);
      if (!renderer) continue;

      ctx.save();
      ctx.globalCompositeOperation = renderer.blendMode;
      ctx.globalAlpha = layer.opacity;
      renderer.draw(ctx, {
        ...context,
        state,
        layer,
        style: context.styleFor?.(layer.id) ?? layer.id,
        compositor: effectiveCompositor,
      });
      ctx.restore();
    }
  }

  dispose() {
    for(const {canvas} of this.projections.values()){canvas.width=0;canvas.height=0;}
    if(this.daylight?.canvas){this.daylight.canvas.width=0;this.daylight.canvas.height=0;}
    this.projections.clear();
    for(const [tile,image] of this.tileImages??[])if(tile.pixels){image.width=0;image.height=0;}
    this.tileImages=undefined;
    this.daylight = undefined;
    this.screenGrid = undefined;
    for (const renderer of this.renderers.values()) {
      renderer.dispose?.();
    }
  }
}
