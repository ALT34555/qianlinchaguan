import { CHUNK_SIZE } from '../../core/config';
import { hash2 } from '../../core/math/Random';
import { Block, blockBase } from './Blocks';
import type { MeshData } from './ChunkMesher';
import { terrainHeightAt } from './TerrainSurface';
import { waterHeightAt } from './WaterSurface';
import { buildPlantById, scatterablePlants } from './vegetation/Plants';
import type { ClimateZone, Season } from './vegetation/types';
import { buildRockById, rocksForChunk } from './rocks/Rocks';
import type { RockForm } from './rocks/types';
import { allowsNaturalDecoration } from './artificial/ArtificialState';

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
}

const SOILS = new Set<number>([Block.GRASS, Block.FOREST_SOIL, Block.DIRT, Block.DRY_DIRT,
  Block.MUD, Block.CLAY, Block.SILT, Block.LOESS, Block.PEAT, Block.LATERITE,
  Block.MOSS_SOIL, Block.PODZOL, Block.ALLUVIUM, Block.GLACIAL_TILL]);
const SANDS = new Set<number>([Block.SAND, Block.RED_SAND, Block.DARK_SAND]);
const plants = scatterablePlants().filter(p => !(p.tags ?? []).some(t => ['farm', 'crop', 'orchard'].includes(t)));
const forms: RockForm[] = ['outcrop', 'buried', 'buried', 'crusted', 'stacked'];
const cache = new Map<string, MeshData>();

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
  for (let gz = 0; gz < CHUNK_SIZE; gz += 8) {
    for (let gx = 0; gx < CHUNK_SIZE; gx += 8) {
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
        const surface = blockBase(input.surfaces[(Math.floor(z) - oz) * CHUNK_SIZE + Math.floor(x) - ox]);
        const size = .8 + rand(37) * .4;
        const common = { x, y: y - .06, z, yaw: rand(41) * Math.PI * 2, scale: size, season, form: forms[Math.floor(rand(53) * forms.length)] };
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

export function buildDecorationMesh(input: DecorationInput, placements = decorationPlacements(input)): MeshData | null {
  const pieces = placements.map(p => {
    const key = `${p.kind}:${p.id}:${p.season}:${p.kind === 'rock' ? `${p.form}:${input.type}` : ''}`;
    let geometry = cache.get(key);
    if (!geometry) {
      geometry = p.kind === 'plant' ? buildPlantById(p.id, p.season)
        : buildRockById(p.id, p.season, { form: p.form, chunkType: input.type });
      if (cache.size >= 192) cache.delete(cache.keys().next().value!);
      cache.set(key, geometry);
    }
    return { p, geometry };
  });
  if (!pieces.length) return null;
  const positions = new Float32Array(pieces.reduce((n, v) => n + v.geometry.positions.length, 0));
  const colors = new Uint8Array(positions.length);
  const indices = new Uint32Array(pieces.reduce((n, v) => n + v.geometry.indices.length, 0));
  let vertexOffset = 0, indexOffset = 0;
  for (const { p, geometry } of pieces) {
    const c = Math.cos(p.yaw), s = Math.sin(p.yaw);
    for (let i = 0; i < geometry.positions.length; i += 3) {
      const x = geometry.positions[i] * p.scale, z = geometry.positions[i + 2] * p.scale;
      positions[vertexOffset * 3 + i] = p.x - input.cx * CHUNK_SIZE + x * c + z * s;
      positions[vertexOffset * 3 + i + 1] = p.y + geometry.positions[i + 1] * p.scale;
      positions[vertexOffset * 3 + i + 2] = p.z - input.cz * CHUNK_SIZE - x * s + z * c;
    }
    colors.set(geometry.colors, vertexOffset * 3);
    for (const index of geometry.indices) indices[indexOffset++] = index + vertexOffset;
    vertexOffset += geometry.positions.length / 3;
  }
  return { positions, colors, indices };
}
