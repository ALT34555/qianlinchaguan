import { GENERATOR_VERSION, normalizeClimateWeights, normalizeGeneration, type ClimateWeights, type WorldGeneration } from './WorldSettings.ts';
import { CHUNK_SIZE, WORLD_MAX_Y, WORLD_MIN_Y } from '../../core/config.ts';

export interface PlayerPosition { x: number; y: number; z: number; yaw: number; pitch: number }
export interface WorldSave {
  generatorVersion: number;
  seed: number;
  climateWeights: ClimateWeights;
  generation: WorldGeneration;
  calendarType: 'real' | 'yuan';
  unixMs?: number;
  utcOffsetMinutes?: number;
  worldName?: string;
  flying?: boolean;
  player: PlayerPosition;
}

export function parseWorldSave(text: string): WorldSave {
  const data = (() => {
    try { return JSON.parse(text); }
    catch { throw new Error('存档不是有效的 JSON 文件。'); }
  })();
  // 保留旧参数与观察位置。
  if (!data || ![GENERATOR_VERSION,14,15,16,17,18,19,20,21].includes(data.generatorVersion)) {
    throw new Error('此存档使用旧版或不支持的生成算法。请使用其中的种子新建世界；新版地形无法还原旧版位置。');
  }
  if (!Number.isInteger(data.seed) || data.seed < 0 || data.seed > 0xffffffff) throw new Error('存档种子无效。');
  if (data.calendarType !== undefined && data.calendarType !== 'real' && data.calendarType !== 'yuan') throw new Error('存档中的历法无效。');
  const climateWeights = normalizeClimateWeights(data.climateWeights);
  if (!data.generation) throw new Error('存档缺少地形生成规则。');
  const generation = normalizeGeneration(data.generation);
  const p = data.player;
  if (data.unixMs !== undefined && (typeof data.unixMs !== 'number' || !Number.isFinite(data.unixMs) || Math.abs(data.unixMs) > 8640000000000000)) throw new Error('存档中的日期无效。');
  if (data.utcOffsetMinutes !== undefined && (!Number.isInteger(data.utcOffsetMinutes) || Math.abs(data.utcOffsetMinutes) > 840)) throw new Error('存档中的时区无效。');
  if (data.worldName !== undefined && (typeof data.worldName !== 'string' || data.worldName.length > 80)) throw new Error('存档名称无效。');
  if (data.flying !== undefined && typeof data.flying !== 'boolean') throw new Error('存档中的浮空状态无效。');
  if (!p || !['x', 'y', 'z', 'yaw', 'pitch'].every(k => typeof p[k] === 'number' && Number.isFinite(p[k])) ||
      Math.abs(p.x) > 10000000 || Math.abs(p.z) > 10000000 || p.y < WORLD_MIN_Y || p.y > WORLD_MAX_Y ||
      Math.abs(p.pitch) > Math.PI / 2) throw new Error('存档中的玩家位置无效。');
  if (generation.mode === 'planet' && Math.abs(p.z) > generation.planet.equatorChunks * CHUNK_SIZE / 4) {
    throw new Error('存档位置超出星球南北极范围。');
  }
  return { 
    generatorVersion: GENERATOR_VERSION, 
    seed: data.seed, 
    climateWeights,
    generation,
    calendarType: data.calendarType === 'yuan' ? 'yuan' : 'real',
    unixMs: typeof data.unixMs === 'number' ? data.unixMs : undefined,
    player: { x: p.x, y: p.y, z: p.z, yaw: p.yaw, pitch: p.pitch },
    ...(data.utcOffsetMinutes !== undefined ? { utcOffsetMinutes: data.utcOffsetMinutes } : {}),
    ...(data.worldName !== undefined ? { worldName: data.worldName } : {}),
    ...(data.flying !== undefined ? { flying: data.flying } : {}),
  };
}
