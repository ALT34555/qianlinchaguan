import { DEFAULT_GENERATION, DEFAULT_PLANET, EARTH_PLANET, normalizeGeneration, type ClimateWeights, type WorldGeneration } from '../systems/world/WorldSettings';

const plane: Record<string, { weights: ClimateWeights; landRatio: number }> = {
  balanced: { weights: [20, 20, 20, 20, 20], landRatio: .5 },
  islands: { weights: [45, 35, 15, 5, 0], landRatio: .2 },
  mountains: { weights: [10, 15, 35, 30, 10], landRatio: .8 },
  arctic: { weights: [0, 5, 15, 35, 45], landRatio: .5 },
};
const planets: Record<string, { poleTemperature: number; equatorTemperature: number; tectonicActivity: number; landRatio: number; precipitation: number }> = {
  primordial: { poleTemperature: -25, equatorTemperature: 30, tectonicActivity: 0, landRatio: .8, precipitation: .5 },
  frozen: { poleTemperature: -80, equatorTemperature: -20, tectonicActivity: 5, landRatio: .5, precipitation: .3 },
  habitable: { poleTemperature: -25, equatorTemperature: 30, tectonicActivity: 5, landRatio: .5, precipitation: .5 },
  archipelago: { poleTemperature: -15, equatorTemperature: 35, tectonicActivity: 9, landRatio: .2, precipitation: .8 },
  redhot: { poleTemperature: 40, equatorTemperature: 90, tectonicActivity: 9, landRatio: .8, precipitation: 0 },
};

export function worldPreset(id: string, current: WorldGeneration): { generation: WorldGeneration; weights: ClimateWeights } | undefined {
  const weights: ClimateWeights = [20, 20, 20, 20, 20];
  if (plane[id]) return { generation: normalizeGeneration({ ...DEFAULT_GENERATION, artificial: current.artificial, landRatio: plane[id].landRatio }), weights: plane[id].weights };
  if (planets[id]) {
    const { landRatio, precipitation, ...planet } = planets[id];
    return { generation: normalizeGeneration({ mode: 'planet', artificial: current.artificial, landRatio, precipitation, planet: { ...DEFAULT_PLANET, ...planet } }), weights };
  }
  const precision = /^earth-(2048|4096|8192)$/.exec(id);
  if (precision) return { generation: normalizeGeneration({ mode: 'planet', artificial: current.artificial, landRatio: .5, precipitation: .5,
    planet: { ...EARTH_PLANET, equatorChunks: Number(precision[1]) } }), weights };
  return undefined;
}
