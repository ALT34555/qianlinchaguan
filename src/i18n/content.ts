/** 静态内容多语言映射层 */
import { getChunkTypeDef, type ChunkTypeDef } from '../systems/world/ChunkTypes';
import { CLIMATES } from '../systems/world/WorldSettings';
import { SEASON_NAMES } from '../systems/calendar/GanZhi';
import { hasMessage, t } from './index';

/** 区块类型名称（已本地化）。 */
export function chunkName(def: ChunkTypeDef): string {
  const key = `chunk.${def.key}.name`;
  return hasMessage(key) ? t(key) : def.name;
}

/** 区块类型描述（已本地化）。 */
export function chunkDescription(def: ChunkTypeDef): string {
  const key = `chunk.${def.key}.desc`;
  return hasMessage(key) ? t(key) : def.description;
}

/** 按区块类型 ID 取本地化名称。 */
export function chunkNameById(id: number): string {
  return chunkName(getChunkTypeDef(id));
}

/** 按区块类型 ID 取本地化描述。 */
export function chunkDescriptionById(id: number): string {
  return chunkDescription(getChunkTypeDef(id));
}

/** 气候带名称（下标对应 WorldSettings */
export function climateName(index: number): string {
  const key = `climate.${index}`;
  if (hasMessage(key)) return t(key);
  return CLIMATES[index]?.name ?? String(index);
}

/** 季名（0 = 春 … 3 = 冬）。 */
export function seasonName(index: number): string {
  const key = `season.${index}`;
  if (hasMessage(key)) return t(key);
  return SEASON_NAMES[index] ?? '';
}

/** 历法名称 */
export function calendarLabel(type: 'real' | 'yuan'): string {
  return t(type === 'yuan' ? 'calendar.yuan' : 'calendar.real');
}
