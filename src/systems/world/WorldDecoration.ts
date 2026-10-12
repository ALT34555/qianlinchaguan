import { CHUNK_SIZE } from '../../core/config';
import { hash2 } from '../../core/math/Random';
import { Block, blockBase } from './Blocks';
import { terrainHeightAt } from './TerrainSurface';
import { waterHeightAt } from './WaterSurface';
import { scatterablePlants } from './vegetation/Plants';
import type { ClimateZone } from './vegetation/types';
import type { Season } from './Season';
import { rocksForChunk } from './rocks/Rocks';
import type { RockForm } from './rocks/types';
import { allowsNaturalDecoration } from './artificial/ArtificialState';
import { isRiverType } from './ChunkTypes';

export interface DecorationInput {
  artificialType?: number;
  cx: number;
  cz: number;
  type: number;
  temperature: number;
  seed: number;
  heights: Float32Array;
  waterLevels: Float32Array;
  surfaces: Uint32Array;
}

export interface DecorationPlacement {
  kind: 'plant' | 'rock';
  id: string;
  x: number;
  y: number;
  z: number;
  yaw: number;
  scale: number;
  season: Season;
  form: RockForm;
  type: number;
}

const SOILS = new Set<number>([Block.GRASS, Block.FOREST_SOIL, Block.DIRT, Block.DRY_DIRT,
  Block.MUD, Block.CLAY, Block.SILT, Block.LOESS, Block.PEAT, Block.LATERITE,
  Block.MOSS_SOIL, Block.PODZOL, Block.ALLUVIUM, Block.GLACIAL_TILL]);
const SANDS = new Set<number>([Block.SAND, Block.RED_SAND, Block.DARK_SAND]);
const plants = scatterablePlants().filter(p => !(p.tags ?? []).some(t => ['farm', 'crop', 'orchard'].includes(t)));
const forms: RockForm[] = ['outcrop', 'buried', 'buried', 'crusted', 'stacked'];

