import type { CalendarClock, CalendarSystem } from '../systems/calendar/CalendarSystem';
import { precipitationWorld } from '../systems/world/GeneratorFactory';
import type { WorldGeneratorBase } from '../systems/world/WorldContract';
import { sampleChunkWeather, type ChunkWeather, type WeatherSystem } from '../systems/world/Weather';
import { WorldDataStore } from '../systems/world/WorldDataStore';
import { Precipitation, precipitationKey } from '../systems/world/weather/Precipitation';
import type { WorldSave } from '../systems/world/WorldSave';

interface GameWeatherOptions {
  generator: WorldGeneratorBase;
  weather: WeatherSystem;
  clock: CalendarClock;
  calendar: CalendarSystem;
  worldKey: string;
  save?: WorldSave;
  /** 降水日界偏移，缺省用历法偏移 */
  utcOffsetMinutes?: number;
  onStateChange(): void;
}

/** 天气查询缓存与降水生命周期 */
export class GameWeather {
  readonly precipitation: Precipitation | null;
  private readonly cache = new Map<string, ChunkWeather>();
  private minute = -Infinity;
  private disposed = false;

  constructor(private readonly options: GameWeatherOptions) {
    const world = precipitationWorld(options.generator);
    if (!world) { this.precipitation = null; return; }
    const store = new WorldDataStore(options.worldKey);
    this.precipitation = new Precipitation({
      world, store, dataKey: options.save?.weatherKey ?? precipitationKey(),
      unixMs: options.save?.unixMs ?? options.clock.unixMs,
      utcOffsetMinutes: options.utcOffsetMinutes ?? options.calendar.utcOffsetMinutes,
      isForced: () => options.weather.isForced,
      forcedWeather: () => options.weather.current,
      createWorker: () => new Worker(new URL('../systems/world/weather/daily.worker.mjs', import.meta.url), { type: 'module' }),
      onStateChange: () => { if (!this.disposed) options.onStateChange(); },
    });
    void this.initialize(store);
  }

  private async initialize(store: WorldDataStore): Promise<void> {
    const precipitation = this.precipitation;
    if (!precipitation) return;
    try {
      const saved = this.options.save?.worldKey ? await store.get(precipitation.dataKey).catch(() => null) : null;
      if (!this.disposed) await precipitation.initialize(saved);
    } catch (error) { if (!this.disposed) console.warn('[Precipitation]', error); }
  }

  invalidate(): void { this.cache.clear(); }

  chunkWeather(cx: number, cz: number): ChunkWeather {
    const { generator, clock } = this.options;
    if (this.precipitation?.ready) return this.precipitation.chunkWeather(cx, cz, generator.getSurfaceTemperature(cx, cz));
    const minute = Math.floor(clock.unixMs / 60000);
    if (minute !== this.minute) { this.cache.clear(); this.minute = minute; }
    const key = `${Math.floor(cx)},${Math.floor(cz)}`;
    let state = this.cache.get(key);
    if (!state) {
      state = sampleChunkWeather(generator, clock.unixMs, cx, cz);
      if (this.cache.size >= 4096) this.cache.clear();
      this.cache.set(key, state);
    }
    return state;
  }

  dispose(): void {
    this.disposed = true;
    this.precipitation?.dispose();
    this.cache.clear();
  }
}
