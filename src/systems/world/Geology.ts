import {Block, BLOCK_RGB, SPECIAL_BLOCKS, getBlockDef} from './Blocks';
import {overlayTerrain, type TerrainProfile, type TerrainWeight} from './TerrainBlend';
import {clamp, smooth} from './TerrainLayers';

const ROCKS = [Block.GRANITE,Block.LIMESTONE,Block.SANDSTONE,Block.SHALE,Block.SLATE,Block.BASALT,Block.QUARTZITE];
export function geologicalRock(region: number, strata: number, height: number): number {
  const layer = Math.floor((height + strata * 90) / 48);
  const province = Math.floor((region + 1) * 3.5);
  return ROCKS[((province + layer) % ROCKS.length + ROCKS.length) % ROCKS.length];
}

/** 地质母材控制裸岩、风化带与沉积土 */
export function applyGeology(profile: TerrainProfile, weights: readonly TerrainWeight[], region: number,
  strata: number, height: number, slope: number, submerged: boolean, snow: number): void {
  const rock = geologicalRock(region,strata,height), exposure = smooth((slope - .45) / 3.2) * (1 - snow);
  const rgb = (id: number) => [BLOCK_RGB[id*3],BLOCK_RGB[id*3+1],BLOCK_RGB[id*3+2]];
  overlayTerrain(profile, exposure * .65, rgb(rock), [[rock,1]]);
  const soil = submerged ? Block.SILT : profile.wetness > .65 ? Block.PEAT : profile.wetness < .2 ? Block.LOESS : Block.ALLUVIUM;
  overlayTerrain(profile, .16 * (1-exposure) * (1-snow), rgb(soil), [[soil,1]]);
  // 连续省域与岩层场决定露头
  for (const {type,weight} of weights) for (const def of SPECIAL_BLOCKS.get(type) ?? []) {
    const fitness = def.category === 'rock' ? .12 + exposure*.65 : def.category === 'ice' ? snow*.6
      : def.category === 'sediment' ? submerged ? .65 : .22 : .4*(1-exposure)*(1-snow);
    const patch = .45 + .55 * smooth((region + strata*.35 + .8) / 1.6);
    const amount = clamp(weight * fitness * patch * .65);
    overlayTerrain(profile, amount, rgb(def.id), [[def.id,1]]);
  }
}

/** 松散母材的风化微地势较软 */
export function geologicalRelief(region: number, strata: number, height: number, weather: number): number {
  const strength = getBlockDef(geologicalRock(region,strata,height))!.erodibility;
  return weather * (.3 + strength * 1.5);
}