function sampleAquaticDecoration(
  xw: number, zw: number, yw: number, wlw: number,
  surface: number, type: number, temperature: number,
  season: Season, slopes: number[], rand: (n: number) => number,
): DecorationPlacement | null {
  if (!Number.isFinite(wlw) || !Number.isFinite(yw)) return null;
  const depth = wlw - yw;
  if (depth < -0.65 || depth > 2.2) return null;

  const isRiver = isRiverType(type) || surface === Block.ALLUVIUM || surface === Block.SILT;
  const isWetland = type === 11 || type === 105 || surface === Block.PEAT;
  const isStillWater = type === 2 || type % 100 === 2;
  const isWetSoil = surface === Block.MUD || surface === Block.ALLUVIUM || surface === Block.SILT || surface === Block.PEAT;
  const reedTempSuitable = temperature >= 8 && temperature <= 32;

  // 1. 茂密芦苇荡：仅在温度适宜的湿地/内陆静水沿岸生成
  if (reedTempSuitable && (isWetland || isStillWater) && !isRiver) {
    if (depth >= -0.45 && depth <= 0.45 && slopes.every(v => v <= 0.75)) {
      if (rand(101) < 0.62) {
        return {
          kind: 'plant', id: 'shui.luweidang', x: xw, y: yw - 0.06, z: zw,
          yaw: rand(103) * Math.PI * 2, scale: 0.88 + rand(107) * 0.28,
          season, form: 'outcrop', type,
        };
      }
    }
  }

  // 2. 河流两侧成丛芦苇与单株芦苇
  if (isRiver && reedTempSuitable && depth >= -0.65 && depth <= 0.35 && slopes.every(v => v <= 0.85)) {
    if (rand(113) < 0.58) {
      const reedId = rand(127) < 0.65 ? 'shui.luweicong' : 'shui.luwei';
      return {
        kind: 'plant', id: reedId, x: xw, y: yw - 0.06, z: zw,
        yaw: rand(131) * Math.PI * 2, scale: 0.85 + rand(137) * 0.3,
        season, form: 'outcrop', type,
      };
    }
  }

  // 3. 挺水水菖蒲与蒲草
  if ((isRiver || isWetland || isWetSoil) && temperature >= 4 && depth >= -0.5 && depth <= 0.35) {
    if (rand(149) < 0.42) {
      const plantId = rand(151) < 0.6 ? 'shui.changpu' : 'shui.pucao';
      return {
        kind: 'plant', id: plantId, x: xw, y: yw - 0.06, z: zw,
        yaw: rand(157) * Math.PI * 2, scale: 0.85 + rand(163) * 0.3,
        season, form: 'outcrop', type,
      };
    }
  }

  // 4. 浮叶植物：萍蓬草与睡莲
  if ((isWetland || isStillWater) && !isRiver && temperature >= 6 && season !== 'winter') {
    if (depth > 0.15 && depth <= 1.8 && slopes.every(v => v <= 0.6)) {
      if (rand(173) < 0.45) {
        const plantId = rand(179) < 0.65 ? 'shui.pingpeng' : 'shui.lian';
        return {
          kind: 'plant', id: plantId, x: xw, y: wlw - 0.01, z: zw,
          yaw: rand(181) * Math.PI * 2, scale: 0.85 + rand(191) * 0.3,
          season, form: 'outcrop', type,
        };
      }
    }
  }

  // 5. 沉水带状草：苦草
  if (isRiver && depth > 0.25 && depth <= 2.2 && (isWetSoil || surface === Block.SAND || surface === Block.GRAVEL)) {
    if (rand(193) < 0.5) {
      return {
        kind: 'plant', id: 'shui.kucao', x: xw, y: yw - 0.04, z: zw,
        yaw: rand(197) * Math.PI * 2, scale: 0.85 + rand(199) * 0.3,
        season, form: 'outcrop', type,
      };
    }
  }

  // 6. 沉水羽叶草：狐尾藻
  if ((isWetland || isStillWater || isRiver) && depth > 0.25 && depth <= 1.8) {
    if (rand(211) < 0.42) {
      return {
        kind: 'plant', id: 'shui.huwei', x: xw, y: yw - 0.04, z: zw,
        yaw: rand(223) * Math.PI * 2, scale: 0.85 + rand(227) * 0.3,
        season, form: 'outcrop', type,
      };
    }
  }

  return null;
}

