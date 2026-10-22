import type { PlayMode } from '../../core/PlayMode';
import type { PlayerProfile } from '../../entities/PlayerProfile';
import type { PlayerFactionOptions } from '../../systems/world/artificial/PlayerFaction';
import type { ClimateWeights, WorldGeneration } from '../../systems/world/WorldSettings';
import type { WorldSave } from '../../systems/world/WorldSave';
import { hashString, randomSeed } from '../../core/math/Random';

/** 创建页启动参数，保持导出兼容 */
export interface StartOptions {
  worldKey?:string;
  playMode?: PlayMode;
  creationTimeSpeed?: number;
  ren_ming?: string;
  profile?: PlayerProfile;
  playerFaction?: PlayerFactionOptions;
  seed: number;
  climateWeights: ClimateWeights;
  generation: WorldGeneration;
  calendarType: 'real' | 'yuan';
  unixMs?: number;
  utcOffsetMinutes?: number;
  worldName?: string;
  saveId?: string;
  save?: WorldSave;
}

/** 数字视为 32 位无符号种子，其他文本走稳定哈希 */
export function parseSeed(raw: string): number {
  const value = raw.trim();
  if (!value) return randomSeed();
  return /^-?\d+$/.test(value) ? Number(BigInt.asUintN(32, BigInt(value))) : hashString(value);
}
