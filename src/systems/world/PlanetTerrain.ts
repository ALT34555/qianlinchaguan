import { createNoise3D } from 'simplex-noise';
import { mulberry32 } from '../../core/math/Random';
import earth from '../../../content/maps/earth/land.json';
import { planetCoordinates, type PlanetSettings } from './WorldSettings';
import { ContinentFragments } from './ContinentFragments';
import { TerrainLayers, spherePoint, clamp, smooth, type TerrainSample } from './TerrainLayers';
import {
  earthMountainsRelief,
  earthMacroPlate,
  earthBasinDepression,
  earthRiverSample,
  earthMoistureField,
} from './EarthGeography';

const RAD = Math.PI / 180;
let coastBytes: Uint8Array | undefined;

/** 打包进游戏的海岸距离场 */
export function earthCoastDistance(longitude: number, latitude: number): number {
  coastBytes ??= Uint8Array.from(atob(earth.coastDistance), c => c.charCodeAt(0));
  const x = ((longitude + 180) / 360 * earth.width - .5 + earth.width) % earth.width;
  const y = clamp((90 - latitude) / 180 * earth.height - .5, 0, earth.height - 1);
  const ix = Math.floor(x), iy = Math.floor(y), tx = x - ix, ty = y - iy;
  const read = (dx: number, dy: number) => coastBytes![Math.min(earth.height - 1, iy + dy) * earth.width + (ix + dx) % earth.width] - 128;
  return ((read(0, 0) * (1 - tx) + read(1, 0) * tx) * (1 - ty) +
    (read(0, 1) * (1 - tx) + read(1, 1) * tx) * ty) * .5;
}

export class PlanetTerrain {
  readonly layers: TerrainLayers;
  private readonly fragments: ContinentFragments | null;

  constructor(readonly settings: PlanetSettings, seed: number, landRatio = .5) {
    const rng = mulberry32(seed ^ 0x706c616e);
    const axis = spherePoint(rng() * 360 - 180, Math.asin(rng() * 2 - 1) / RAD);
    const noise = createNoise3D(mulberry32(seed ^ 0x636f6173));
    const density = settings.map === 'earth' ? (p: [number, number, number]) =>
      earthCoastDistance(Math.atan2(p[2], p[0]) / RAD, Math.asin(clamp(p[1], -1, 1)) / RAD) / 28
      : (p: [number, number, number]) => p.reduce((sum, value, i) => sum + value * axis[i], 0) +
        noise(p[0] * 3 + 17.3, p[1] * 3 - 8.1, p[2] * 3 + 3.7) * .06;
    this.layers = new TerrainLayers(seed, true, settings.map === 'earth' ? .5 : landRatio,
      density, settings.map === 'earth' ? 0 : undefined, settings.equatorChunks * 64 / (2 * Math.PI));
    const activity = settings.tectonicActivity;
    const strength = activity <= 5 ? activity * 3 / 5 : 3 + (activity - 5) * 3 / 4;
    this.fragments = settings.map !== 'earth' && activity > 0 && landRatio > 0 && landRatio < 1
      ? new ContinentFragments(axis, this.layers.deformation, seed, strength) : null;
  }

  private terrain(lon: number, lat: number,heightOnly=false): TerrainSample {
    const point = spherePoint(lon, lat), activity = this.settings.map === 'earth' ? 0 : this.settings.tectonicActivity;
    const temperature = this.temperatureAt(lat);
    let sample: TerrainSample;
    if (this.fragments) {
      const mapped = this.fragments.map(point, p => this.layers.islandCoverage(p, undefined, activity));
      sample = this.layers.sample(mapped.point, activity, temperature, mapped.distance,heightOnly);
      const coast = this.layers.islandCoverage(point, sample.coverage, activity);
      if (coast > sample.coverage) sample = this.layers.sample(mapped.point, activity, temperature, coast,heightOnly);
    } else sample = this.layers.sample(point, activity, temperature,undefined,heightOnly);

    if (this.settings.map === 'earth' && sample.coverage > 0) {
      const coast = smooth(sample.coverage / .12);
      const macro = earthMacroPlate(lon, lat);
      const sink = earthBasinDepression(lon, lat);
      const mtn = earthMountainsRelief(lon, lat);
      const base = Math.max(0, Math.max(sample.plateBase * .1, macro.base - sink));
      const river = earthRiverSample(lon, lat);
      let height = (base + mtn + sample.hillRelief * .2) * coast;
      if (river.inValley) {
        height = Math.max(0, height - (height - river.bedElevation) * river.valleyFactor * .75);
      }
      if (river.incision > 0) {
        height = Math.max(0, height - river.incision * 3.5);
      }
      const tier = (base >= 1800 ? 3 : base >= 900 ? 2 : base >= 450 ? 1 : 0) as TerrainSample['elevationTier'];
      sample = {
        ...sample,
        height,
        uplift: base * coast,
        plateBase: base,
        elevationTier: Math.max(sample.elevationTier, tier) as TerrainSample['elevationTier'],
        hillRelief: Math.min(256, sample.hillRelief * .2 + mtn),
        mountainRelief: mtn,
      };
    }
    return sample;
  }

  private temperatureAt(lat: number): number {
    const p = this.settings, blend = Math.max(0, Math.cos(lat * RAD)) ** 1.4;
    return p.poleTemperature + (p.equatorTemperature - p.poleTemperature) * blend;
  }

  sample(cx: number, cz: number, rainfall = true): TerrainSample {
    const {longitude: lon, latitude: lat} = planetCoordinates(cx, cz, this.settings.equatorChunks);
    const terrain = this.terrain(lon, lat);
    if (this.settings.map === 'earth') {
      let moisture = earthMoistureField(lon, lat);
      const river = earthRiverSample(lon, lat);
      if (river.inValley) moisture = Math.min(1, moisture + .35 * river.valleyFactor);
      if (rainfall && terrain.height > 0) {
        const arid = Math.exp(-(((Math.abs(lat) - 27) / 12) ** 2));
        moisture += .08 * (1 - arid * .8);
      }
      return {...terrain, moisture: clamp(moisture, -1, 1)};
    }
    const arid = Math.exp(-(((Math.abs(lat) - 27) / 12) ** 2));
    let moisture = this.layers.field(spherePoint(lon, lat), 1.8, 2, 79) * .38 + .38 * Math.cos(lat * RAD * 2);
    moisture -= .95 * arid;
    if (rainfall && terrain.height > 0) {
      const angle = this.settings.monsoonDirection * RAD;
      let ocean = 0, barrier = terrain.height;
      for (const distance of [2, 5, 10]) {
        const source = this.terrain(((lon - Math.sin(angle) * distance / Math.max(.3, Math.cos(lat * RAD)) + 540) % 360) - 180,
          clamp(lat - Math.cos(angle) * distance, -90, 90));
        if (source.height < 0) ocean += .22;
        barrier = Math.max(barrier, source.height);
      }
      moisture += ocean * (1 - arid * .75) - Math.min(.45, (barrier - terrain.height) / 1024);
    }
    return {...terrain, moisture: clamp(moisture, -1, 1)};
  }

  boundaryHeight(cx:number,cz:number):number {
    const {longitude,latitude}=planetCoordinates(cx,cz,this.settings.equatorChunks);
    return this.terrain(longitude,latitude,this.settings.map!=='earth').height;
  }
}
