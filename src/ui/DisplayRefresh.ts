const FALLBACK_REFRESH_RATE = 60;
const MIN_REFRESH_RATE = 24;
const MAX_REFRESH_RATE = 500;
const SAMPLE_COUNT = 24;
const PROBE_BUDGET_MS = 700;
const NICE_RATES = [24, 30, 50, 60, 75, 90, 100, 120, 144, 165, 180, 200, 240, 300, 360, 480, 500];

let estimate = FALLBACK_REFRESH_RATE;
let probing = false;
let overridden = false;

export function displayRefreshRate(): number {
  return estimate;
}

export function overrideDisplayRefreshRate(value: number): void {
  if (!Number.isFinite(value) || value <= 0) return;
  estimate = Math.min(MAX_REFRESH_RATE, Math.max(MIN_REFRESH_RATE, Math.round(value)));
  overridden = true;
}

function rateFromDeltas(deltas: number[]): number {
  const sorted = [...deltas].sort((a, b) => a - b);
  const median = sorted[Math.floor(sorted.length / 2)];
  const fastest = sorted[0] * 1.15;
  return 1000 / Math.max(1, Math.min(median, fastest));
}

function snapRefreshRate(rate: number): number {
  const ceiling = NICE_RATES.find(value => value >= rate * 0.92);
  return ceiling ?? Math.round(rate);
}

export function probeDisplayRefresh(): void {
  if (probing || overridden || typeof requestAnimationFrame !== 'function') return;
  probing = true;
  const deltas: number[] = [];
  let last = 0;
  const deadline = performance.now() + PROBE_BUDGET_MS;
  const settle = (): void => {
    probing = false;
    if (overridden || deltas.length < 3) return;
    const rate = snapRefreshRate(rateFromDeltas(deltas));
    if (!Number.isFinite(rate)) return;
    estimate = Math.min(MAX_REFRESH_RATE, Math.max(MIN_REFRESH_RATE, Math.round(rate)));
  };
  const step = (time: number): void => {
    if (last) deltas.push(time - last);
    last = time;
    if (deltas.length >= SAMPLE_COUNT || performance.now() >= deadline) { settle(); return; }
    requestAnimationFrame(step);
  };
  requestAnimationFrame(step);
}
