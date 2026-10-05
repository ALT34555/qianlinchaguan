/** 所有非虚空地形共用的地表配方与混合器，不维护成对的特例。 */
import { Block } from './Blocks';
import { CHUNK_TYPES } from './ChunkTypes';

export interface TerrainWeight { type: number; weight: number }
export interface TerrainProfile {
  color: [number, number, number];
  materials: number[];
  roughness: number;
  wetness: number;
}
type Family = 'plain' | 'coast' | 'hill' | 'dry' | 'river' | 'forest' | 'valley' | 'plateau' | 'rift'
  | 'savanna' | 'desert' | 'swamp' | 'monsoon' | 'jungle' | 'shrub' | 'snowpeak' | 'snowfield';
interface Recipe { color: string; material: [number, number][]; roughness: number; wetness: number }
const RECIPES: Record<Family, Recipe> = {
  plain: { color: '#829956', material: [[Block.GRASS, .9], [Block.DIRT, .1]], roughness: 1.1, wetness: .25 },
  coast: { color: '#c2bc95', material: [[Block.SAND, .85], [Block.GRAVEL, .15]], roughness: .45, wetness: .85 },
  hill: { color: '#81906b', material: [[Block.GRASS, .6], [Block.STONE, .3], [Block.GRAVEL, .1]], roughness: 2.5, wetness: .15 },
  dry: { color: '#b5a171', material: [[Block.DRY_DIRT, .75], [Block.SAND, .25]], roughness: .9, wetness: 0 },
  river: { color: '#7f9670', material: [[Block.GRASS, .6], [Block.MUD, .25], [Block.GRAVEL, .15]], roughness: .45, wetness: .9 },
  forest: { color: '#5e7d4e', material: [[Block.GRASS, .6], [Block.FOREST_SOIL, .4]], roughness: 1.4, wetness: .7 },
  valley: { color: '#839963', material: [[Block.GRASS, .75], [Block.DIRT, .15], [Block.MUD, .1]], roughness: .65, wetness: .65 },
  plateau: { color: '#a3a17a', material: [[Block.GRASS, .4], [Block.GRAVEL, .35], [Block.STONE, .25]], roughness: 1.6, wetness: .1 },
  rift: { color: '#928777', material: [[Block.STONE, .65], [Block.GRAVEL, .25], [Block.DIRT, .1]], roughness: 2.8, wetness: .1 },
  savanna: { color: '#b0ae67', material: [[Block.GRASS, .6], [Block.DRY_DIRT, .4]], roughness: 1, wetness: .15 },
  desert: { color: '#d4bf87', material: [[Block.SAND, .9], [Block.DRY_DIRT, .1]], roughness: 1.8, wetness: 0 },
  swamp: { color: '#738267', material: [[Block.MUD, .65], [Block.GRASS, .35]], roughness: .3, wetness: 1 },
  monsoon: { color: '#71884d', material: [[Block.GRASS, .6], [Block.FOREST_SOIL, .4]], roughness: 1.2, wetness: .7 },
  jungle: { color: '#52764e', material: [[Block.GRASS, .55], [Block.FOREST_SOIL, .35], [Block.MUD, .1]], roughness: 1.5, wetness: .9 },
  shrub: { color: '#a2a177', material: [[Block.GRASS, .5], [Block.DRY_DIRT, .4], [Block.GRAVEL, .1]], roughness: 1.2, wetness: .2 },
  snowpeak: { color: '#cbd4cf', material: [[Block.SNOW, .7], [Block.STONE, .3]], roughness: 2.8, wetness: .1 },
  snowfield: { color: '#e2e8df', material: [[Block.SNOW, .95], [Block.GRAVEL, .05]], roughness: .6, wetness: .15 },
};
const FAMILIES: Record<number, Family> = {
  1: 'plain', 2: 'coast', 3: 'hill', 4: 'dry', 5: 'river', 6: 'forest', 7: 'valley', 8: 'plateau', 9: 'rift', 10: 'river',
  101: 'savanna', 102: 'coast', 103: 'hill', 104: 'desert', 105: 'swamp', 106: 'jungle', 107: 'valley', 108: 'plateau', 109: 'monsoon',
  201: 'plain', 202: 'coast', 203: 'hill', 204: 'shrub', 205: 'monsoon', 206: 'jungle', 207: 'valley', 208: 'plateau',
  301: 'plain', 302: 'coast', 303: 'hill', 304: 'shrub', 305: 'monsoon', 306: 'jungle', 307: 'valley', 308: 'plateau',
  401: 'plain', 402: 'coast', 403: 'hill', 404: 'shrub', 405: 'monsoon', 406: 'snowpeak', 407: 'valley', 408: 'plateau', 409: 'snowfield',
};
export const TERRAIN_PROFILES = new Map<number, TerrainProfile>();
for (const def of CHUNK_TYPES.filter(d => d.generate)) {
  const recipe = RECIPES[FAMILIES[def.id]];
  if (!recipe) throw new Error(`地形 ${def.code} 缺少混合配方`);
  const rgb = parseInt(recipe.color.slice(1), 16);
  const color: [number, number, number] = [rgb >> 16, (rgb >> 8) & 255, rgb & 255];
  const materials = Array<number>(12).fill(0);
  recipe.material.forEach(([id, amount]) => { materials[id] = amount; });
  const zone = Math.floor(def.id / 100);
  // 色相微调而不是直接使用地图图例色；每个气候的山丘/高地/林地也可混合。
  const tint = zone === 1 ? [5, 3, -7] : zone === 2 ? [-3, 5, -2] : zone === 3 ? [-4, -1, 6] : zone === 4 ? [8, 10, 17] : [0, 0, 0];
  color.forEach((v, i) => { color[i] = Math.max(0, Math.min(255, v + tint[i])); });
  TERRAIN_PROFILES.set(def.id, { color, materials, roughness: recipe.roughness, wetness: recipe.wetness });
}

export function mixTerrainProfiles(weights: readonly TerrainWeight[]): TerrainProfile {
  const result: TerrainProfile = { color: [0, 0, 0], materials: Array<number>(12).fill(0), roughness: 0, wetness: 0 };
  let sum = 0;
  for (const { type, weight } of weights) {
    if (!Number.isFinite(weight) || weight < 0) throw new Error('地形混合权重必须有限且非负');
    if (weight === 0) continue;
    const profile = TERRAIN_PROFILES.get(type);
    if (!profile) throw new Error(`不支持混合的地形 ${type}`);
    sum += weight;
    for (let i = 0; i < 3; i++) result.color[i] += profile.color[i] * weight;
    for (let i = 0; i < 12; i++) result.materials[i] += profile.materials[i] * weight;
    result.roughness += profile.roughness * weight; result.wetness += profile.wetness * weight;
  }
  if (sum <= 0) throw new Error('地形混合权重之和必须大于零');
  result.color = result.color.map(v => v / sum) as TerrainProfile['color'];
  result.materials = result.materials.map(v => v / sum);
  result.roughness /= sum; result.wetness /= sum;
  return result;
}

/** 连续叠加岩石、积雪、沉积物等环境层，材质标签不主导颜色切换。 */
export function overlayTerrain(profile: TerrainProfile, amount: number, color: readonly number[], materials: [number, number][]): void {
  const t = Math.max(0, Math.min(1, amount));
  for (let i = 0; i < 3; i++) profile.color[i] += (color[i] - profile.color[i]) * t;
  for (let i = 0; i < profile.materials.length; i++) profile.materials[i] *= 1 - t;
  for (const [id, weight] of materials) profile.materials[id] += weight * t;
}
