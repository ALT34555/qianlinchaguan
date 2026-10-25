const SHADE_STEPS = [0.58, 0.7, 0.84, 1.0];
const SUN: readonly [number, number, number] = [0.42, 0.84, -0.34];

export function shadeOf(nx: number, ny: number, nz: number): number {
  const d = nx * SUN[0] + ny * SUN[1] + nz * SUN[2];
  const t = (d + 1) / 2;
  const i = Math.round(t * (SHADE_STEPS.length - 1));
  return SHADE_STEPS[i < 0 ? 0 : i > SHADE_STEPS.length - 1 ? SHADE_STEPS.length - 1 : i];
}
