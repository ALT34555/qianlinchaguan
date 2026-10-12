import { CHUNK_SIZE } from '../../../core/config';
import { hash2, hashString } from '../../../core/math/Random';
import { planetCoordinates } from '../WorldSettings';
import type { WorldGenerator } from '../WorldGenerator';
import { EXCLUSION_RADIUS, type Territory } from './ArtificialState';

/** 每格候选尝试次数 */
const CANDIDATE_TRIES = 12;
/** 宜居分下限，低于此分不立领地 */
export const MIN_TERRITORY_SCORE = .5;
/** 达到该分立即采用，不再比较 */
const GOOD_SCORE = .62;
/** 评分权重：平坦/临水/气候/纬度/低地 */
const W_RELIEF = .28, W_WATER = .22, W_CLIMATE = .2, W_LATITUDE = .2, W_ELEVATION = .1;
/** 不可立领地的地貌类型 */
const BARREN_TYPES = [0, 2, 5, 10, 11, 105];
/** 视作水体的邻块类型 */
const WATER_TYPES = [2, 5, 10, 11, 105];
const NEIGHBOURS: readonly [number, number][] = [[-1, -1], [0, -1], [1, -1], [-1, 0], [1, 0], [-1, 1], [0, 1], [1, 1]];
const RAD = Math.PI / 180;

