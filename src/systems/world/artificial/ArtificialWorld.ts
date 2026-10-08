import type { PlayerFactionOptions } from './PlayerFaction';
import type { ChunkInfo, WorldGenerator } from '../WorldGenerator';
import { ArtificialGenerator } from './ArtificialGenerator';
import { normalizeArtificial } from './ArtificialSettings';
import { calendarYear, claimKey, DAY_MS, EXCLUSION_RADIUS, FLAG_LAND, OWNED_LAND,
  type ArtificialClaim, type ArtificialState, type Territory } from './ArtificialState';
import { expandTerritory } from './NaturalExpansion';
import { evolveTerritory } from './NaturalEvolution';
import { flagIndependence } from './FlagIndependence';

export type TerritoryChunkInfo = ChunkInfo & { territory?: Territory };

export class ArtificialWorld {
  readonly generator: ArtificialGenerator;
  readonly settings;
  readonly territories = new Map<string, Territory>();
  readonly claims = new Map<string, ArtificialClaim>();
  revision = 0;
  playerFactionId?: string;
  private readonly tiles = new Set<string>();
  private readonly buckets = new Map<string, ArtificialClaim[]>();
  private readonly epochUnixMs: number;
  private lastDay: number;
  private work: Generator<void> | null = null;
  private needsCatchup = false;

  constructor(readonly terrain: WorldGenerator, unixMs = 0, private readonly mode: 'real' | 'yuan' = 'real',
    private readonly offset = 0, state?: ArtificialState) {
    this.settings = Object.freeze(normalizeArtificial(terrain.generation.artificial));
    this.generator = new ArtificialGenerator(terrain);
    this.epochUnixMs = state?.epochUnixMs ?? unixMs;
    this.lastDay = state?.lastDay ?? this.day(unixMs);
    if (state) {
      this.playerFactionId = state.playerFactionId;
      for (const t of state.territories) this.territories.set(t.id, { ...t });
      for (const c of state.claims) this.addClaim({ ...c });
      this.needsCatchup = true;
    }
  }

  private day(ms: number): number { return Math.floor((ms + this.offset * 60000) / DAY_MS); }
  private year(ms: number): number {
    return Math.max(0, calendarYear(ms, this.mode, this.offset) - calendarYear(this.epochUnixMs, this.mode, this.offset));
  }

  private addClaim(c: ArtificialClaim): void {
    this.claims.set(claimKey(c.cx, c.cz), c);
    const key = claimKey(Math.floor(c.cx / EXCLUSION_RADIUS), Math.floor(c.cz / EXCLUSION_RADIUS));
    const bucket = this.buckets.get(key) ?? [];
    bucket.push(c); this.buckets.set(key, bucket);
    this.revision++;
  }

  private found(cx: number, cz: number, type: number, year: number, salt: string): void {
    cx = this.generator.wrap(cx);
    const t = this.generator.faction(cx, cz, year, salt);
    if (this.territories.has(t.id) || this.claims.has(claimKey(cx, cz))) return;
    this.territories.set(t.id, t);
    this.addClaim({ cx, cz, type, faction: t.id, step: 0 });
    this.needsCatchup = true;
  }

  createPlayerFaction(cx: number, cz: number, options: PlayerFactionOptions): void {
    cx = this.generator.wrap(cx);
    if (this.playerFactionId || !this.generator.suitable(cx, cz) || !this.hasSpace(cx, cz)) throw new Error('己方置旗地不可用。');
    const t = { ...this.generator.faction(cx, cz, 0, 'ren'), name: options.name,
      mainColor: options.mainColor, trimColor: options.trimColor };
    this.playerFactionId = t.id;
    this.territories.set(t.id, t);
    this.addClaim({ cx, cz, type: FLAG_LAND, faction: t.id, step: 0 });
  }

  discover(cx: number, cz: number, radius = 0): void {
    if (!this.settings.enabled) return;
    const s = this.generator.cellSize;
    const planet = this.terrain.generation.mode === 'planet';
    const size = this.terrain.generation.planet.equatorChunks;
    const ox = planet ? -size / 2 : 0, oz = planet ? -size / 4 : 0;
    cx = this.generator.wrap(cx);
    const columns = Math.round(size / s);
    for (let z = Math.floor((cz - radius - oz) / s); z <= Math.floor((cz + radius - oz) / s); z++) {
      if (planet && (z < 0 || z * s >= size / 2)) continue;
      for (let x = Math.floor((cx - radius - ox) / s); x <= Math.floor((cx + radius - ox) / s); x++) {
        const tx = planet ? (x % columns + columns) % columns : x;
        const key = claimKey(tx, z);
        if (this.tiles.has(key)) continue;
        this.tiles.add(key);
        const c = this.generator.candidate(tx, z);
        if (c && this.hasSpace(c.cx, c.cz)) this.found(c.cx, c.cz, OWNED_LAND, 0, 'initial');
      }
    }
  }

