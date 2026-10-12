import type { GameSettings } from './GameSettings';

export function performanceBudget(settings: Pick<GameSettings,'graphicsPreset'>) {
  const low = settings.graphicsPreset !== 'balanced';
  return { chunkWorkers: low ? 1 : 2, previewWorkers: low ? 1 : 3,
    generatorEntries: low ? 2048 : 8192, derivedMeshBytes: low ? 8*1024*1024 : 24*1024*1024,
    vegetationDensity: settings.graphicsPreset === 'minimal' ? .3 : low ? .6 : 1 };
}