export class ArtificialGenerator {
  private readonly ratings = new Map<string, number>();
  private readonly rowScale: number;
  readonly cellSize: number;
  constructor(readonly terrain: WorldGenerator) {
    const circumference = terrain.generation.planet.equatorChunks;
    this.cellSize = terrain.generation.mode === 'planet' ? circumference / Math.max(1, Math.floor(circumference / 384)) : 384;
    this.rowScale = terrain.generation.mode === 'planet' ? this.maxRowWeight() : 1;
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

  /** 硬性资格：平坦干燥的普通陆地 */
  suitable(cx: number, cz: number): boolean {
    return this.rating(cx, cz) >= 0;
  }

  /** 宜居评分，负数代表不合格 */
  score(cx: number, cz: number): number {
    return this.rating(cx, cz);
  }

  private rating(cx: number, cz: number): number {
    cx = this.wrap(cx);
    const key = `${cx},${cz}`, cached = this.ratings.get(key);
    if (cached !== undefined) return cached;
    const relief = this.check(cx, cz);
    const result = relief === null ? -1 : this.rate(cx, cz, relief);
    if (this.ratings.size >= 8192) this.ratings.clear();
    this.ratings.set(key, result);
    return result;
  }

  private check(cx: number, cz: number): number | null {
    if (!this.inWorld(cz)) return null;
    const info = this.terrain.getChunkInfo(cx, cz);
    if (BARREN_TYPES.includes(info.type) || info.wetland > 0 || info.lakeLevel !== null) return null;
    let min = Infinity, max = -Infinity;
    const sample = (x: number, z: number): boolean => {
      const wx = cx * CHUNK_SIZE + x, wz = cz * CHUNK_SIZE + z;
      const h = this.terrain.getHeight(wx, wz), water = this.terrain.getWaterLevel(wx, wz);
      min = Math.min(min, h); max = Math.max(max, h);
      return Number.isFinite(h) && max - min < 4 && !(water >= h);
    };
    for (const z of [0, 16, 32, 48, 63]) for (const x of [0, 16, 32, 48, 63]) if (!sample(x, z)) return null;
    for (let z = 0; z < CHUNK_SIZE; z++) for (let x = 0; x < CHUNK_SIZE; x++) if (!sample(x, z)) return null;
    return max - min;
  }

  /** 平坦、临水、气候宜人、不在极地、地势不高 */
  private rate(cx: number, cz: number, relief: number): number {
    const info = this.terrain.getChunkInfo(cx, cz);
    const temperature = info.temperature, elevation = info.elevation;
    const latitude = this.latitude(cx, cz);
    const climate = Number.isFinite(temperature) ? 1 - Math.min(1, Math.abs(temperature - 16) / 30) : .5;
    const high = Number.isFinite(elevation) ? 1 - Math.min(1, Math.max(0, elevation) / 48) : .5;
    return W_RELIEF * (1 - relief / 4)
      + W_WATER * (this.nearWater(cx, cz) ? 1 : .5)
      + W_CLIMATE * climate
      + W_LATITUDE * (latitude === null ? 1 : 1 - Math.min(1, Math.max(0, (latitude - 45) / 30)))
      + W_ELEVATION * high;
  }

  /** 星球纬度绝对值，平面模式无纬度 */
  private latitude(cx: number, cz: number): number | null {
    if (this.terrain.generation.mode !== 'planet') return null;
    return Math.abs(planetCoordinates(cx, cz, this.terrain.generation.planet.equatorChunks).latitude);
  }

  private nearWater(cx: number, cz: number): boolean {
    for (const [dx, dz] of NEIGHBOURS) {
      if (WATER_TYPES.includes(this.terrain.getChunkInfo(this.wrap(cx + dx), cz + dz).type)) return true;
    }
    return false;
  }

  private rowBounds(tz: number): { start: number; end: number } {
    const size = this.terrain.generation.planet.equatorChunks, oz = -size / 4;
    return { start: Math.max(oz + tz * this.cellSize, oz), end: Math.min(oz + (tz + 1) * this.cellSize, -oz) };
  }

  /** 行面积权重：极带一行不按整格计 */
  private rowWeight(tz: number): number {
    const size = this.terrain.generation.planet.equatorChunks;
    const { start, end } = this.rowBounds(tz);
    const clampLat = (v: number) => Math.max(-90, Math.min(90, v));
    const ra = clampLat(-start / size * 360) * RAD, rb = clampLat(-end / size * 360) * RAD;
    const span = Math.abs(rb - ra);
    return span < 1e-9 ? Math.cos(ra) : (Math.sin(rb) - Math.sin(ra)) / (rb - ra);
  }

  /** 最宽一行的面积权，作稀释基准 */
  private maxRowWeight(): number {
    const size = this.terrain.generation.planet.equatorChunks;
    const rows = Math.max(1, Math.round(size / 2 / this.cellSize));
    let max = 0;
    for (let tz = 0; tz < rows; tz++) max = Math.max(max, this.rowWeight(tz));
    return max || 1;
  }

  private rowAccepted(tx: number, tz: number): boolean {
    const size = this.terrain.generation.planet.equatorChunks;
    const columns = Math.max(1, Math.round(size / this.cellSize));
    const x = ((tx % columns) + columns) % columns;
    return hash2(x, tz, this.terrain.seed ^ 0x706f6c65) < this.rowWeight(tz) / this.rowScale;
  }

  candidate(tx: number, tz: number): { cx: number; cz: number } | null {
    const salt = this.terrain.seed ^ 0x6198b;
    const planet = this.terrain.generation.mode === 'planet', size = this.terrain.generation.planet.equatorChunks;
    const ox = planet ? -size / 2 : 0;
    const columns = Math.max(1, Math.round(size / this.cellSize));
    if (planet) tx = ((tx % columns) + columns) % columns;
    const margin = EXCLUSION_RADIUS / 2;
    const width = Math.max(1, Math.floor(this.cellSize - margin * 2));
    const { start, end } = planet ? this.rowBounds(tz) : { start: tz * this.cellSize, end: (tz + 1) * this.cellSize };
    const height = end - start;
    if (height < 1) return null;
    const pad = height > margin * 2 ? margin : 0;
    if (planet && !this.rowAccepted(tx, tz)) return null;
    let best: { cx: number; cz: number } | null = null, bestScore = MIN_TERRITORY_SCORE;
    for (let i = 0; i < CANDIDATE_TRIES; i++) {
      const cx = this.wrap(Math.floor(ox + tx * this.cellSize) + margin + Math.floor(hash2(tx, tz, salt + i * 2) * width));
      const cz = Math.floor(start + pad + hash2(tx, tz, salt + i * 2 + 1) * Math.max(1, Math.floor(height - pad * 2)));
      const value = this.rating(cx, cz);
      if (value >= GOOD_SCORE) return { cx, cz };
      if (value > bestScore) { bestScore = value; best = { cx, cz }; }
    }
    return best;
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