export function decorationPlacements(input: DecorationInput): DecorationPlacement[] {
  const { cx, cz, seed, type, temperature } = input;
  if (type === 0 || !allowsNaturalDecoration(input.artificialType ?? type)) return [];
  const ox = cx * CHUNK_SIZE, oz = cz * CHUNK_SIZE, padded = CHUNK_SIZE + 2;
  const at = (x: number, z: number) => (Math.floor(z) - oz + 1) * padded + Math.floor(x) - ox + 1;
  const height = (x: number, z: number) => input.heights[at(x, z)];
  const water = (x: number, z: number) => input.waterLevels[at(x, z)];
  const ground = (x: number, z: number) => terrainHeightAt(x, z, seed, height);
  const climate: ClimateZone = temperature >= 26 ? 'tropical' : temperature >= 18 ? 'subtropical' : temperature >= 0 ? 'temperate' : 'cold';
  const season: Season = temperature < 0 ? 'winter' : 'summer';
  const rockUnits = rocksForChunk(type);
  const out: DecorationPlacement[] = [];
  const isWaterBiome = isRiverType(type) || type === 11 || type === 105 || type === 2 || type % 100 === 2;

  for (let gz = 0; gz < CHUNK_SIZE; gz += 8) {
    for (let gx = 0; gx < CHUNK_SIZE; gx += 8) {
      const centerSurfIdx = (gz + 4) * CHUNK_SIZE + gx + 4;
      const centerRaw = centerSurfIdx < input.surfaces.length ? input.surfaces[centerSurfIdx] : Block.DIRT;
      const centerSurf = blockBase(centerRaw);
      if (type !== 0 && centerRaw !== Block.ICE && (isWaterBiome || centerSurf === Block.ALLUVIUM || centerSurf === Block.SILT)) {
        const saltW = seed ^ (0x9e377 + (gz * 8 + gx) * 31);
        const randW = (n: number) => hash2(ox + gx, oz + gz, saltW ^ n);
        const xw = ox + gx + 1 + randW(17) * 6;
        const zw = oz + gz + 1 + randW(29) * 6;
        const yw = ground(xw, zw);
        const wlw = waterHeightAt(xw, zw, seed, height, water);
        if (Number.isFinite(yw) && Number.isFinite(wlw)) {
          const surfIdx = (Math.floor(zw) - oz) * CHUNK_SIZE + Math.floor(xw) - ox;
          const surf = surfIdx >= 0 && surfIdx < input.surfaces.length ? blockBase(input.surfaces[surfIdx]) : Block.DIRT;
          const slopes = [[.75, 0], [-.75, 0], [0, .75], [0, -.75]].map(([dx, dz]) => Math.abs(ground(xw + dx, zw + dz) - yw));
          const aquatic = sampleAquaticDecoration(xw, zw, yw, wlw, surf, type, temperature, season, slopes, randW);
          if (aquatic) out.push(aquatic);
        }
      }

      for (let layer = 0; layer < 3; layer++) {
        const salt = seed ^ (0x4d321 + layer * 7919);
        const rand = (n: number) => hash2(ox + gx, oz + gz, salt ^ n);
        const x = ox + gx + 2 + rand(11) * 4, z = oz + gz + 2 + rand(23) * 4;
        const y = ground(x, z);
        if (!Number.isFinite(y)) continue;
        const wl = waterHeightAt(x, z, seed, height, water);
        if (wl > y - .05) continue;
        const slopes = [[.75, 0], [-.75, 0], [0, .75], [0, -.75]].map(([dx, dz]) => Math.abs(ground(x + dx, z + dz) - y));
        if (slopes.some(v => !Number.isFinite(v) || v > (layer === 2 ? 1.4 : .8))) continue;
        const surfaceRaw = input.surfaces[(Math.floor(z) - oz) * CHUNK_SIZE + Math.floor(x) - ox];
        if (surfaceRaw === Block.ICE) continue;
        const surface = blockBase(surfaceRaw);
        const size = .8 + rand(37) * .4;
        const common = { x, y: y - .06, z, yaw: rand(41) * Math.PI * 2, scale: size, season, form: forms[Math.floor(rand(53) * forms.length)], type };
        if (layer === 2) {
          const rocky = !SOILS.has(surface) && !SANDS.has(surface);
          if (!rockUnits.length || rand(67) > (rocky ? .55 : .16)) continue;
          const unit = rockUnits[Math.floor(rand(71) * rockUnits.length)];
          out.push({ ...common, kind: 'rock', id: unit.id, scale: size * (.65 + rand(79) * .65) });
          continue;
        }
        const snow = surface === Block.SNOW;
        if ((!SOILS.has(surface) && !SANDS.has(surface) && !snow) || temperature < -18) continue;
        const forest = surface === Block.FOREST_SOIL || surface === Block.PODZOL || type % 100 === 6 || type === 205 || type === 305 || type === 405 || type === 109;
        const density = layer === 0 ? (forest ? .72 : .17) : .65;
        const patch = .55 + hash2(Math.floor(x / 24), Math.floor(z / 24), seed ^ 0x7711) * .6;
        if (rand(67) > density * patch * (snow ? .3 : 1)) continue;
        const dry = SANDS.has(surface) || surface === Block.DRY_DIRT;
        const candidates = plants.filter(p => {
          const tags = p.tags ?? [];
          const tall = tags.includes('tree') || tags.includes('bamboo');
          return p.climate === climate && tall === (layer === 0) && !tags.includes('water')
            && (!dry || tags.includes('arid')) && (!snow || tags.includes('snow'))
            && (!tags.includes('riparian') || surface === Block.MUD || surface === Block.ALLUVIUM);
        });
        if (!candidates.length) continue;
        const plant = candidates[Math.floor(rand(71) * candidates.length)];
        out.push({ ...common, kind: 'plant', id: plant.id });
      }
    }
  }
  return out;
}
