export const OWNED_LAND = 9000;
export const FLAG_LAND = 9001;
export const EXCLUSION_RADIUS = 128;
export const DAY_MS = 86400000;

export interface Territory {
  id: string;
  name?: string;
  mainColor: string;
  trimColor: string;
  cx: number;
  cz: number;
  bornYear: number;
  level: number;
  expandedSteps: number;
}

export interface ArtificialClaim {
  cx: number;
  cz: number;
  type: number;
  faction: string;
  step: number;
}

export interface ArtificialState {
  playerFactionId?: string;
  epochUnixMs: number;
  lastDay: number;
  territories: Territory[];
  claims: ArtificialClaim[];
}

export const claimKey = (cx: number, cz: number): string => `${cx},${cz}`;
export const allowsNaturalDecoration = (type: number): boolean => type < OWNED_LAND || type === OWNED_LAND;

export function calendarYear(ms: number, mode: 'real' | 'yuan', offset: number): number {
  const local = ms + offset * 60000;
  if (mode === 'yuan') return local / (360 * DAY_MS);
  const date = new Date(local), year = date.getUTCFullYear();
  const start = new Date(local); start.setUTCFullYear(year, 0, 1); start.setUTCHours(0, 0, 0, 0);
  const end = new Date(start); end.setUTCFullYear(year + 1);
  return year + (local - start.getTime()) / (end.getTime() - start.getTime());
}

export function parseArtificialState(value: unknown): ArtificialState | undefined {
  if (value === undefined) return undefined;
  const fail = (): never => { throw new Error('存档中的人工领地数据无效。'); };
  if (!value || typeof value !== 'object') return fail();
  const s = value as ArtificialState;
  if (!Number.isFinite(s.epochUnixMs) || Math.abs(s.epochUnixMs) > 8640000000000000 || !Number.isSafeInteger(s.lastDay) ||
      !Array.isArray(s.territories) || !Array.isArray(s.claims)) return fail();
  const coord = (n: number) => Number.isInteger(n) && Math.abs(n) <= 10000000;
  const integer = (n: number) => Number.isSafeInteger(n) && n >= 0;
  const ids = new Set<string>(), cells = new Set<string>();
  const territories = s.territories.map(t => {
    if (!t || !/^[0-9a-f]{16}$/.test(t.id) || ids.has(t.id) || !/^#[0-9a-f]{6}$/i.test(t.mainColor) ||
        !/^#[0-9a-f]{6}$/i.test(t.trimColor) || !coord(t.cx) || !coord(t.cz) ||
        !Number.isFinite(t.bornYear) || t.bornYear < 0 || !integer(t.level) || !integer(t.expandedSteps)) return fail();
    if (t.name !== undefined && (typeof t.name !== 'string' || !t.name.trim() || t.name.length > 40 || /[\x00-\x1f\x7f]/.test(t.name))) return fail();
    ids.add(t.id);
    return { id: t.id, ...(t.name !== undefined ? { name: t.name } : {}), mainColor: t.mainColor, trimColor: t.trimColor, cx: t.cx, cz: t.cz,
      bornYear: t.bornYear, level: t.level, expandedSteps: t.expandedSteps };
  });
  const claims = s.claims.map(c => {
    if (!c || !coord(c.cx) || !coord(c.cz) || !ids.has(c.faction) ||
        (c.type !== OWNED_LAND && c.type !== FLAG_LAND) || !integer(c.step)) return fail();
    const key = claimKey(c.cx, c.cz);
    if (cells.has(key)) return fail();
    cells.add(key);
    return { cx: c.cx, cz: c.cz, type: c.type, faction: c.faction, step: c.step };
  });
  const byCell = new Map(claims.map(c => [claimKey(c.cx, c.cz), c]));
  for (const t of territories) if (byCell.get(claimKey(t.cx, t.cz))?.faction !== t.id) return fail();
  if (s.playerFactionId !== undefined && !ids.has(s.playerFactionId)) return fail();
  return { ...(s.playerFactionId !== undefined ? { playerFactionId: s.playerFactionId } : {}), epochUnixMs: s.epochUnixMs, lastDay: s.lastDay, territories, claims };
}
