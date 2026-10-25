import {ancestor} from './tile-plan.mjs';
import {visibleTiles, TILE} from './tiles.mjs';
import {isStaticLayer, STATIC_LAYER_LEVEL} from './static-layers.mjs';
import {MAX_CONTOUR_LEVEL} from './contour.mjs';

const BYTES = TILE * TILE * 4;
export const usesTileWorker = id => !['rain', 'daylight', 'baseline', 'climate', 'civilization', 'chunks'].includes(id);

/** 合并视口与已保留的固定瓦片 */
export function staticResidency(state, layers, store, styleFor) {
  const fixed = new Set();
  for (const [id, item] of store.items) if (item.tile.persistent) fixed.add(id);
  const targets = visibleTiles(state, STATIC_LAYER_LEVEL);
  for (const layer of layers) if (isStaticLayer(layer.id)) {
    for (const tile of targets) fixed.add(store.id(styleFor(layer.id), tile.key));
  }
  return {targets, bytes: fixed.size * BYTES};
}

/** 工作集必须能驻留缓存 */
export function planLayerResidency(plan, layers, fixed, budget) {
  const ancestors = new Set(), targets = new Map(), caps = new Map();
  for (const tile of plan.tiles) for (let z = 2; z <= tile.z; z++) ancestors.add(ancestor(tile, z).key);
  const at = level => [...new Map(plan.tiles.map(t => {const p = ancestor(t, Math.min(t.z, level)); return [p.key, p];})).values()];
  for (const layer of layers) if (usesTileWorker(layer.id)) {
    const cap = isStaticLayer(layer.id) ? STATIC_LAYER_LEVEL : Math.min(plan.z, layer.id === 'contour' ? MAX_CONTOUR_LEVEL : plan.z);
    caps.set(layer.id, cap);
    targets.set(layer.id, isStaticLayer(layer.id) ? fixed.targets : at(cap));
  }
  const waterTiles=[...ancestors].filter(key=>Number(key.split('/')[0])>=17).length+(plan.z>=17?9:0);
  const estimate = () => fixed.bytes + (ancestors.size + waterTiles + [...targets].reduce((sum, [id, tiles]) => sum + (isStaticLayer(id) ? 0 : tiles.length), 0) + 9) * BYTES;
  while (estimate() > budget * .92) {
    const choices = [...targets].filter(([id]) => !isStaticLayer(id) && caps.get(id) > 2).sort((a, b) => b[1].length - a[1].length);
    if (!choices.length) break;
    const id = choices[0][0], cap = caps.get(id) - 1;
    caps.set(id, cap); targets.set(id, at(cap));
  }
  return {targets, caps, bytes: estimate()};
}
