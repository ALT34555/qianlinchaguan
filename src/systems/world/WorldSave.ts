import { renName } from '../../entities/PlayerIdentity.ts';
import { playerProfile, type PlayerProfile } from '../../entities/PlayerProfile.ts';
import { playerFactionOptions, type PlayerFactionOptions } from './artificial/PlayerFaction.ts';
import { normalizeClimateWeights, normalizeGeneration, type ClimateWeights, type WorldGeneration } from './WorldSettings.ts';
import { CURRENT_RULE_ID, checkRuleId, type RuleVerdict } from '../../core/WorldRule.ts';
import { CHUNK_SIZE, WORLD_MAX_Y, WORLD_MIN_Y } from '../../core/config.ts';
import { DAY_MS, parseArtificialState, type ArtificialState } from './artificial/ArtificialState.ts';
import { parsePlayerState, type PlayerState } from '../../entities/PlayerState.ts';
import { parseMysterySettings, type MysterySettings } from '../../core/MysterySettings.ts';

import { parsePlayMode, parseCreationTimeSpeed, type PlayMode } from '../../core/PlayMode.ts';
import type { ExplorationSnapshot } from './Exploration.ts';
import { validWorldKey } from './WorldDataStore.ts';

export interface PlayerPosition { x: number; y: number; z: number; yaw: number; pitch: number }
export interface AtlasDisplayState {info:boolean;colors:boolean;relief:boolean;rainClouds:boolean}
export interface WorldSave {
  atlasDisplay?:AtlasDisplayState;
  worldKey?:string;
  exploration?:ExplorationSnapshot;
  playMode?: PlayMode;
  creationTimeSpeed?: number;
  playerState?: PlayerState;
  mystery?: MysterySettings;
  ren?: '人';
  ren_ming?: string;
  profile?: PlayerProfile;
  playerFaction?: PlayerFactionOptions;
  artificialState?: ArtificialState;
  /** 生成规则身份，单次抓取真值 */
  ruleVersion: string;
  seed: number;
  climateWeights: ClimateWeights;
  generation: WorldGeneration;
  calendarType: 'real' | 'yuan';
  unixMs?: number;
  utcOffsetMinutes?: number;
  /** 降水快照在世界数据存储中的键，缺省表示该世界没有降水 */
  weatherKey?: string;
  worldName?: string;
  flying?: boolean;
  player: PlayerPosition;
}

export function parseWorldSave(text: string): WorldSave {
  const result = parseWorldSaveDetailed(text);
  if (!result.save) throw new Error(result.verdict.kind === 'reject' ? result.verdict.reason : '存档解析失败。');
  return result.save;
}

/** 解析存档并判定版本降级，不抛错 */
export function parseWorldSaveDetailed(text: string): { save: WorldSave | null; verdict: RuleVerdict } {
  const defaultVerdict: RuleVerdict = { kind: 'reject', reason: '存档不是有效的 JSON 文件。' };
  let data: unknown;
  try { data = JSON.parse(text); }
  catch { return { save: null, verdict: defaultVerdict }; }
  const verdict = checkRuleId((data as {ruleVersion?:unknown})?.ruleVersion);
  if (verdict.kind === 'reject') return { save: null, verdict };
  try { return { save: parseBody(data as Record<string, unknown>), verdict }; }
  catch (e) { return { save: null, verdict: { kind: 'reject', reason: e instanceof Error ? e.message : String(e) } }; }
}

function parseBody(data: any): WorldSave {
  if (!Number.isInteger(data.seed)) throw new Error('存档种子无效。');
  if(data.worldKey!==undefined&&(typeof data.worldKey!=='string'||!validWorldKey(data.worldKey)))throw new Error('世界存储编号无效。');
  let atlasDisplay:AtlasDisplayState|undefined;
  if(data.atlasDisplay!==undefined){
    const value=data.atlasDisplay;
    if(!value||typeof value!=='object'||Array.isArray(value))throw new Error('舆图显示设置无效。');
    const keys=['info','colors','relief','rainClouds'] as const;
    if(keys.some(k=>value[k]!==undefined&&typeof value[k]!=='boolean'))throw new Error('舆图显示开关无效。');    atlasDisplay={info:value.info??false,colors:value.colors??false,relief:value.relief??false,rainClouds:value.rainClouds??false};
  }
  const exploration=data.exploration as ExplorationSnapshot|undefined;
  if(data.weatherKey!==undefined&&(typeof data.weatherKey!=='string'||!/^weather[a-z]{8}$/.test(data.weatherKey)))throw new Error('存档中的降水编号无效。');
  if(exploration){
    if(!Array.isArray(exploration.regions)||exploration.regions.length>32768)throw new Error('探索记录无效。');
    const keys=new Set<string>();
    for(const r of exploration.regions){
      if(!Number.isInteger(r.x)||!Number.isInteger(r.z)||Math.abs(r.x)>1000000||Math.abs(r.z)>1000000||
        !Array.isArray(r.bits)||r.bits.length!==128||r.bits.some(v=>!Number.isInteger(v)||v<0||v>255)||keys.has(`${r.x},${r.z}`))throw new Error('探索区域无效。');
      keys.add(`${r.x},${r.z}`);
    }
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
  const playMode = parsePlayMode(data.playMode);
  const creationTimeSpeed = parseCreationTimeSpeed(data.creationTimeSpeed);
  const mystery = playMode === 'survival' ? { timeSpeed: creationTimeSpeed, menuAutoPause: false } : parseMysterySettings(data.mystery);
  const playerFaction = data.playerFaction === undefined ? undefined : playerFactionOptions(data.playerFaction);
  const profile = playerProfile(data.profile ?? { name: playerState.name, mainColor: playerFaction?.mainColor ?? owned?.mainColor, trimColor: playerFaction?.trimColor ?? owned?.trimColor }, playMode);
  playerState.name = profile.name;
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
  const save: WorldSave = { 
    ...(data.worldKey?{worldKey:data.worldKey}:{}),...(exploration?{exploration}:{}),
    playMode, creationTimeSpeed,
    ...(atlasDisplay?{atlasDisplay}:{}),
    ren: '人', ren_ming: profile.name, profile, playerState, mystery, ...(playerFaction ? { playerFaction } : {}),
    ruleVersion: CURRENT_RULE_ID,
    seed: data.seed, 
    climateWeights,
    generation,
    ...(artificialState ? { artificialState } : {}),
    calendarType: data.calendarType === 'yuan' ? 'yuan' : 'real',
    unixMs: typeof data.unixMs === 'number' ? data.unixMs : undefined,
    player: { x: p.x, y: p.y, z: p.z, yaw: p.yaw, pitch: p.pitch },
    ...(data.utcOffsetMinutes !== undefined ? { utcOffsetMinutes: data.utcOffsetMinutes } : {}),
    ...(typeof data.weatherKey === 'string' ? { weatherKey: data.weatherKey } : {}),
    ...(data.worldName !== undefined ? { worldName: data.worldName } : {}),
    ...(playMode === 'survival' ? { flying: false } : data.flying !== undefined ? { flying: data.flying } : {}),
  };
  return save;
}