  getClaim(cx: number, cz: number): ArtificialClaim | undefined {
    this.discover(cx, cz);
    return this.claims.get(claimKey(this.generator.wrap(cx), cz));
  }

  *discoverRegion(minX: number, minZ: number, maxX: number, maxZ: number): Generator<void> {
    if (!this.settings.enabled) return;
    const s = this.generator.cellSize, planet = this.terrain.generation.mode === 'planet';
    const size = this.terrain.generation.planet.equatorChunks;
    const ox = planet ? -size / 2 : 0, oz = planet ? -size / 4 : 0;
    for (let z = Math.floor((minZ - oz) / s); z <= Math.floor((maxZ - oz) / s); z++) {
      for (let x = Math.floor((minX - ox) / s); x <= Math.floor((maxX - ox) / s); x++) {
        this.discover(Math.floor(ox + (x + .5) * s), Math.floor(Math.min(oz + (z + .5) * s, planet ? size / 4 - 1 : Infinity)));
        yield;
      }
    }
  }

  info(cx: number, cz: number): TerritoryChunkInfo {
    const info = this.terrain.getChunkInfo(cx, cz), c = this.getClaim(cx, cz);
    return c ? { ...info, type: c.type, territory: this.territories.get(c.faction) } : info;
  }

  unowned(cx: number, cz: number, except?: string): boolean {
    cx = this.generator.wrap(cx);
    this.discover(cx, cz, EXCLUSION_RADIUS);
    return this.hasSpace(cx, cz, except);
  }

  private hasSpace(cx: number, cz: number, except?: string): boolean {
    const size = this.terrain.generation.planet.equatorChunks;
    const shifts = this.terrain.generation.mode === 'planet' ? [-size, 0, size] : [0];
    for (const shift of shifts) {
      const bx = Math.floor((cx + shift) / EXCLUSION_RADIUS), bz = Math.floor(cz / EXCLUSION_RADIUS);
      for (let z = bz - 1; z <= bz + 1; z++) for (let x = bx - 1; x <= bx + 1; x++) {
        for (const c of this.buckets.get(claimKey(x, z)) ?? []) {
          if (c.faction !== except && this.generator.distance2(cx, cz, c.cx, c.cz) <= EXCLUSION_RADIUS ** 2) return false;
        }
      }
    }
    return true;
  }

  private claim = (cx: number, cz: number, faction: string, step: number): boolean => {
    cx = this.generator.wrap(cx);
    if (this.claims.has(claimKey(cx, cz)) || !this.generator.inWorld(cz) || !this.unowned(cx, cz, faction) ||
        !this.generator.suitable(cx, cz)) return false;
    this.addClaim({ cx, cz, type: OWNED_LAND, faction, step });
    return true;
  };

  private *advance(year: number): Generator<void> {
    for (const t of [...this.territories.values()].sort((a, b) => a.id.localeCompare(b.id))) {
      if (evolveTerritory(t, year, this.settings.evolution)) this.revision++;
      yield* expandTerritory(t, year, this.settings.expansion, {
        claims: this.claims, wrap: cx => this.generator.wrap(cx), claim: this.claim,
      });
      yield;
    }
  }

  private *simulate(targetDay: number, playerCx: number, playerCz: number): Generator<void> {
    if (this.needsCatchup) {
      this.needsCatchup = false;
      yield* this.advance(this.year(this.lastDay * DAY_MS - this.offset * 60000));
    }
    while (this.lastDay < targetDay) {
      const day = this.lastDay + 1, year = this.year(day * DAY_MS - this.offset * 60000);
      yield* this.advance(year);
      const highestLevel = Math.max(0, ...[...this.territories.values()].map(t => t.level));
      yield* flagIndependence({ enabled: this.settings.evolution > 0 && this.settings.independence,
        highestLevel, seed: this.terrain.seed, day, cx: playerCx, cz: playerCz,
        unowned: (x, z) => this.unowned(x, z), suitable: (x, z) => this.generator.suitable(x, z),
        found: (x, z) => this.found(x, z, FLAG_LAND, year, `day:${day}`),
      });
      this.lastDay = day;
      yield;
    }
  }

  update(ms: number, playerCx: number, playerCz: number, budgetMs = 3): void {
    if (!this.settings.enabled) return;
    this.discover(playerCx, playerCz);
    const day = this.day(ms);
    if (!this.work && (day > this.lastDay || this.needsCatchup)) this.work = this.simulate(day, playerCx, playerCz);
    const start = performance.now();
    while (this.work) {
      if (this.work.next().done) this.work = null;
      if (performance.now() - start >= budgetMs) break;
    }
  }

  snapshot(): ArtificialState | undefined {
    if (!this.settings.enabled && !this.playerFactionId) return undefined;
    return { ...(this.playerFactionId ? { playerFactionId: this.playerFactionId } : {}), epochUnixMs: this.epochUnixMs, lastDay: this.lastDay,
      territories: [...this.territories.values()].map(t => ({ ...t })), claims: [...this.claims.values()].map(c => ({ ...c })) };
  }
}
