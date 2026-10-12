import { WaterKind, type WaterField, type WaterState } from './DynamicWater';
import { WATER_OFFSET } from './WaterSurface';

export const MIST_EMITTER_STRIDE = 10;
const EMITTER_CAP = 14;
const TRACE_LIMIT = 1024;
export type WaterfallSampler = (x: number, z: number) => WaterState;
const DIRECTIONS = [[1, 0], [-1, 0], [0, 1], [0, -1], [1, 1], [1, -1], [-1, 1], [-1, -1]];

function flowStep(state: WaterState, sign: number): number[] {
  let best = -Infinity, step = DIRECTIONS[0];
  for (const direction of DIRECTIONS) {
    const score = sign * (direction[0] * state.vx + direction[1] * state.vz) / Math.hypot(...direction);
    if (score > best) { best = score; step = direction; }
  }
  return step;
}

export function waterfallMistRatio(discharge: number, form = 0, freezing = 0): number {
  const flow = Math.min(1, Math.log1p(Math.max(0, discharge)) / Math.log1p(256));
  return Math.min(.2, Math.max(.05, .05 + .15 * flow + (form === 3 ? .03 : form === 1 ? -.03 : 0) - (freezing === 1 ? .02 : 0)));
}

export function findMistEmitters(size: number, field: WaterField, outside?: WaterfallSampler): Float32Array | undefined {
  if (field.appearance?.form === 2 || field.appearance?.freezing === 2) return undefined;
  const P = size + 2;
  const cache = new Map<string, WaterState>();
  const sample: WaterfallSampler = (x, z) => {
    const key = `${x},${z}`, cached = cache.get(key);
    if (cached) return cached;
    let state: WaterState;
    if (x >= -1 && x <= size && z >= -1 && z <= size) {
      const i = (z + 1) * P + x + 1;
      state = { level: field.levels[i], kind: field.kinds[i], vx: field.velocities[i * 2], vz: field.velocities[i * 2 + 1], discharge: field.discharge[i] };
    } else state = outside?.(x, z) ?? {level: -Infinity, kind: WaterKind.DRY, vx: 0, vz: 0, discharge: 0};
    cache.set(key, state);
    return state;
  };
  const flowing = (state: WaterState) => state.kind === WaterKind.FALL && Number.isFinite(state.level) && Math.hypot(state.vx, state.vz) >= .5 && state.discharge > 0;
  const candidates: number[][] = [];
  for (let z = 0; z < size; z++) for (let x = 0; x < size; x++) {
    const state = sample(x, z);
    if (!flowing(state)) continue;
    const [dx, dz] = flowStep(state, 1), next = sample(x + dx, z + dz);
    if (Number.isFinite(next.level) && state.level - next.level > .01) candidates.push([x, z]);
  }
  if (!candidates.length) return undefined;
  const trace = (sx: number, sz: number, sign: number): { level: number; complete: boolean } => {
    let x = sx, z = sz, state = sample(x, z);
    let calmSteps = 0, endpoint = state.level;
    const visited = new Set<string>();
    for (let n = 0; n < TRACE_LIMIT; n++) {
      visited.add(`${x},${z}`);
      let [dx, dz] = flowStep(state, sign);
      if (!flowing(sample(x + dx, z + dz))) {
        let alignment = .38;
        const speed = Math.hypot(state.vx, state.vz);
        for (const [ox, oz] of DIRECTIONS) {
          const score = sign * (ox * state.vx + oz * state.vz) / (Math.hypot(ox, oz) * speed);
          const candidate = sample(x + ox, z + oz);
          if (score > alignment && flowing(candidate) && (candidate.level - state.level) * sign <= .5) {
            alignment = score; dx = ox; dz = oz;
          }
        }
      }
      const nx = x + dx, nz = z + dz;
      if (visited.has(`${nx},${nz}`)) return {level: state.level, complete: false};
      const next = sample(nx, nz);
      if (!Number.isFinite(next.level)) return {level: state.level, complete: false};
      if ((next.level - state.level) * sign > .5) return {level: state.level, complete: false};
      if (!flowing(next)) {
        if (calmSteps === 0) endpoint = sign > 0 ? next.level : state.level;
        calmSteps++;
        if (calmSteps >= 8 || Math.hypot(next.vx, next.vz) < .5 || next.kind === WaterKind.LAKE || next.kind === WaterKind.SEA)
          return {level: endpoint, complete: true};
      } else calmSteps = 0;
      x = nx; z = nz; state = next;
    }
    return {level: state.level, complete: false};
  };
  const count = Math.min(EMITTER_CAP, candidates.length), out: number[] = [];
  for (let n = 0; n < count; n++) {
    const [x, z] = candidates[Math.round(n * (candidates.length - 1) / Math.max(1, count - 1))];
    const state = sample(x, z), speed = Math.hypot(state.vx, state.vz);
    const dx = state.vx / speed, dz = state.vz / speed;
    const [ox, oz] = flowStep(state, 1), next = sample(x + ox, z + oz);
    const top = trace(x, z, -1), base = trace(x, z, 1);
    const ratio = top.complete && base.complete && top.level > base.level
      ? waterfallMistRatio(state.discharge, field.appearance?.form, field.appearance?.freezing) : 0;
    out.push(x + .5 + dx * .55, next.level - WATER_OFFSET, z + .5 + dz * .55,
      Math.min(2.2, .7 + Math.log1p(state.discharge) * .22), dx, dz,
      state.level - WATER_OFFSET, base.level - WATER_OFFSET, top.level - WATER_OFFSET, ratio);
  }
  return Float32Array.from(out);
}
