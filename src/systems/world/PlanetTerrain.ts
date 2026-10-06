import { createNoise3D } from 'simplex-noise';
import { mulberry32 } from '../../core/math/Random';
import earth from '../../../content/maps/earth/land.json';
import { planetCoordinates, type PlanetSettings } from './WorldSettings';
import { ContinentFragments } from './ContinentFragments';
import { TerrainLayers, spherePoint, clamp, smooth, type TerrainSample } from './TerrainLayers';
import {plateUplift} from './PlateUplift';

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

// 仅模拟主要山系位置与宽度
const BELTS = [
  { points: [[-150, 62], [-125, 48], [-110, 33], [-99, 19]], width: 4, height: 125 },
  { points: [[-76, 8], [-78, -5], [-71, -23], [-72, -45]], width: 3, height: 180 },
  { points: [[-9, 32], [5, 46], [25, 43], [47, 34], [70, 35], [88, 29], [103, 29]], width: 3, height: 175 },
  { points: [[35, 11], [37, -5], [30, -20]], width: 3, height: 85 },
  { points: [[140, -18], [149, -28], [146, -39]], width: 3, height: 65 },
  { points: [[130, 45], [140, 38], [143, 32]], width: 2, height: 85 },
  { points: [[7, 59], [20, 69]], width: 3, height: 75 },
];

function earthUplift(lon: number, lat: number): number {
  let uplift = 0;
  for (const belt of BELTS) {
    let contribution = 0;
    for (let i = 1; i < belt.points.length; i++) {
      const [ax, ay] = belt.points[i - 1], [bx, by] = belt.points[i];
      const cos = Math.max(.25, Math.cos(lat * RAD));
      const dx = (bx - ax) * cos, dy = by - ay;
      const px = (lon - ax) * cos, py = lat - ay;
      const t = clamp((px * dx + py * dy) / (dx * dx + dy * dy));
      const distance = Math.hypot(px - dx * t, py - dy * t);
      contribution = Math.max(contribution, belt.height * Math.exp(-((distance / belt.width) ** 2)));
    }
    // 同一条山系的线段不重复计高
    uplift += contribution;
  }
  return Math.min(256,uplift);
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
      density, settings.map === 'earth' ? 0 : undefined);
    const activity = settings.tectonicActivity;
    const strength = activity <= 5 ? activity * 3 / 5 : 3 + (activity - 5) * 3 / 4;
    this.fragments = settings.map !== 'earth' && activity > 0 && landRatio > 0 && landRatio < 1
      ? new ContinentFragments(axis, this.layers.deformation, seed, strength) : null;
  }

  private terrain(lon: number, lat: number): TerrainSample {
    const point = spherePoint(lon, lat), activity = this.settings.map === 'earth' ? 0 : this.settings.tectonicActivity;
    let sample: TerrainSample;
    if (this.fragments) {
      const mapped = this.fragments.map(point, p => this.layers.islandCoverage(p, undefined, activity));
      sample = this.layers.sample(mapped.point, activity, mapped.distance);
      // 漂移后的浅海补充游离小岛
      const coast = this.layers.islandCoverage(point, sample.coverage, activity);
      if (coast > sample.coverage) sample = this.layers.sample(mapped.point, activity, coast);
    } else sample = this.layers.sample(point, activity);
    if (this.settings.map === 'earth' && sample.coverage > 0) {
      const extra = earthUplift(lon, lat) * smooth(sample.coverage / .12);
      // 地球第五层只修正板块底座与相对山系
      const tibet=plateUplift(Math.min(Math.max(0,1-(((lon-88)/15)**2+((lat-34)/8)**2))*.79,.14+Math.max(0,sample.coverage)));
      const coast=smooth(sample.coverage/.12),base=Math.max(sample.plateBase,tibet.base);
      const hills=Math.min(256,sample.hillRelief+extra);
      sample={...sample,height:sample.height+(base-sample.plateBase)*coast+(hills-sample.hillRelief)*coast,
        uplift:base*coast,plateBase:base,elevationTier:Math.max(tibet.tier,sample.elevationTier) as TerrainSample['elevationTier'],hillRelief:hills};
    }
    return sample;
  }

  sample(cx: number, cz: number, rainfall = true): TerrainSample {
    const {longitude: lon, latitude: lat} = planetCoordinates(cx, cz, this.settings.equatorChunks);
    const terrain = this.terrain(lon, lat);
    // 大尺度干旱带先成片
    const arid=Math.exp(-(((Math.abs(lat)-27)/12)**2));
    let moisture = this.layers.field(spherePoint(lon, lat), 1.8, 2, 79) * .38 + .38 * Math.cos(lat * RAD * 2);
    moisture -= .95 * arid;
    if (this.settings.map === 'earth') {
      for (const [x, y, sx, sy] of [[20, 24, 35, 13], [48, 24, 16, 12], [133, -25, 17, 12], [85, 43, 28, 10]]) {
        moisture -= .5 * Math.exp(-(((lon - x) / sx) ** 2 + ((lat - y) / sy) ** 2));
      }
    }
    if (rainfall && terrain.height > 0) {
      const angle = this.settings.monsoonDirection * RAD;
      let ocean = 0, barrier = terrain.height;
      for (const distance of [2, 5, 10]) {
        const source = this.terrain(((lon - Math.sin(angle) * distance / Math.max(.3, Math.cos(lat * RAD)) + 540) % 360) - 180,
          clamp(lat - Math.cos(angle) * distance, -90, 90));
        if (source.height < 0) ocean += .22;
        barrier = Math.max(barrier, source.height);
      }
      moisture += ocean*(1-arid*.75) - Math.min(.45, (barrier - terrain.height) / 1024);
    }
    return {...terrain, moisture: clamp(moisture, -1, 1)};
  }
}
