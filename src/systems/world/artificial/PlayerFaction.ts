import { renName } from '../../../entities/PlayerIdentity.ts';
import type { ArtificialWorld } from './ArtificialWorld.ts';

export interface PlayerFactionOptions {
  spawnAsFlagLand: boolean;
  name: string;
  mainColor: string;
  trimColor: string;
}
export const DEFAULT_PLAYER_FACTION: Readonly<PlayerFactionOptions> = Object.freeze({
  spawnAsFlagLand: true, name: '缀红', mainColor: '#CB3A56', trimColor: '#5d3f51',
});

export function hexColor(value: unknown): string {
  if (typeof value !== 'string' || !/^#(?:[0-9a-f]{3}|[0-9a-f]{6})$/i.test(value.trim())) throw new Error('颜色需为 #RGB 或 #RRGGBB。');
  const hex = value.trim().slice(1);
  return '#' + (hex.length === 3 ? [...hex].map(c => c + c).join('') : hex);
}

export function playerFactionOptions(value: unknown): PlayerFactionOptions {
  if (!value || typeof value !== 'object') throw new Error('己方阵营设置无效。');
  const v = value as PlayerFactionOptions;
  if (typeof v.spawnAsFlagLand !== 'boolean') throw new Error('己方出生设置无效。');
  return { spawnAsFlagLand: v.spawnAsFlagLand, name: renName(v.name, DEFAULT_PLAYER_FACTION.name),
    mainColor: hexColor(v.mainColor), trimColor: hexColor(v.trimColor) };
}

export function playerFlagSpawn(world: ArtificialWorld, options: PlayerFactionOptions): { cx: number; cz: number } {
  const spawn = world.terrain.findSpawnChunk();
  if (!world.terrain.generation.artificial?.enabled || !options.spawnAsFlagLand) return spawn;
  const g = world.generator;
  const accept = (cx: number, cz: number) => {
    cx = g.wrap(cx);
    if (!g.suitable(cx, cz)) return null;
    world.createPlayerFaction(cx, cz, options);
    return { cx, cz };
  };
  for (let radius = 0; radius <= 8; radius++) {
    for (let dz = -radius; dz <= radius; dz++) for (let dx = -radius; dx <= radius; dx++) {
      if (Math.max(Math.abs(dx), Math.abs(dz)) !== radius) continue;
      const result = accept(spawn.cx + dx, spawn.cz + dz);
      if (result) return result;
    }
  }
  const planet = world.terrain.generation.mode === 'planet', size = world.terrain.generation.planet.equatorChunks;
  const tx = Math.floor((spawn.cx + (planet ? size / 2 : 0)) / g.cellSize);
  const tz = Math.floor((spawn.cz + (planet ? size / 4 : 0)) / g.cellSize);
  for (let radius = 0; radius <= 4; radius++) {
    for (let dz = -radius; dz <= radius; dz++) for (let dx = -radius; dx <= radius; dx++) {
      if (Math.max(Math.abs(dx), Math.abs(dz)) !== radius) continue;
      const candidate = g.candidate(tx + dx, tz + dz);
      if (candidate) { const result = accept(candidate.cx, candidate.cz); if (result) return result; }
    }
  }
  throw new Error('未找到合适的己方置旗地，请更换种子或取消置旗地出生。');
}
