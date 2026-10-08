import { renName } from './PlayerIdentity.ts';
import { hexColor } from '../systems/world/artificial/PlayerFaction.ts';

export interface PlayerFactionState {
  id: string;
  relationship: 'owned' | 'member';
  mainColor: string;
  trimColor: string;
}
export interface PlayerState {
  name: string;
  activeFactionId?: string;
  factions: PlayerFactionState[];
}

export function parsePlayerState(value: unknown): PlayerState {
  if (!value || typeof value !== 'object') throw new Error('玩家状态无效。');
  const state = value as PlayerState;
  if (typeof state.name !== 'string' || !state.name.trim() || state.name.length > 40 ||
      /[\x00-\x1f\x7f]/.test(state.name) || !Array.isArray(state.factions)) throw new Error('玩家状态无效。');
  const ids = new Set<string>();
  const factions = state.factions.map(faction => {
    if (!faction || typeof faction.id !== 'string' || !/^[0-9a-f]{16}$/.test(faction.id) || ids.has(faction.id) ||
        !['owned', 'member'].includes(faction.relationship)) throw new Error('玩家阵营状态无效。');
    ids.add(faction.id);
    return { id: faction.id, relationship: faction.relationship, mainColor: hexColor(faction.mainColor), trimColor: hexColor(faction.trimColor) };
  });
  if (state.activeFactionId !== undefined && !ids.has(state.activeFactionId)) throw new Error('当前玩家阵营无效。');
  return { name: renName(state.name), factions, ...(state.activeFactionId !== undefined ? { activeFactionId: state.activeFactionId } : {}) };
}
