import { CHUNK_SIZE } from '../../core/config';
import { hash2 } from '../../core/math/Random';
import { Block, blockBase } from './Blocks';
import { terrainHeightAt, type HeightSampler } from './TerrainSurface';
import {patchWaterHeight} from './planet-macro/PlanetPatchWater';
import { waterHeightAt } from './WaterSurface';
import { scatterablePlants, buildPlantById } from './vegetation/Plants';
import type { ClimateZone } from './vegetation/types';
import type { Season } from './Season';
import { rocksForChunk } from './rocks/Rocks';
import type { RockForm } from './rocks/types';
import { allowsNaturalDecoration } from './artificial/ArtificialState';
import { isRiverType } from './ChunkTypes';
import { WaterKind, type WaterField } from './DynamicWater';
import {decorationHabitat,type DecorationEnvironmentSampler} from './DecorationHabitat';

export interface DecorationInput {
  environment?:DecorationEnvironmentSampler;
  springRocks?:{x:number;z:number;id:string;scale:number;yaw:number;form:RockForm}[];
  terrainBed?:HeightSampler;
  surfaceWater?:HeightSampler;
  macroSurface?:boolean;
  artificialType?: number;
  vegetationMultiplier?: number;
  cx: number;
  cz: number;
  type: number;
  temperature: number;
  seed: number;
  heights: Float32Array;
  waterLevels: Float32Array;
  surfaces: Uint32Array;
  waterField?: WaterField;
}

export function decorationSurfaceSamplers(cx:number,cz:number,heights:Float32Array,levels:Float32Array,
  beds:Float32Array|undefined,seed:number,macroSurface:boolean,
  column:(x:number,z:number)=>{bedLevel:number;level:number}) {
  const columns=new Map<string,ReturnType<typeof column>>();
  const sample=(x:number,z:number)=>{
    x=Math.floor(x);z=Math.floor(z);const key=`${x},${z}`;
    let value=columns.get(key);if(!value){value=column(x,z);columns.set(key,value);}return value;
  };
  const index=(x:number,z:number)=>{
    const ix=Math.floor(x)-cx*CHUNK_SIZE,iz=Math.floor(z)-cz*CHUNK_SIZE;
    return ix>=-1&&ix<=CHUNK_SIZE&&iz>=-1&&iz<=CHUNK_SIZE?(iz+1)*(CHUNK_SIZE+2)+ix+1:-1;
  };
  const bed=(x:number,z:number)=>{const i=index(x,z);return i<0?sample(x,z).bedLevel:(beds??heights)[i];};
  const liquid=(x:number,z:number)=>{const i=index(x,z);return i<0?sample(x,z).level:levels[i];};
  return {terrainBed:bed,macroSurface,
    surfaceWater:macroSurface?(x:number,z:number)=>patchWaterHeight(x,z,seed,bed,liquid):undefined};
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
export type PlantCategory = 'cao' | 'guan' | 'hua' | 'nong' | 'shu' | 'shui';
const LAND_CATEGORIES: readonly PlantCategory[] = ['shu', 'guan', 'cao'];
const COLD_FOREST_CATEGORIES: readonly PlantCategory[] = ['shu', 'guan'];
const WATER_CATEGORIES: readonly PlantCategory[] = ['shui'];
const EMPTY_CATEGORIES: readonly PlantCategory[] = [];
export function naturalPlantCategories(type: number): readonly PlantCategory[] {
  if (type === 0) return EMPTY_CATEGORIES;
  if (isRiverType(type) || type === 11 || type === 105 || type === 2 || type % 100 === 2) return WATER_CATEGORIES;
  return type === 405 ? COLD_FOREST_CATEGORIES : LAND_CATEGORIES;
}
const pools = new Map<string, typeof plants>();
const plantHeights = new Map<string, number>();
function plantTop(id: string, season: Season): number {
  const key = `${id}:${season}`;
  if (!plantHeights.has(key)) {
    const geometry = buildPlantById(id, season);
    let top = .05;
    for (let i = 1; i < geometry.positions.length; i += 3) top = Math.max(top, geometry.positions[i]);
    plantHeights.set(key, top);
  }
  return plantHeights.get(key)!;
}
for (const climate of ['tropical', 'subtropical', 'temperate', 'cold']) for (const category of LAND_CATEGORIES) for (let flags = 0; flags < 8; flags++) {
  pools.set(`${climate}:${category}:${flags}`, plants.filter(plant => {
    const tags = plant.tags ?? [];
    return plant.climate === climate && plant.id.startsWith(category + '.') && !tags.includes('water')
      && (!(flags & 1) || tags.includes('arid')) && (!(flags & 2) || tags.includes('snow'))
      && (!tags.includes('riparian') || !!(flags & 4));
  }));
}
export function naturalPlantScale(type: number, category: PlantCategory, id: string, size: number, rarity: number, variation: number, season: Season = 'summer'): number {
  if (id === 'cao.dixue') return (.18 + variation * .16) / plantTop(id, season);
  if (category === 'shu' && type === 106) return rarity < .08 ? 1.55 + variation * .5 : .8 + variation * .65;
  if (category === 'cao' && (type === 7 || type % 100 === 7) && rarity < .2) return Math.min(4, (1.46 + variation * .4) / plantTop(id, season));
  if (category === 'cao') return .55 + variation * .7;
  if (category === 'guan') return .65 + variation * .65;
  return size;
}
const forms: RockForm[] = ['outcrop', 'buried', 'buried', 'crusted', 'stacked'];

function vegetationPatch(x: number, z: number, seed: number): number {
  const noise = (scale: number, salt: number) => {
    const u = x / scale, v = z / scale, ix = Math.floor(u), iz = Math.floor(v);
    const smooth = (n: number) => n * n * (3 - 2 * n);
    const tx = smooth(u - ix), tz = smooth(v - iz);
    const a = hash2(ix, iz, seed ^ salt), b = hash2(ix + 1, iz, seed ^ salt);
    const c = hash2(ix, iz + 1, seed ^ salt), d = hash2(ix + 1, iz + 1, seed ^ salt);
    return (a + (b - a) * tx) * (1 - tz) + (c + (d - c) * tx) * tz;
  };
  return .2 + 1.3 * (.65 * noise(19, 0x7711) + .35 * noise(43, 0x8917));
}

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

  // 芦苇荡：适温湿地与内陆静水岸
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

  return null;
}

