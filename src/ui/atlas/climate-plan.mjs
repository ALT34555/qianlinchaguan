import {macroAt} from '../../systems/world/planet-macro/backend.mjs';
import {CLIMATE_RGB} from './relief.mjs';

const plans = new WeakMap();

/** 固定世界气候规划，仅烘焙一次 */
export function climatePlanTiles(world) {
  const cached = plans.get(world);
  if (cached) return cached;
  if (!world.temperature || !world.height) return [];
  const width = 256, height = 128, side = 64;
  const source = new Uint8ClampedArray(width * height * 4);
  for (let y = 0; y < height; y++) for (let x = 0; x < width; x++) {
    const wx = ((x + .5) / width - .5) * world.circumference;
    const wz = ((y + .5) / width - .25) * world.circumference;
    const t = macroAt(world, wx, wz, 'temperature');
    const id = t >= 26 ? 0 : t >= 18 ? 1 : t >= 10 ? 2 : t >= 0 ? 3 : 4;
    const i = (y * width + x) * 4, rgb = CLIMATE_RGB[id];
    source[i] = rgb[0]; source[i + 1] = rgb[1]; source[i + 2] = rgb[2];
    const col = Math.floor((x + .5) / width * world.columns), row = Math.floor((y + .5) / height * world.rows);
    source[i + 3] = macroAt(world, wx, wz) > 0 && world.water?.[row * world.columns + col] !== 2 ? 255 : 0;
  }
  const smooth = new Uint8ClampedArray(source.length);
  for (let y = 0; y < height; y++) for (let x = 0; x < width; x++) {
    const i = (y * width + x) * 4;
    let weight = 0;
    const rgb = [0, 0, 0];
    for (let dy = -1; dy <= 1; dy++) for (let dx = -1; dx <= 1; dx++) {
      const sx = (x + dx + width) % width, sy = Math.max(0, Math.min(height - 1, y + dy));
      const at = (sy * width + sx) * 4, w = source[at + 3] * (dx ? 1 : 2) * (dy ? 1 : 2);
      weight += w;
      for (let k = 0; k < 3; k++) rgb[k] += source[at + k] * w;
    }
    for (let k = 0; k < 3; k++) smooth[i + k] = weight ? rgb[k] / weight : 0;
    smooth[i + 3] = source[i + 3];
  }
  const tiles = [];
  for (let y = 0; y < 2; y++) for (let x = 0; x < 4; x++) {
    const pixels = new Uint8ClampedArray(side * side * 4);
    for (let row = 0; row < side; row++) {
      const at = ((y * side + row) * width + x * side) * 4;
      pixels.set(smooth.subarray(at, at + side * 4), row * side * 4);
    }
    tiles.push({z: 2, x, y, side, pixels, key: `2/${x}/${y}`});
  }
  plans.set(world, tiles);
  return tiles;
}
