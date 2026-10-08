import { hash2 } from '../../../core/math/Random';

export interface IndependenceContext {
  enabled: boolean;
  highestLevel: number;
  seed: number;
  day: number;
  cx: number;
  cz: number;
  unowned: (cx: number, cz: number) => boolean;
  suitable: (cx: number, cz: number) => boolean;
  found: (cx: number, cz: number) => void;
}

const nearby: [number, number][] = [];
for (let z = -8; z <= 8; z++) for (let x = -8; x <= 8; x++) if (x * x + z * z <= 64) nearby.push([x, z]);
nearby.sort((a, b) => a[0] ** 2 + a[1] ** 2 - b[0] ** 2 - b[1] ** 2);

export function* flagIndependence(c: IndependenceContext): Generator<void> {
  if (!c.enabled || c.highestLevel < 5 || !c.unowned(c.cx, c.cz)) return;
  for (const [dx, dz] of nearby) {
    const cx = c.cx + dx, cz = c.cz + dz;
    if (c.unowned(cx, cz) && c.suitable(cx, cz)) {
      if (hash2(c.day, 9001, c.seed) < .5) c.found(cx, cz);
      return;
    }
    yield;
  }
}
