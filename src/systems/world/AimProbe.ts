import type { ChunkWaterAppearance } from './WaterAppearance';
/** 准心检视探针：模型 / 领地旗帜 / 河流 */
import { CHUNK_SIZE } from '../../core/config';
import { Block } from './Blocks';
import { isRiverType } from './ChunkTypes';
import { getRockDef, buildRockById } from './rocks/Rocks';
import type { RockForm } from './rocks/types';
import type { Season } from './Season';
import { baseIdOf, buildPlantById, getPlantVariant, toPlantDef } from './vegetation/Plants';
import { PLANT_STATES, type PlantState } from './vegetation/types';
import type { DecorationPlacement } from './WorldDecoration';
import { FLOW_DIRECTIONS, type ChunkInfo } from './WorldContract';
import type { Territory } from './artificial/ArtificialState';

/** 准心最远检视距离 */
export const AIM_DISTANCE = 256;
const AIM_STEP = .5;
const AIM_CEILING = 512;
const BANDS = 8;
const EXTENT_LIMIT = 96;
const EPSILON = 1e-12;

export type ModelKind = DecorationPlacement['kind'];

export interface AimFlagBounds {
  minX: number; minY: number; minZ: number;
  maxX: number; maxY: number; maxZ: number;
}

export interface AimChunk {
  cx: number;
  cz: number;
  surfaces: Uint32Array;
  waterField?: { appearance?: ChunkWaterAppearance };
  decorations: readonly DecorationPlacement[];
  flagBounds: AimFlagBounds | null;
}

export interface AimWorld {
  getHeight(wx: number, wz: number): number;
  getWaterLevel(wx: number, wz: number): number;
  getChunkType(cx: number, cz: number): number;
  getChunk(cx: number, cz: number): AimChunk | undefined;
  readonly generator: { generation?:{terrainRule?:string}; getChunkInfo(cx: number, cz: number): ChunkInfo; displayFlowWanted: boolean; getChunkWaterAppearance?(cx: number, cz: number): ChunkWaterAppearance };
  readonly artificial: {
    getClaim(cx: number, cz: number): { faction: string } | undefined;
    readonly territories: ReadonlyMap<string, Territory>;
    readonly playerFactionId?: string;
  };
}

export interface AimRay {
  x: number; y: number; z: number;
  dx: number; dy: number; dz: number;
}

export interface AimSurface {
  kind: 'surface';
  surface: 'ground' | 'water';
  waterAppearance: ChunkWaterAppearance | null;
  chunkType: number;
  blockId: number | null;
  cx: number; cz: number;
  distance: number;
}

export interface AimModel {
  kind: 'model';
  model: ModelKind;
  id: string;
  name: string;
  modelState: PlantState | null;
  colorState: Season;
  cx: number; cz: number;
  distance: number;
}

export interface AimFlag {
  kind: 'flag';
  territory: Territory;
  player: boolean;
  cx: number; cz: number;
  distance: number;
}

export type AimHit = AimSurface | AimModel | AimFlag;

export interface AimRiver {
  discharge: number;
  direction: number;
  width: number;
  depth: number;
}

export interface AimResult {
  hit: AimHit | null;
  river: AimRiver | null;
}

interface ModelBand { y0: number; y1: number; radius: number }
interface ModelExtent { bands: readonly ModelBand[] }

const extents = new Map<string, ModelExtent>();

/** 模型名（含状态后缀的植物 id 归并到物种） */
export function modelName(kind: ModelKind, id: string): string {
  return kind === 'plant' ? getPlantVariant(id)?.name ?? id : getRockDef(id)?.name ?? id;
}

/** 流向文字：箭头 + 格偏移 */
export function flowLabel(direction: number): string {
  const d = FLOW_DIRECTIONS[direction];
  return d ? `${d.arrow} (${d.dx}, ${d.dz})` : '';
}

function modelExtent(kind: ModelKind, id: string, season: Season, form: RockForm, type: number): ModelExtent {
  const key = `${kind}:${id}:${season}:${kind === 'rock' ? `${form}:${type}` : ''}`;
  const cached = extents.get(key);
  if (cached) return cached;
  const { positions } = kind === 'plant' ? buildPlantById(id, season) : buildRockById(id, season, { form, chunkType: type });
  let minY = Infinity, maxY = -Infinity;
  for (let i = 1; i < positions.length; i += 3) {
    const y = positions[i];
    if (y < minY) minY = y;
    if (y > maxY) maxY = y;
  }
  if (!Number.isFinite(minY)) { minY = 0; maxY = 1; }
  const span = Math.max(maxY - minY, 1e-3);
  const bands: ModelBand[] = [];
  for (let b = 0; b < BANDS; b++) bands.push({ y0: minY + span * b / BANDS, y1: minY + span * (b + 1) / BANDS, radius: 0 });
  for (let i = 0; i < positions.length; i += 3) {
    const radius = Math.hypot(positions[i], positions[i + 2]);
    const band = bands[Math.min(BANDS - 1, Math.max(0, Math.floor((positions[i + 1] - minY) / span * BANDS)))];
    if (radius > band.radius) band.radius = radius;
  }
  const solid = bands.find(b => b.radius > 0);
  if (solid) {
    for (let b = 1; b < BANDS; b++) if (bands[b].radius === 0) bands[b].radius = bands[b - 1].radius;
    for (let b = 0; b < BANDS && bands[b].radius === 0; b++) bands[b].radius = solid.radius;
  }
  const extent: ModelExtent = { bands };
  if (extents.size >= EXTENT_LIMIT) extents.delete(extents.keys().next().value!);
  extents.set(key, extent);
  return extent;
}

