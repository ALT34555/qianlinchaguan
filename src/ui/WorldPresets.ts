import { DEFAULT_GENERATION, DEFAULT_PLANET, normalizeGeneration, type ClimateWeights, type WorldGeneration } from '../systems/world/WorldSettings';

/** 宏观星球预设：只保留行星档，模板配方沿用当前选择 */
const planets: Record<string, { poleTemperature: number; equatorTemperature: number; tectonicActivity: number; landRatio: number; precipitation: number }> = {
  primordial: { poleTemperature: -25, equatorTemperature: 30, tectonicActivity: 0, landRatio: .8, precipitation: .5 },
  frozen: { poleTemperature: -80, equatorTemperature: -20, tectonicActivity: 5, landRatio: .5, precipitation: .3 },
  habitable: { poleTemperature: -25, equatorTemperature: 30, tectonicActivity: 5, landRatio: .3, precipitation: 1 },
  archipelago: { poleTemperature: -15, equatorTemperature: 35, tectonicActivity: 9, landRatio: .2, precipitation: .8 },
  redhot: { poleTemperature: 40, equatorTemperature: 90, tectonicActivity: 9, landRatio: .8, precipitation: 0 },
};

export function worldPreset(id: string, current: WorldGeneration): { generation: WorldGeneration; weights: ClimateWeights } | undefined {
  const weights: ClimateWeights = [20, 20, 20, 20, 20];
  const preset = planets[id];
  if (!preset) return undefined;
  const { landRatio, precipitation, ...planet } = preset;
  return { generation: normalizeGeneration({
    ...DEFAULT_GENERATION,
    artificial: current.artificial,
    macroTerrain: current.macroTerrain,
    macroClimate: current.macroClimate,
    macroColumns: current.macroColumns,
    landRatio: Math.round(landRatio * 10) / 10,
    precipitation: Math.round(precipitation * 4) / 4,
    planet: { ...DEFAULT_PLANET, ...planet, equatorChunks: current.planet.equatorChunks },
  }), weights };
}
