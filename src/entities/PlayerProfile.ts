import { renName } from './PlayerIdentity.ts';
import { hexColor } from '../systems/world/artificial/PlayerFaction.ts';

export interface PlayerProfile {
  name: string;
  heightCm: number;
  mainColor: string;
  trimColor: string;
}

export const DEFAULT_PLAYER_PROFILE: Readonly<PlayerProfile> = Object.freeze({ name: '赤', heightCm: 170, mainColor: '#CB3A56', trimColor: '#5d3f51' });
export function profileHeightRange(mode = 'leisure'): { min: number; max: number } {
  return mode === 'survival' ? { min: 64, max: 256 } : { min: 8, max: 1024 };
}
export function movementScale(heightCm: number): number { return heightCm / 170; }

export function playerProfile(value: unknown = DEFAULT_PLAYER_PROFILE, mode = 'leisure'): PlayerProfile {
  if (!value || typeof value !== 'object') throw new Error('档案无效。');
  const profile = value as Partial<PlayerProfile>;
  const heightCm = profile.heightCm ?? 170;
  const { min, max } = profileHeightRange(mode);
  if (!Number.isFinite(heightCm) || heightCm < min || heightCm > max) throw new Error(`身高需在 ${min}～${max} cm 之间。`);
  return { name: renName(profile.name), heightCm, mainColor: hexColor(profile.mainColor ?? DEFAULT_PLAYER_PROFILE.mainColor), trimColor: hexColor(profile.trimColor ?? DEFAULT_PLAYER_PROFILE.trimColor) };
}
