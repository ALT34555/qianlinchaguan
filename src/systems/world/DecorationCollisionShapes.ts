import type { MeshData } from './ChunkMesher';
import type { DecorationPlacement } from './WorldDecoration';
import { getPlantVariant, toPlantDef } from './vegetation/Plants';

export const COLLIDER_STRIDE = 7;
export const HARD_TRUNK = 0;
export const SOFT_SHRUB = 1;
export const HARD_ROCK = 2;
export const PLANT_TRIGGER = 3;
interface Shape { x: number; z: number; bottom: number; top: number; rx: number; rz: number; kind: number }
const shapes = new WeakMap<MeshData, Shape | null>();
const woody = new Set(['conifer', 'broadleaf', 'palm', 'acacia', 'baobab', 'blossomTree', 'bamboo', 'sapling']);

function shapeFor(p: DecorationPlacement, geometry: MeshData): Shape | null {
  if (shapes.has(geometry)) return shapes.get(geometry)!;
  let minX = Infinity, minY = Infinity, minZ = Infinity, maxX = -Infinity, maxY = -Infinity, maxZ = -Infinity;
  let shape: Shape | null = null;
  const variant = p.kind === 'plant' ? getPlantVariant(p.id) : undefined;
  const def = variant ? toPlantDef(variant, p.season) : undefined;
  if (p.kind === 'rock' || def) {
    for (let i = 0; i < geometry.positions.length; i += 3) {
      minX = Math.min(minX, geometry.positions[i]); maxX = Math.max(maxX, geometry.positions[i]);
      minY = Math.min(minY, geometry.positions[i + 1]); maxY = Math.max(maxY, geometry.positions[i + 1]);
      minZ = Math.min(minZ, geometry.positions[i + 2]); maxZ = Math.max(maxZ, geometry.positions[i + 2]);
    }
    if (p.kind === 'rock' || p.id.startsWith('guan.')) {
      const fraction = p.kind === 'rock' ? .5 : .35;
      shape = { x: (minX + maxX) / 2, z: (minZ + maxZ) / 2, bottom: minY, top: maxY,
        rx: (maxX - minX) * fraction, rz: (maxZ - minZ) * fraction, kind: p.kind === 'rock' ? HARD_ROCK : SOFT_SHRUB };
    } else if (def && woody.has(def.archetype) && !p.id.startsWith('shui.')) {
      const height = Number(def.params.height) || maxY;
      const trunk = Number(def.params.trunk) || height * .55;
      const radius = def.archetype === 'bamboo' ? Math.min(.6, (Number(def.params.spread) || .5) * .5 + .1) : (Number(def.params.trunkRadius) || height * .035) * 1.3;
      shape = { x: (Number(def.params.lean) || 0) * trunk * .125, z: 0, bottom: 0,
        top: Math.min(maxY, def.archetype === 'conifer' ? trunk + (height - trunk) * .7 : def.archetype === 'bamboo' ? height : trunk),
        rx: radius, rz: radius, kind: HARD_TRUNK };
    } else {
      const radius = Math.max(.08, Math.min(.6, Math.max(maxX - minX, maxZ - minZ) * .2));
      shape = { x: (minX + maxX) / 2, z: (minZ + maxZ) / 2, bottom: minY, top: Math.max(maxY, minY + .05), rx: radius, rz: radius, kind: PLANT_TRIGGER };
    }
  }
  shapes.set(geometry, shape);
  return shape;
}

export function appendDecorationCollider(out: number[], p: DecorationPlacement, geometry: MeshData): void {
  const shape = shapeFor(p, geometry);
  if (!shape) return;
  const c = Math.cos(p.yaw), s = Math.sin(p.yaw), scale = p.scale;
  const rx = shape.kind === HARD_ROCK ? Math.abs(c) * shape.rx + Math.abs(s) * shape.rz : Math.max(shape.rx, shape.rz);
  const rz = shape.kind === HARD_ROCK ? Math.abs(s) * shape.rx + Math.abs(c) * shape.rz : rx;
  out.push(p.x + (c * shape.x + s * shape.z) * scale, p.y + shape.bottom * scale,
    p.z + (-s * shape.x + c * shape.z) * scale, rx * scale, rz * scale, p.y + shape.top * scale, shape.kind);
}