export function decorationPlacements(input: DecorationInput): DecorationPlacement[] {
  const { cx, cz, seed, type, temperature } = input;
  if (type === 0 || !allowsNaturalDecoration(input.artificialType ?? type)) return [];
  const ox = cx * CHUNK_SIZE, oz = cz * CHUNK_SIZE, padded = CHUNK_SIZE + 2;
  const at=(x:number,z:number)=>{
    const ix=Math.floor(x)-ox,iz=Math.floor(z)-oz;
    return ix>=-1&&ix<=CHUNK_SIZE&&iz>=-1&&iz<=CHUNK_SIZE?(iz+1)*padded+ix+1:-1;
  };
  const height=input.terrainBed??((x:number,z:number)=>{
    const i=at(x,z);return i>=0?(input.waterField?.bedLevels?.[i]??input.heights[i]):NaN;
  });
  const water=(x:number,z:number)=>{const i=at(x,z);return i>=0?input.waterLevels[i]:-Infinity;};
  const iced = (x:number,z:number) => (input.waterField?.iceCoverages?.[at(x,z)]??0)>=.5;
  const shallow = input.waterField ? (x:number,z:number) => {
    const i=at(x,z);return input.waterField!.kinds[i]===WaterKind.STATIC&&!(input.waterField!.iceCoverages?.[i]!>=.5);
  } : undefined;
  const ground = (x: number, z: number) => terrainHeightAt(x, z, seed, height);
  const surfaceWater=input.surfaceWater??(input.macroSurface
    ?(x:number,z:number)=>patchWaterHeight(x,z,seed,height,water)
    :(x:number,z:number)=>waterHeightAt(x,z,seed,height,water,true,shallow));
  const slopeGround = input.terrainBed?ground:(x: number, z: number) => ground(Math.max(ox, Math.min(ox + CHUNK_SIZE, x)), Math.max(oz, Math.min(oz + CHUNK_SIZE, z)));
  const habitat=(x:number,z:number,slope:number)=>decorationHabitat(x,z,seed,slope,input.environment?.(x,z)??{moisture:.55,activity:0});
  const climate: ClimateZone = temperature >= 26 ? 'tropical' : temperature >= 18 ? 'subtropical' : temperature >= 0 ? 'temperate' : 'cold';
  const season: Season = temperature < 0 ? 'winter' : 'summer';
  const rockUnits = rocksForChunk(type);
  const out: DecorationPlacement[] = [];
  for(const site of input.springRocks??[]){
    const y=ground(site.x,site.z),wl=surfaceWater(site.x,site.z);
    if(!Number.isFinite(y)||Number.isFinite(wl)&&wl-y>Math.max(.25,site.scale*.45))continue;
    out.push({...site,y:y-.12*site.scale,kind:'rock',season,type});
  }
  const categories = naturalPlantCategories(type);
  const isWaterBiome = categories === WATER_CATEGORIES;
  const hasHerbs = categories.includes('cao');
  const emptyPool: typeof plants = [];
  const multiplier = Math.max(1, Math.min(16, input.vegetationMultiplier ?? 1));

  for (let gz = 0; gz < CHUNK_SIZE; gz += 8) {
    for (let gx = 0; gx < CHUNK_SIZE; gx += 8) {
      for (let pass = 0; pass < Math.ceil(multiplier); pass++) {
        if (pass > 0 && hash2(ox + gx, oz + gz, seed ^ (pass * 104729)) >= multiplier - pass) continue;
        const centerSurfIdx = (gz + 4) * CHUNK_SIZE + gx + 4;
        const centerRaw = centerSurfIdx < input.surfaces.length ? input.surfaces[centerSurfIdx] : Block.DIRT;
        if (isWaterBiome && (pass > 0 || centerRaw !== Block.ICE)) {
          const saltW = seed ^ (0x9e377 + (gz * 8 + gx) * 31 + pass * 104729);
          const randW = (n: number) => hash2(ox + gx, oz + gz, saltW ^ n);
          const xw = ox + (pass === 0 ? gx + 1 + randW(17) * 6 : randW(17) * CHUNK_SIZE);
          const zw = oz + (pass === 0 ? gz + 1 + randW(29) * 6 : randW(29) * CHUNK_SIZE);
          const yw = ground(xw, zw);
          const wlw = surfaceWater(xw,zw);
          if (Number.isFinite(yw) && Number.isFinite(wlw)) {
            const surfIdx = (Math.floor(zw) - oz) * CHUNK_SIZE + Math.floor(xw) - ox;
            if (iced(xw,zw) || (pass > 0 && input.surfaces[surfIdx] === Block.ICE)) continue;
            const surf = surfIdx >= 0 && surfIdx < input.surfaces.length ? blockBase(input.surfaces[surfIdx]) : Block.DIRT;
            const slopes = [[.75, 0], [-.75, 0], [0, .75], [0, -.75]].map(([dx, dz]) => Math.abs(slopeGround(xw + dx, zw + dz) - yw));
            const aquatic = sampleAquaticDecoration(xw, zw, yw, wlw, surf, type, temperature, season, slopes, randW);
            if (aquatic&&(!input.macroSurface||randW(211)<.55*habitat(xw,zw,0).understory)) out.push(aquatic);
          }
        }
        if (isWaterBiome) continue;

        for (let layer = 0; layer < 3; layer++) {
          if (pass > 0 && layer !== 1) continue;
          const salt = seed ^ (0x4d321 + layer * 7919 + pass * 104729);
          const rand = (n: number) => hash2(ox + gx, oz + gz, salt ^ n);
          const x = ox + (pass === 0 ? gx + 2 + rand(11) * 4 : rand(11) * CHUNK_SIZE);
          const z = oz + (pass === 0 ? gz + 2 + rand(23) * 4 : rand(23) * CHUNK_SIZE);
          const y = ground(x, z);
          if (!Number.isFinite(y)) continue;
          const wl = surfaceWater(x,z);
          if (wl > y - .05) continue;
          const slopes = [[.75, 0], [-.75, 0], [0, .75], [0, -.75]].map(([dx, dz]) => Math.abs(slopeGround(x + dx, z + dz) - y));
          if (slopes.some(v => !Number.isFinite(v))) continue;
          const slope=Math.max(...slopes)/.75;
          if(input.macroSurface?slope>(layer===2?3.5:1.65):slopes.some(v=>v>(layer===2?1.4:.8)))continue;
          const local=input.macroSurface?habitat(x,z,slope):null;
          const surfaceRaw = input.surfaces[(Math.floor(z) - oz) * CHUNK_SIZE + Math.floor(x) - ox];
          if (surfaceRaw === Block.ICE || iced(x,z)) continue;
          const surface = blockBase(surfaceRaw);
          const size = .8 + rand(37) * .4;
          const common = { x, y: y - .06, z, yaw: rand(41) * Math.PI * 2, scale: size, season, form: forms[Math.floor(rand(53) * forms.length)], type };
          if (layer === 2) {
            const rocky = !SOILS.has(surface) && !SANDS.has(surface);
            const chance=local?(rocky?.45:.10+local.exposed*.25)*local.rocks:(rocky?.55:.16);
            if (!rockUnits.length || rand(67) > chance) continue;
            const preferred=local?rockUnits.filter(v=>local.exposed>.5?['rock.ledge','rock.slab','rock.block'].includes(v.id):['rock.rubble','rock.boulder','rock.stub'].includes(v.id)):[];
            const units=preferred.length?preferred:rockUnits,unit=units[Math.floor(rand(71)*units.length)];
            const scale=local?size*(.5+rand(79)*1.1+local.exposed*.8):size*(.65+rand(79)*.65);
            const form=local?(local.exposed>.5?'outcrop':rand(53)<.7?'buried':'stacked'):common.form;
            out.push({ ...common, kind:'rock',id:unit.id,scale,form,y:local?y-scale*(.12+local.exposed*.15):common.y });
            continue;
          }
          const snow = surface === Block.SNOW;
          if ((!SOILS.has(surface) && !SANDS.has(surface) && !snow) || temperature < -18) continue;
          const forest = surface === Block.FOREST_SOIL || surface === Block.PODZOL || !input.macroSurface&&(type % 100 === 6 || type === 205 || type === 305 || type === 405 || type === 109);
          const density = local?(layer===0?(forest?.46:.30)*local.trees:.48*local.understory):layer === 0 ? (forest ? .72 : .17) : .65;
          const patch = !local&&pass===0?.55+hash2(Math.floor(x/24),Math.floor(z/24),seed^0x7711)*.6:vegetationPatch(x,z,seed);
          if (rand(67) > density * patch * (snow ? .3 : 1)) continue;
          const dry = SANDS.has(surface) || surface === Block.DRY_DIRT;
          const flags = Number(dry) | (Number(snow) << 1) | (Number(surface === Block.MUD || surface === Block.ALLUVIUM) << 2);
          let category: PlantCategory = 'shu';
          if (layer === 1) {
            const shrubs = pools.get(`${climate}:guan:${flags}`)!;
            const herbs = hasHerbs ? pools.get(`${climate}:cao:${flags}`)! : emptyPool;
            const herbShare = type === 7 || type % 100 === 7 || [101, 201, 301, 401].includes(type) ? .75 : .4;
            category = herbs.length && (!shrubs.length || rand(73) < herbShare) ? 'cao' : 'guan';
          }
          const candidates = pools.get(`${climate}:${category}:${flags}`)!;
          if (!candidates.length) continue;
          const plant = candidates[Math.floor(rand(71) * candidates.length)];
          out.push({ ...common, kind: 'plant', id: plant.id, scale: naturalPlantScale(type, category, plant.id, size, rand(83), rand(89), season) });
        }
      }
    }
  }
  return out;
}