/** 射线与竖直圆柱（按高度分层）的最早交距 */
function cylinderDistance(ray: AimRay, px: number, pz: number, radius: number,
  y0: number, y1: number, limit: number): number | null {
  const ox = ray.x - px, oz = ray.z - pz;
  let t0 = 0, t1 = limit;
  const a = ray.dx * ray.dx + ray.dz * ray.dz;
  if (a < EPSILON) {
    if (ox * ox + oz * oz > radius * radius) return null;
  } else {
    const b = 2 * (ox * ray.dx + oz * ray.dz);
    const c = ox * ox + oz * oz - radius * radius;
    const disc = b * b - 4 * a * c;
    if (disc < 0) return null;
    const root = Math.sqrt(disc);
    t0 = Math.max(t0, (-b - root) / (2 * a));
    t1 = Math.min(t1, (-b + root) / (2 * a));
    if (t0 > t1) return null;
  }
  if (Math.abs(ray.dy) < EPSILON) {
    if (ray.y < y0 || ray.y > y1) return null;
  } else {
    const ya = (y0 - ray.y) / ray.dy, yb = (y1 - ray.y) / ray.dy;
    t0 = Math.max(t0, Math.min(ya, yb));
    t1 = Math.min(t1, Math.max(ya, yb));
    if (t0 > t1) return null;
  }
  if (t1 < 0 || t0 > limit) return null;
  return Math.max(0, t0);
}

/** 射线与轴对齐盒的最早交距 */
function boxDistance(ray: AimRay, bounds: AimFlagBounds, limit: number): number | null {
  const axes: readonly (readonly [number, number, number, number])[] = [
    [ray.x, ray.dx, bounds.minX, bounds.maxX],
    [ray.y, ray.dy, bounds.minY, bounds.maxY],
    [ray.z, ray.dz, bounds.minZ, bounds.maxZ],
  ];
  let t0 = 0, t1 = limit;
  for (const [origin, direction, min, max] of axes) {
    if (Math.abs(direction) < EPSILON) {
      if (origin < min || origin > max) return null;
      continue;
    }
    const entry = (min - origin) / direction, exit = (max - origin) / direction;
    t0 = Math.max(t0, Math.min(entry, exit));
    t1 = Math.min(t1, Math.max(entry, exit));
    if (t0 > t1) return null;
  }
  if (t1 < 0 || t0 > limit) return null;
  return Math.max(0, t0);
}

function modelDistance(ray: AimRay, placement: DecorationPlacement, limit: number): number | null {
  const { bands } = modelExtent(placement.kind, placement.id, placement.season, placement.form, placement.type);
  let best = limit;
  let found: number | null = null;
  for (const band of bands) {
    const distance = cylinderDistance(ray, placement.x, placement.z, band.radius * placement.scale,
      placement.y + band.y0 * placement.scale, placement.y + band.y1 * placement.scale, best);
    if (distance !== null && distance <= best) { best = distance; found = distance; }
  }
  return found;
}

/** 命中点所在区块的河流流量与流向 */
function riverInfo(world: AimWorld, cx: number, cz: number): AimRiver | null {
  const generator = world.generator;
  let info = generator.getChunkInfo(cx, cz);
  if (info.displayFlow < 0 && info.flow < 0 && info.courseVal > 0 && info.type !== 11 && isRiverType(info.type,generator.generation?.terrainRule==='planet-macro')) {
    generator.displayFlowWanted = true;
    try { info = generator.getChunkInfo(cx, cz); }
    finally { generator.displayFlowWanted = false; }
  }
  const direction = info.displayFlow >= 0 ? info.displayFlow : info.flow;
  if (direction < 0 && !isRiverType(info.type,generator.generation?.terrainRule==='planet-macro') && info.riverWidth <= 0) return null;
  const appearance = world.getChunk(cx, cz)?.waterField?.appearance ?? generator.getChunkWaterAppearance?.(cx, cz);
  const scale = appearance?.flowScale ?? 1;
  return { discharge: info.discharge * scale, direction: scale === 0 ? -1 : direction, width: info.riverWidth, depth: info.riverDepth };
}

