import type { GameSettings } from './GameSettings';

export function performanceBudget(settings: Pick<GameSettings,'graphicsPreset'>) {
  const preset = settings.graphicsPreset ?? 'balanced';
  const low = preset === 'minimal' || preset === 'low' || preset === 'smooth';
  const extreme = preset === 'extreme';
  return { chunkWorkers: low ? 1 : extreme ? 3 : 2, previewWorkers: low ? 1 : 3,
    generatorEntries: low ? 2048 : extreme ? 16384 : 8192,
    derivedMeshBytes: (low ? 8 : extreme ? 48 : preset === 'high' ? 32 : 24)*1024*1024,
    vegetationDensity: preset === 'minimal' ? 0 : low ? .6 : 1,
    pixelRatioCap: low ? 1 : extreme ? 2 : 1.5,
    renderPixels: low ? 1280*720 : extreme ? 2560*1440 : 1920*1080 };
}
