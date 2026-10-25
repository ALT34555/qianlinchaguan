import { hash2 } from '../../core/math/Random';
import type { WorldGeneratorBase as WorldGenerator } from './WorldContract';
export type WeatherId = 0 | 1 | 2 | 3 | 4 | 100 | 101 | 102 | 103 | 104 | 200 | 201 | 202 | 203 | 204 | 300 | 301 | 302 | 303 | 304 | 400 | 401 | 900 | 901 | 902;
export interface ChunkWeather {
  cx: number; cz: number; id: WeatherId; cloudCover: number; intensity: number;
  precipitation: 'none' | 'rain' | 'snow' | 'mixed'; temperature: number;
}
export interface WeatherProfile {
  name: string; clouds: number; dim: number; wind: number;
  rain: number; snow: number; hail: number; dust: number;
  fog: number; color: string; lightning: boolean; vortex: boolean;
}
function profile(name: string, value: Partial<WeatherProfile> = {}): Readonly<WeatherProfile> {
  return Object.freeze({ name, clouds: 1, dim: 0, wind: 1, rain: 0, snow: 0, hail: 0, dust: 0,
    fog: 1, color: '#8794a5', lightning: false, vortex: false, ...value });
}
export const WEATHER: Readonly<Record<WeatherId, Readonly<WeatherProfile>>> = Object.freeze({
  0: profile('普通'), 1: profile('无云的晴天', { clouds: 0 }),
  2: profile('多云', { clouds: 1.6, dim: .12 }),
  3: profile('阴天', { clouds: 2.5, dim: .4 }),
  4: profile('晴朗且微风', { clouds: .35, wind: 1.8 }),
  100: profile('小雨', { clouds: 1.6, dim: .2, rain: .25 }),
  101: profile('雨', { clouds: 2, dim: .32, rain: .6 }),
  102: profile('大雨', { clouds: 2.5, dim: .5, rain: 1, fog: .65, wind: 2 }),
  103: profile('暴风雨', { clouds: 3, dim: .7, rain: 1, wind: 10, fog: .45 }),
  104: profile('雷雨', { clouds: 3, dim: .6, rain: .8, wind: 4, lightning: true, fog: .6 }),
  200: profile('雨夹雪', { clouds: 2, dim: .3, rain: .3, snow: .35 }),
  201: profile('小雪', { clouds: 1.6, dim: .2, snow: .3 }),
  202: profile('大雪', { clouds: 2.5, dim: .4, snow: 1, fog: .6 }),
  203: profile('暴风雪', { clouds: 3, dim: .55, snow: 1, wind: 12, fog: .25, color: '#bbc4cf' }),
  204: profile('冰雹', { clouds: 2.5, dim: .45, hail: 1 }),
  300: profile('风', { wind: 4, dust: .12 }),
  301: profile('大风', { wind: 9, dust: .3, clouds: 1.8 }),
  302: profile('飓风', { wind: 18, dust: .6, clouds: 3, dim: .65, fog: .35, vortex: true }),
  303: profile('台风', { wind: 15, rain: 1, clouds: 3, dim: .65, fog: .4, vortex: true }),
  304: profile('沙尘暴', { wind: 12, dust: 1, clouds: .5, dim: .5, fog: .2, color: '#b99b68' }),
  400: profile('雾', { clouds: .6, fog: .18, color: '#c3ccd3' }),
  401: profile('霾', { clouds: .6, fog: .22, dim: .25, color: '#888888' }),
  900: profile('日月同耀', { clouds: .3, color: '#6e4d7e' }),
  901: profile('极昼', { clouds: .5 }), 902: profile('极夜', { clouds: .5 }),
});
export const WEATHER_INTERVAL_MS = 4 * 60 * 60 * 1000;
export function weatherPrecipitation(id: WeatherId): number {
  const effect = WEATHER[id];
  return Math.min(1, effect.rain + effect.snow + effect.hail);
}
export function weatherDayRatio(id: WeatherId, ratio: number): number {
  return id === 901 || id === 900 ? .5 : id === 902 ? 0 : ratio;
}
function cloudNoise(seed: number, x: number, z: number, period: number): number {
  const ix = Math.floor(x), iz = Math.floor(z);
  const smooth = (n: number) => n * n * (3 - 2 * n);
  const u = smooth(x - ix), v = smooth(z - iz);
  const at = (a: number, b: number) => hash2(period ? ((a % period) + period) % period : a, b, seed);
  const a = at(ix, iz) * (1 - u) + at(ix + 1, iz) * u;
  const b = at(ix, iz + 1) * (1 - u) + at(ix + 1, iz + 1) * u;
  return a * (1 - v) + b * v;
}
export function naturalChunkWeather(seed: number, unixMs: number, cx: number, cz: number, temperature: number, circumference = 0): ChunkWeather {
  cx = Math.floor(cx); cz = Math.floor(cz);
  const time = Math.floor(unixMs / 60000) * 60000 / WEATHER_INTERVAL_MS;
  const scale = 32, drift = time * 8;
  const cover = cloudNoise(seed, (cx + .5 - drift) / scale, (cz + .5 - drift * .34) / scale, circumference / scale);
  const intensity = cover <= .56 ? 0 : Math.min(1, (cover - .56) / .3);
  const precipitation = intensity === 0 ? 'none' : temperature > 2 ? 'rain' : temperature > 0 ? 'mixed' : 'snow';
  let id: WeatherId;
  if (!intensity) id = cover < .18 ? 1 : cover < .3 ? 4 : cover < .4 ? 0 : cover < .5 ? 2 : 3;
  else if (precipitation === 'mixed') id = 200;
  else if (precipitation === 'snow') id = intensity < .55 ? 201 : 202;
  else if (intensity < .25) id = 100;
  else if (intensity < .6) id = 101;
  else if (intensity < .9) id = 102;
  else id = hash2(Math.floor(cx / scale), Math.floor(cz / scale), seed) < .5 ? 103 : 104;
  return { cx, cz, id, cloudCover: cover, intensity, precipitation, temperature };
}
export function naturalWeather(seed: number, unixMs: number, temperature: number, cx = 0, cz = 0, circumference = 0): WeatherId {
  return naturalChunkWeather(seed, unixMs, cx, cz, temperature, circumference).id;
}
export function sampleChunkWeather(generator: WorldGenerator, unixMs: number, cx: number, cz: number): ChunkWeather {
  cx = Math.floor(cx); cz = Math.floor(cz);
  const circumference = generator.generation.mode === 'planet' ? generator.generation.planet.equatorChunks : 0;
  if (circumference) {
    cx = ((cx + circumference / 2) % circumference + circumference) % circumference - circumference / 2;
    cz = Math.max(-circumference / 4, Math.min(circumference / 4 - 1, cz));
  }
  const temperature = generator.getSurfaceTemperature(cx,cz);
  return naturalChunkWeather(generator.seed, unixMs, cx, cz, temperature, circumference);
}
export class WeatherSystem {
  private forced: WeatherId | null = null;
  current: WeatherId = 0;
  get isForced(): boolean { return this.forced !== null; }
  constructor(private readonly seed: number) {}
  update(unixMs: number, temperature: number, cx = 0, cz = 0, circumference = 0): WeatherId {
    this.current = this.forced ?? naturalWeather(this.seed, unixMs, temperature, cx, cz, circumference);
    return this.current;
  }
  display(natural: ChunkWeather): WeatherId { return this.current = this.forced ?? natural.id; }
  command(value: string): string | null {
    if (!/^\/world\.weather(?:\s|$)/.test(value)) return null;
    const arg = value.slice('/world.weather'.length).trim();
    if (!arg || arg === 'reset') {
      this.forced = null;
      return '已解除天气调控，恢复自然天气。';
    }
    const id = Number(arg) as WeatherId;
    if (!/^\d{1,3}$/.test(arg) || !Object.hasOwn(WEATHER, id)) return '用法：/world.weather <天气id> 或 /world.weather reset';
    this.forced = id; this.current = id;
    return `已强制天气 ${String(id).padStart(3, '0')}：${WEATHER[id].name}`;
  }
}