function nearest(model: AimModel | null, flag: AimFlag | null, surface: AimSurface | null): AimHit | null {
  let best: AimHit | null = surface;
  if (model && (!best || model.distance <= best.distance)) best = model;
  if (flag && (!best || flag.distance <= best.distance)) best = flag;
  return best;
}

/** 准心探针：返回最近命中与所在区块的河流信息 */
export function probeAim(world: AimWorld, ray: AimRay): AimResult {
  const visited = new Set<string>();
  const startKey = `${Math.floor(ray.x / CHUNK_SIZE)},${Math.floor(ray.z / CHUNK_SIZE)}`;
  visited.add(startKey);
  let surface: AimSurface | null = null;
  for (let d = AIM_STEP; d <= AIM_DISTANCE; d += AIM_STEP) {
    const x = ray.x + ray.dx * d, y = ray.y + ray.dy * d, z = ray.z + ray.dz * d;
    if (y > AIM_CEILING) break;
    const cx = Math.floor(x / CHUNK_SIZE), cz = Math.floor(z / CHUNK_SIZE);
    visited.add(`${cx},${cz}`);
    const ground = world.getHeight(x, z), water = world.getWaterLevel(x, z);
    if (y <= Math.max(ground, water)) {
      let near = Math.max(0, d - AIM_STEP), far = d;
      for (let i = 0; i < 12; i++) {
        const mid = (near + far) * .5;
        const mx = ray.x + ray.dx * mid, mz = ray.z + ray.dz * mid;
        if (ray.y + ray.dy * mid <= Math.max(world.getHeight(mx, mz), world.getWaterLevel(mx, mz))) far = mid;
        else near = mid;
      }
      const hx = ray.x + ray.dx * far, hz = ray.z + ray.dz * far;
      const hitCx = Math.floor(hx / CHUNK_SIZE), hitCz = Math.floor(hz / CHUNK_SIZE);
      const chunk = world.getChunk(hitCx, hitCz);
      const isWater = world.getWaterLevel(hx, hz) > world.getHeight(hx, hz);
      const index = (Math.floor(hz) - hitCz * CHUNK_SIZE) * CHUNK_SIZE + Math.floor(hx) - hitCx * CHUNK_SIZE;
      visited.add(`${hitCx},${hitCz}`);
      surface = { kind: 'surface', surface: isWater ? 'water' : 'ground',
        blockId: isWater ? Block.WATER : chunk?.surfaces[index] ?? null,
        waterAppearance: isWater || chunk?.surfaces[index] === Block.ICE
          ? chunk?.waterField?.appearance ?? world.generator.getChunkWaterAppearance?.(hitCx, hitCz) ?? null : null,
        chunkType: world.getChunkType(hitCx, hitCz), cx: hitCx, cz: hitCz, distance: far };
      break;
    }
  }

  const limit = surface ? surface.distance : AIM_DISTANCE;
  let model: AimModel | null = null;
  let flag: AimFlag | null = null;
  for (const key of visited) {
    const split = key.indexOf(',');
    const chunk = world.getChunk(Number(key.slice(0, split)), Number(key.slice(split + 1)));
    if (!chunk) continue;
    for (const placement of chunk.decorations) {
      const distance = modelDistance(ray, placement, model ? model.distance : limit);
      if (distance === null) continue;
      const variant = placement.kind === 'plant'
        ? getPlantVariant(placement.id) ?? getPlantVariant(baseIdOf(placement.id)) : undefined;
      const suffix = placement.id.slice(baseIdOf(placement.id).length).replace(/^@/, '') as PlantState;
      const state = PLANT_STATES.includes(suffix) ? suffix : 'normal';
      model = { kind: 'model', model: placement.kind, id: placement.id, name: modelName(placement.kind, placement.id),
        modelState: variant ? toPlantDef(variant, placement.season, { state }).state : null,
        colorState: placement.season,
        cx: chunk.cx, cz: chunk.cz, distance };
    }
    if (!chunk.flagBounds) continue;
    const distance = boxDistance(ray, chunk.flagBounds, flag ? flag.distance : limit);
    if (distance === null) continue;
    const claim = world.artificial.getClaim(chunk.cx, chunk.cz);
    const territory = claim ? world.artificial.territories.get(claim.faction) : undefined;
    if (!territory) continue;
    flag = { kind: 'flag', territory, player: world.artificial.playerFactionId === territory.id,
      cx: chunk.cx, cz: chunk.cz, distance };
  }

  const hit = nearest(model, flag, surface);
  return { hit, river: hit ? riverInfo(world, hit.cx, hit.cz) : null };
}
