import {continuousSatelliteColor, continuousBiomeColor} from '../../systems/world/planet-macro/satellite.mjs';
import {SEA_RGB} from './relief.mjs';
import {markSmallLakes} from './lake-vectors.mjs';

/** 全球骨架底图，不触发地表细化，与底图同一连续配色 */
export function macroBackdrop(world, palette) {
  if (!world.height || !world.temperature || !world.rainfall || !world.evaporation) return [];
  const side = 32, n = 4, tiles = [];
  const columns = world.columns, rows = world.rows;
  const span = world.circumference / columns;
  for (let y = 0; y < n / 2; y++) for (let x = 0; x < n; x++) {
    const pixels = new Uint8ClampedArray(side * side * 4);
    for (let py = 0; py < side; py++) for (let px = 0; px < side; px++) {
      const col = Math.min(columns - 1, Math.floor((x + (px + .5) / side) / n * columns));
      const row = Math.min(rows - 1, Math.floor((y + (py + .5) / side) / n * columns));
      const i = row * columns + col;
      let rgb = world.water?.[i]===2?[51,113,139]:SEA_RGB;
      if (world.height[i] > 0 && world.water?.[i] !== 2) {
        if (palette === 'satellite') {
          const west = world.height[row * columns + (col + columns - 1) % columns];
          const east = world.height[row * columns + (col + 1) % columns];
          const north = world.height[Math.max(0, row - 1) * columns + col];
          const south = world.height[Math.min(rows - 1, row + 1) * columns + col];
          const slope = Math.max(Math.abs(east - west), Math.abs(south - north)) / (2 * span);
          rgb = continuousSatelliteColor(world.temperature[i], world.rainfall[i], world.evaporation[i], slope * 64 * Math.SQRT2, slope);
        } else rgb = continuousBiomeColor(world.temperature[i], world.rainfall[i], world.evaporation[i]);
      }
      const at = (py * side + px) * 4;
      pixels[at] = rgb[0]; pixels[at + 1] = rgb[1]; pixels[at + 2] = rgb[2]; pixels[at + 3] = 255;
    }
    const tile={z:2,x,y,key:`2/${x}/${y}`,side,pixels};
    if(world.water&&world.lakeId&&world.lakes){
      const mask=markSmallLakes(new Float32Array(side*side),tile,world,side),lakeRGB=[51,113,139];
      for(let i=0;i<mask.length;i++)if(mask[i])for(let k=0;k<3;k++)pixels[i*4+k]=pixels[i*4+k]*(1-mask[i])+lakeRGB[k]*mask[i];
    }
    tiles.push(tile);
  }
  return tiles;
}
