import type { MysterySettings } from './MysterySettings.ts';

export type PlayMode = 'survival' | 'leisure';

export function parsePlayMode(value: unknown): PlayMode {
  if (value === undefined) return 'leisure';
  if (value !== 'survival' && value !== 'leisure') throw new Error('游戏模式无效。');
  return value;
}

export function parseCreationTimeSpeed(value: unknown = 72): number {
  if (typeof value !== 'number' || !Number.isFinite(value) || value < 0 || value > 86400) throw new Error('创建时间流速无效。');
  return value;
}

export class PlaySession {
  private unlocked = false;
  constructor(readonly mode: PlayMode, readonly creationTimeSpeed: number) {}
  get allowsCheats(): boolean { return this.mode === 'leisure' || this.unlocked; }
  unlock(command: string): boolean {
    if (command !== '/world.qianlin 天地無窮極陰陽轉相因') return false;
    this.unlocked = true;
    return true;
  }
  saveMystery(current: MysterySettings): MysterySettings {
    return this.mode === 'survival' ? { timeSpeed: this.creationTimeSpeed, menuAutoPause: false } : current;
  }
}
