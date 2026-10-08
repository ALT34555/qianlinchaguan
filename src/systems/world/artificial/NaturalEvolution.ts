import type { Territory } from './ArtificialState';

export function evolveTerritory(territory: Territory, year: number, speed: number): boolean {
  if (speed === 0) return false;
  const level = Math.max(territory.level, Math.floor(Math.max(0, year - territory.bornYear) * speed / 10 + 1e-9));
  if (level === territory.level) return false;
  territory.level = level;
  return true;
}
