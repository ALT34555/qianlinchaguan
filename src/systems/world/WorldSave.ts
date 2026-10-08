import { renName } from '../../entities/PlayerIdentity.ts';
import { playerFactionOptions, type PlayerFactionOptions } from './artificial/PlayerFaction.ts';
import { normalizeClimateWeights, normalizeGeneration, type ClimateWeights, type WorldGeneration } from './WorldSettings.ts';
import { GENERATOR_VERSION, isSupportedGeneratorVersion } from '../../core/version.ts';
import { CHUNK_SIZE, WORLD_MAX_Y, WORLD_MIN_Y } from '../../core/config.ts';
import { DAY_MS, parseArtificialState, type ArtificialState } from './artificial/ArtificialState.ts';
import { parsePlayerState, type PlayerState } from '../../entities/PlayerState.ts';
import { parseMysterySettings, type MysterySettings } from '../../core/MysterySettings.ts';

export interface PlayerPosition { x: number; y: number; z: number; yaw: number; pitch: number }
export interface WorldSave {
  playerState?: PlayerState;
  mystery?: MysterySettings;
  ren?: '人';
  ren_ming?: string;
  playerFaction?: PlayerFactionOptions;
  artificialState?: ArtificialState;
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
  if (!data || !isSupportedGeneratorVersion(data.generatorVersion)) {
    throw new Error('此存档使用旧版或不支持的生成算法。请使用其中的种子新建世界；新版地形无法还原旧版位置。');
  }
  if (!Number.isInteger(data.seed) || data.seed < 0 || data.seed > 0xffffffff) throw new Error('存档种子无效。');
  if (data.calendarType !== undefined && data.calendarType !== 'real' && data.calendarType !== 'yuan') throw new Error('存档中的历法无效。');
  const climateWeights = normalizeClimateWeights(data.climateWeights);
  if (!data.generation) throw new Error('存档缺少地形生成规则。');
  const generation = normalizeGeneration(data.generation);
  const artificialState = parseArtificialState(data.artificialState);
  if (data.ren !== undefined && data.ren !== '人') throw new Error('玩家身份无效。');
  const owned = artificialState?.territories.find(t => t.id === artificialState.playerFactionId);
  const playerState = parsePlayerState(data.playerState ?? { name: renName(data.ren_ming),
    factions: owned ? [{ id: owned.id, relationship: 'owned', mainColor: owned.mainColor, trimColor: owned.trimColor }] : [],
    ...(owned ? { activeFactionId: owned.id } : {}) });
  const ren_ming = playerState.name;
  const mystery = parseMysterySettings(data.mystery);
  const playerFaction = data.playerFaction === undefined ? undefined : playerFactionOptions(data.playerFaction);
  if (artificialState && !generation.artificial?.enabled) throw new Error('无人世界不能包含人类活动信息。');
  if (playerState.factions.some(f => !artificialState?.territories.some(t => t.id === f.id))) throw new Error('玩家阵营在世界中不存在。');
  const p = data.player;
  if (data.unixMs !== undefined && (typeof data.unixMs !== 'number' || !Number.isFinite(data.unixMs) || Math.abs(data.unixMs) > 8640000000000000)) throw new Error('存档中的日期无效。');
  if (data.utcOffsetMinutes !== undefined && (!Number.isInteger(data.utcOffsetMinutes) || Math.abs(data.utcOffsetMinutes) > 840)) throw new Error('存档中的时区无效。');
  if (artificialState) {
    const offset = (data.utcOffsetMinutes ?? 480) * 60000;
    if (typeof data.unixMs !== 'number' || artificialState.epochUnixMs > data.unixMs ||
        artificialState.lastDay < Math.floor((artificialState.epochUnixMs + offset) / DAY_MS) ||
        artificialState.lastDay > Math.floor((data.unixMs + offset) / DAY_MS)) throw new Error('存档中的领地时间无效。');
    if (generation.mode === 'planet') {
      const size = generation.planet.equatorChunks;
      if (artificialState.claims.some(c => c.cx < -size / 2 || c.cx >= size / 2 || c.cz < -size / 4 || c.cz >= size / 4)) {
        throw new Error('存档中的领地超出星球范围。');
      }
    }
  }
  if (data.worldName !== undefined && (typeof data.worldName !== 'string' || data.worldName.length > 80)) throw new Error('存档名称无效。');
  if (data.flying !== undefined && typeof data.flying !== 'boolean') throw new Error('存档中的浮空状态无效。');
  if (!p || !['x', 'y', 'z', 'yaw', 'pitch'].every(k => typeof p[k] === 'number' && Number.isFinite(p[k])) ||
      Math.abs(p.x) > 10000000 || Math.abs(p.z) > 10000000 || p.y < WORLD_MIN_Y || p.y > WORLD_MAX_Y ||
      Math.abs(p.pitch) > Math.PI / 2) throw new Error('存档中的玩家位置无效。');
  if (generation.mode === 'planet' && Math.abs(p.z) > generation.planet.equatorChunks * CHUNK_SIZE / 4) {
    throw new Error('存档位置超出星球南北极范围。');
  }
  return { 
    ren: '人', ren_ming, playerState, mystery, ...(playerFaction ? { playerFaction } : {}),
    generatorVersion: GENERATOR_VERSION, 
    seed: data.seed, 
    climateWeights,
    generation,
    ...(artificialState ? { artificialState } : {}),
    calendarType: data.calendarType === 'yuan' ? 'yuan' : 'real',
    unixMs: typeof data.unixMs === 'number' ? data.unixMs : undefined,
    player: { x: p.x, y: p.y, z: p.z, yaw: p.yaw, pitch: p.pitch },
    ...(data.utcOffsetMinutes !== undefined ? { utcOffsetMinutes: data.utcOffsetMinutes } : {}),
    ...(data.worldName !== undefined ? { worldName: data.worldName } : {}),
    ...(data.flying !== undefined ? { flying: data.flying } : {}),
  };
}
