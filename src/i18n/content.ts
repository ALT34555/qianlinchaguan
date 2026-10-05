/**
 * 内容数据（content/）的多语言覆盖层。
 *
 * 设计取舍：`content/data/world/*.json` 里保存的是**内容作者的基准语言文本**
 * （目前为中文），策划改表时不需要懂代码；而语言包按内容 `key` 提供覆盖：
 *
 *   chunk.<key>.name / chunk.<key>.desc   —— 区块类型名称与描述
 *   climate.<index>                        —— 气候带名称
 *
 * 命中语言包就用译文，未命中就回退到 JSON 原文。这样
 * "新增一种语言" 与 "新增一种区块" 互不阻塞。
 */
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

/** 气候带名称（下标对应 WorldSettings.CLIMATES）。 */
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

/** 历法名称：'real' → 公历/农历双历，'yuan' → 元历。 */
export function calendarLabel(type: 'real' | 'yuan'): string {
  return t(type === 'yuan' ? 'calendar.yuan' : 'calendar.real');
}
