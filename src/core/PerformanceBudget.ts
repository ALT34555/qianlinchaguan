import type { GameSettings } from './GameSettings';

export function performanceBudget(settings: Pick<GameSettings,'graphicsPreset'>) {
  const preset = settings.graphicsPreset ?? 'balanced';
  const low = preset === 'minimal' || preset === 'low' || preset === 'smooth';
  const extreme = preset === 'extreme';
  const balancedWorkers=Math.max(1,Math.min(6,(globalThis.navigator?.hardwareConcurrency??8)-2));
  return { chunkWorkers: low ? 1 : preset === 'balanced' ? balancedWorkers : extreme ? 3 : 2, previewWorkers: low ? 1 : 3,
    generatorEntries: low ? 2048 : extreme ? 16384 : 8192,
    derivedMeshBytes: (extreme ? 48 : preset === 'high' ? 32 : 0)*1024*1024,
    vegetationDensity: preset === 'minimal' ? 0 : low ? .6 : 1,
    pixelRatioCap: low ? 1 : extreme ? 2 : 1.5,
    renderPixels: low ? 1280*720 : extreme ? 2560*1440 : 1920*1080 };
}
