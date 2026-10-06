/** 旗帜落位与锚点 */

import { CHUNK_SIZE, SEA_LEVEL } from '../../../core/config';
import { hash2 } from '../../../core/math/Random';
import type { FlagPlacement, FlagpoleKind, SurfaceQuery, V3 } from './types';

export type { SurfaceQuery } from './types';

export type FlagAnchorMode = 'ground' | 'wall' | 'hand';

export function poleAnchorOf(kind: FlagpoleKind): FlagAnchorMode {
  if (kind === 'horizontal') return 'wall';
  if (kind === 'stick') return 'hand';
  return 'ground';
}

export const WALL_FACINGS: Readonly<Record<string, number>> = Object.freeze({
  east: 0,
  south: Math.PI / 2,
  west: Math.PI,
  north: -Math.PI / 2,
});

export function wallYawOf(facing: string): number {
  const yaw = WALL_FACINGS[facing];
  if (yaw === undefined) {
    throw new Error(`[Flags] 未知墙面朝向: ${facing}（可用: ${Object.keys(WALL_FACINGS).join(', ')}）`);
  }
  return yaw;
}

export function canDeployFlag(q: SurfaceQuery, x: number, z: number, maxSlope = 2): boolean {
  const h = q.height(x, z);
  if (!Number.isFinite(h)) return false;
  const wl = q.waterLevel(x, z);
  if (Number.isFinite(wl) && h < wl) return false;
  const surface = q.surface(x, z);
  if (surface === 0) return false;
  let maxDelta = 0;
  for (const [dx, dz] of [[1, 0], [-1, 0], [0, 1], [0, -1]] as const) {
    const nh = q.height(x + dx, z + dz);
    if (!Number.isFinite(nh)) return false;
    const d = Math.abs(nh - h);
    if (d > maxDelta) maxDelta = d;
  }
  return maxDelta <= maxSlope;
}

export function groundAnchor(q: SurfaceQuery, x: number, z: number): V3 {
  const h = q.height(x, z);
  if (!Number.isFinite(h)) throw new Error(`[Flags] (${x}, ${z}) 的地表未加载，无法立杆`);
  return [x, h, z];
}

export interface FlagAnchor {
  kind: FlagpoleKind;
  mode: FlagAnchorMode;
  origin: V3;
  yaw: number;
}

export function anchorOf(placement: FlagPlacement, kind: FlagpoleKind, q?: SurfaceQuery): FlagAnchor {
  const mode = poleAnchorOf(kind);
  let y = placement.y;
  if (mode === 'ground' && q) y = groundAnchor(q, placement.x, placement.z)[1];
  return { kind, mode, origin: [placement.x, y, placement.z], yaw: placement.yaw };
}

export function snapPlacement(q: SurfaceQuery, placement: FlagPlacement): FlagPlacement {
  const h = q.height(placement.x, placement.z);
  return { ...placement, y: Number.isFinite(h) ? h : placement.y };
}

export function makePlacement(input: Partial<FlagPlacement> & { x: number; z: number; pole: string; flag: string }): FlagPlacement {
  const { x, z } = input;
  return {
    x,
    z,
    y: input.y ?? SEA_LEVEL,
    pole: input.pole,
    flag: input.flag,
    mount: input.mount ?? 'masthead',
    yaw: input.yaw ?? hash2(x, z, 0x51ed2701) * Math.PI * 2,
    scale: input.scale ?? 1,
    wind: input.wind ?? 0.75,
    shapeSeed: input.shapeSeed ?? Math.floor(hash2(x, z, 0x1b873593) * 0xffffffff) >>> 0,
  };
}

export function deployQuery(
  heights: Int16Array | Float32Array,
  surfaces: Uint32Array | Uint16Array | Uint8Array,
  waterLevels: Float32Array,
  paddedSize: number,
  originX: number,
  originZ: number,
): SurfaceQuery {
  const at = (x: number, z: number): number => {
    const lx = x - originX;
    const lz = z - originZ;
    if (lx < 0 || lz < 0 || lx >= paddedSize || lz >= paddedSize) return -1;
    return lz * paddedSize + lx;
  };
  return {
    height: (x, z) => {
      const i = at(x, z);
      return i < 0 ? Number.NaN : heights[i];
    },
    waterLevel: (x, z) => {
      const i = at(x, z);
      return i < 0 ? Number.NaN : (waterLevels[i] ?? SEA_LEVEL);
    },
    surface: (x, z) => {
      const i = at(x, z);
      return i < 0 ? 0 : surfaces[i];
    },
  };
}

export function chunkOfPlacement(placement: FlagPlacement): { cx: number; cz: number } {
  return {
    cx: Math.floor(placement.x / CHUNK_SIZE),
    cz: Math.floor(placement.z / CHUNK_SIZE),
  };
}
