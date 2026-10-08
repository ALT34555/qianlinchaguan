import { claimKey, type ArtificialClaim, type Territory } from './ArtificialState';

export interface ExpansionContext {
  claims: Map<string, ArtificialClaim>;
  wrap: (cx: number) => number;
  claim: (cx: number, cz: number, faction: string, step: number) => boolean;
}

export function* expandTerritory(t: Territory, year: number, speed: number, context: ExpansionContext): Generator<void> {
  if (speed === 0) return;
  const target = Math.floor(Math.max(0, year - t.bornYear) * speed + 1e-9);
  while (t.expandedSteps < target) {
    const step = t.expandedSteps + 1;
    const frontier = [...context.claims.values()].filter(c => c.faction === t.id && c.step === step - 1);
    if (!frontier.length) { t.expandedSteps = target; return; }
    const visited = new Set<string>();
    for (const c of frontier) for (const [dx, dz] of [[-1, -1], [0, -1], [1, -1], [-1, 0], [1, 0], [-1, 1], [0, 1], [1, 1]]) {
      const cx = context.wrap(c.cx + dx), cz = c.cz + dz, key = claimKey(cx, cz);
      if (visited.has(key) || context.claims.has(key)) continue;
      visited.add(key);
      context.claim(cx, cz, t.id, step);
      yield;
    }
    t.expandedSteps = step;
  }
}
