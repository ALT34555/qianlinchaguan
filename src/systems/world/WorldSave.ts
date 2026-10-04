import { GENERATOR_VERSION, normalizeClimateWeights, type ClimateWeights } from './WorldSettings';
import { WORLD_MAX_Y, WORLD_MIN_Y } from '../../core/config';

export interface PlayerPosition { x: number; y: number; z: number; yaw: number; pitch: number }
export interface WorldSave {
  generatorVersion: number;
  seed: number;
  climateWeights: ClimateWeights;
  calendarType: 'real' | 'yuan';
  unixMs?: number;
  player: PlayerPosition;
}

export function parseWorldSave(text: string): WorldSave {
  const data = JSON.parse(text);
  if (!data || data.generatorVersion !== GENERATOR_VERSION) {
    throw new Error('此存档使用旧版或不支持的生成算法。请使用其中的种子新建世界；新版地形无法还原旧版位置。');
  }
  if (!Number.isInteger(data.seed) || data.seed < 0 || data.seed > 0xffffffff) throw new Error('存档种子无效。');
  const climateWeights = normalizeClimateWeights(data.climateWeights);
  const p = data.player;
  if (!p || !['x', 'y', 'z', 'yaw', 'pitch'].every(k => typeof p[k] === 'number' && Number.isFinite(p[k])) ||
      Math.abs(p.x) > 10000000 || Math.abs(p.z) > 10000000 || p.y < WORLD_MIN_Y || p.y > WORLD_MAX_Y ||
      Math.abs(p.pitch) > Math.PI / 2) throw new Error('存档中的玩家位置无效。');
  return { 
    generatorVersion: GENERATOR_VERSION, 
    seed: data.seed, 
    climateWeights,
    calendarType: data.calendarType === 'yuan' ? 'yuan' : 'real',
    unixMs: typeof data.unixMs === 'number' ? data.unixMs : undefined,
    player: { x: p.x, y: p.y, z: p.z, yaw: p.yaw, pitch: p.pitch } 
  };
}
