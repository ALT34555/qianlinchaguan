import { CHUNK_SIZE } from '../../../core/config';
import { hash2, hashString } from '../../../core/math/Random';
import type { WorldGenerator } from '../WorldGenerator';
import { EXCLUSION_RADIUS, type Territory } from './ArtificialState';

export class ArtificialGenerator {
  private readonly suitability = new Map<string, boolean>();
  readonly cellSize: number;
  constructor(readonly terrain: WorldGenerator) {
    const circumference = terrain.generation.planet.equatorChunks;
    this.cellSize = terrain.generation.mode === 'planet' ? circumference / Math.max(1, Math.floor(circumference / 384)) : 384;
  }

  wrap(cx: number): number {
    if (this.terrain.generation.mode !== 'planet') return cx;
    const size = this.terrain.generation.planet.equatorChunks;
    return ((cx + size / 2) % size + size) % size - size / 2;
  }

  inWorld(cz: number): boolean {
    return this.terrain.generation.mode !== 'planet' || Math.abs(cz + .5) < this.terrain.generation.planet.equatorChunks / 4;
  }

  distance2(ax: number, az: number, bx: number, bz: number): number {
    return this.wrap(ax - bx) ** 2 + (az - bz) ** 2;
  }

  suitable(cx: number, cz: number): boolean {
    cx = this.wrap(cx);
    const key = `${cx},${cz}`, cached = this.suitability.get(key);
    if (cached !== undefined) return cached;
    const result = this.check(cx, cz);
    if (this.suitability.size >= 8192) this.suitability.clear();
    this.suitability.set(key, result);
    return result;
  }

  private check(cx: number, cz: number): boolean {
    if (!this.inWorld(cz)) return false;
    const info = this.terrain.getChunkInfo(cx, cz);
    if ([0, 2, 5, 10, 11, 105].includes(info.type) || info.wetland > 0 || info.lakeLevel !== null) return false;
    let min = Infinity, max = -Infinity;
    const sample = (x: number, z: number): boolean => {
      const wx = cx * CHUNK_SIZE + x, wz = cz * CHUNK_SIZE + z;
      const h = this.terrain.getHeight(wx, wz), water = this.terrain.getWaterLevel(wx, wz);
      min = Math.min(min, h); max = Math.max(max, h);
      return Number.isFinite(h) && max - min < 4 && !(water >= h);
    };
    for (const z of [0, 16, 32, 48, 63]) for (const x of [0, 16, 32, 48, 63]) if (!sample(x, z)) return false;
    for (let z = 0; z < CHUNK_SIZE; z++) for (let x = 0; x < CHUNK_SIZE; x++) if (!sample(x, z)) return false;
    return true;
  }

  candidate(tx: number, tz: number): { cx: number; cz: number } | null {
    const salt = this.terrain.seed ^ 0x6198b;
    const planet = this.terrain.generation.mode === 'planet', size = this.terrain.generation.planet.equatorChunks;
    const ox = planet ? -size / 2 : 0, oz = planet ? -size / 4 : 0;
    const margin = EXCLUSION_RADIUS / 2;
    const width = Math.max(1, Math.floor(this.cellSize - margin * 2));
    const height = planet ? Math.min(this.cellSize, size / 2 - tz * this.cellSize) : this.cellSize;
    if (tz > 0 && height <= margin) return null;
    const zMargin = height <= EXCLUSION_RADIUS && tz === 0 ? 0 : margin;
    for (let i = 0; i < 12; i++) {
      const cx = this.wrap(Math.floor(ox + tx * this.cellSize) + margin + Math.floor(hash2(tx, tz, salt + i * 2) * width));
      const cz = Math.floor(oz + tz * this.cellSize) + zMargin + Math.floor(hash2(tx, tz, salt + i * 2 + 1) * Math.max(1, Math.floor(height - zMargin * 2)));
      if (this.suitable(cx, cz)) return { cx, cz };
    }
    return null;
  }

  faction(cx: number, cz: number, bornYear: number, salt: string): Territory {
    const base = `${this.terrain.seed}:${cx},${cz}:${salt}`;
    const hex = (n: number) => n.toString(16).padStart(8, '0');
    const id = hex(hashString(base)) + hex(hashString(`${base}:faction`));
    const colors = ['#b7463e', '#387fa0', '#65834a', '#8c5e9b', '#c58636', '#337f76', '#445898', '#9b5270'];
    const trims = ['#ead9a2', '#ece6d8', '#d3a349', '#283744'];
    return { id, cx, cz, bornYear, level: 0, expandedSteps: 0,
      mainColor: colors[hashString(base) % colors.length], trimColor: trims[hashString(`${base}:trim`) % trims.length] };
  }
}
