import { clamp, smooth } from './TerrainLayers';
import { mountainProfile } from './MountainField';

const RAD = Math.PI / 180;

interface MountainBelt {
  readonly points: readonly (readonly [number, number])[];
  readonly width: number;
  readonly height: number;
}

interface RiverDef {
  readonly name: string;
  readonly nameEn: string;
  readonly points: readonly (readonly [number, number])[];
  readonly width: number;
  readonly sourceHeight: number;
}

interface RegionDef {
  readonly cx: number;
  readonly cy: number;
  readonly rx: number;
  readonly ry: number;
  readonly base: number;
  readonly tier: 0 | 1 | 2 | 3;
}

interface BasinDef {
  readonly cx: number;
  readonly cy: number;
  readonly rx: number;
  readonly ry: number;
  readonly targetHeight: number;
  readonly depth: number;
}

const MOUNTAINS: readonly MountainBelt[] = [
  { points: [[74, 35], [77, 32], [81, 30], [86, 28], [91, 28], [95, 29]], width: 2.5, height: 260 },
  { points: [[80, 31], [85, 31], [90, 31], [94, 31]], width: 2.5, height: 220 },
  { points: [[72, 38], [75, 36], [77, 35]], width: 2.0, height: 260 },
  { points: [[76, 36], [82, 36], [89, 36], [94, 36], [98, 35]], width: 2.2, height: 240 },
  { points: [[70, 42], [75, 42], [80, 42], [86, 43], [94, 43]], width: 2.0, height: 220 },
  { points: [[83, 49], [87, 48], [90, 47], [96, 45]], width: 1.8, height: 180 },
  { points: [[103, 34], [107, 34], [110, 34], [113, 33]], width: 1.6, height: 190 },
  { points: [[98, 32], [99, 29], [100, 26], [101, 23]], width: 2.0, height: 220 },
  { points: [[112, 35], [113, 37], [114, 39], [115, 41]], width: 1.3, height: 140 },
  { points: [[120, 47], [121, 50], [123, 52]], width: 1.8, height: 130 },
  { points: [[126, 41], [128, 42], [130, 44]], width: 1.5, height: 160 },
  { points: [[137, 35], [138, 36], [140, 38], [141, 40]], width: 1.3, height: 160 },
  { points: [[60, 51], [59, 56], [59, 61], [63, 66], [66, 68]], width: 1.8, height: 140 },
  { points: [[40, 44], [44, 43], [48, 41]], width: 1.6, height: 230 },
  { points: [[45, 37], [48, 34], [52, 30], [56, 27]], width: 2.2, height: 190 },
  { points: [[67, 35], [70, 36], [73, 37]], width: 1.8, height: 230 },
  { points: [[5.5, 45], [7.5, 46], [10, 46.5], [13, 46.5], [15, 46]], width: 1.8, height: 210 },
  { points: [[-2, 43], [0, 42.8], [2.5, 42.5]], width: 1.2, height: 180 },
  { points: [[19, 49], [22, 49], [25, 48], [25.5, 46], [23, 45]], width: 1.8, height: 160 },
  { points: [[10, 44], [12, 43], [14, 41], [16, 39]], width: 1.5, height: 150 },
  { points: [[7, 60], [12, 63], [16, 66], [20, 68]], width: 2.2, height: 150 },
  { points: [[-8, 31], [-5, 32], [0, 34], [5, 36]], width: 2.0, height: 190 },
  { points: [[37, 14], [39, 10], [38, 6], [36, 0], [34, -4], [34, -10]], width: 2.5, height: 180 },
  { points: [[28, -29], [29, -29], [30, -28]], width: 1.5, height: 150 },
  { points: [[-73, 10], [-76, 5], [-78, 0], [-79, -5], [-76, -10], [-72, -15], [-68, -18], [-69, -23], [-70, -29], [-71, -34], [-71, -40], [-72, -47], [-70, -53]], width: 2.5, height: 250 },
  { points: [[-125, 57], [-120, 54], [-115, 50], [-112, 45], [-107, 40], [-105, 36]], width: 2.5, height: 220 },
  { points: [[-122, 49], [-121, 44], [-120, 39], [-118, 36]], width: 1.8, height: 210 },
  { points: [[-84, 35], [-81, 37], [-77, 40], [-73, 44], [-69, 47]], width: 2.2, height: 120 },
  { points: [[-105, 28], [-102, 24], [-99, 20]], width: 2.0, height: 180 },
  { points: [[144, -15], [146, -20], [148, -25], [151, -30], [149, -35], [144, -37]], width: 2.2, height: 130 },
  { points: [[168, -45], [170, -44], [172, -42]], width: 1.2, height: 170 },
  { points: [[73, 19], [74, 15], [76, 11], [77, 9]], width: 1.2, height: 140 },
];

const PLATEAUS: readonly RegionDef[] = [
  { cx: 88, cy: 33, rx: 14, ry: 6.5, base: 2048, tier: 3 },
  { cx: 77, cy: 36, rx: 6, ry: 4, base: 2048, tier: 3 },
  { cx: 98, cy: 32, rx: 6, ry: 5, base: 1800, tier: 3 },
  { cx: 108, cy: 38, rx: 8, ry: 5, base: 1024, tier: 2 },
  { cx: 104, cy: 26, rx: 5.5, ry: 4, base: 1024, tier: 2 },
  { cx: -68, cy: -18, rx: 4, ry: 7, base: 1024, tier: 2 },
  { cx: -110, cy: 37, rx: 5.5, ry: 4, base: 1024, tier: 2 },
  { cx: 39, cy: 10, rx: 4.5, ry: 5, base: 1024, tier: 2 },
  { cx: 54, cy: 32, rx: 8, ry: 5, base: 1024, tier: 2 },
  { cx: 33, cy: 39, rx: 6, ry: 3, base: 1024, tier: 2 },
  { cx: -50, cy: -14, rx: 12, ry: 9, base: 512, tier: 1 },
  { cx: 77, cy: 18, rx: 5, ry: 6, base: 512, tier: 1 },
  { cx: 98, cy: 63, rx: 14, ry: 8, base: 512, tier: 1 },
  { cx: 26, cy: -27, rx: 6, ry: 5, base: 512, tier: 1 },
  { cx: -62, cy: 5, rx: 7, ry: 4, base: 512, tier: 1 },
  { cx: 124, cy: -25, rx: 8, ry: 6, base: 512, tier: 1 },
];

const BASINS: readonly BasinDef[] = [
  { cx: 106, cy: 30.5, rx: 3.4, ry: 2.5, targetHeight: 280, depth: 700 },
  { cx: 83, cy: 39.5, rx: 6.5, ry: 3.0, targetHeight: 650, depth: 1300 },
  { cx: 87, cy: 45, rx: 4.0, ry: 2.5, targetHeight: 450, depth: 500 },
  { cx: 21, cy: 0, rx: 7.5, ry: 6.0, targetHeight: 320, depth: 220 },
  { cx: -60, cy: -3, rx: 14.0, ry: 7.0, targetHeight: 60, depth: 400 },
  { cx: -91, cy: 36, rx: 8.0, ry: 7.0, targetHeight: 80, depth: 300 },
  { cx: 117, cy: 34, rx: 6.0, ry: 5.0, targetHeight: 40, depth: 400 },
  { cx: 20, cy: 52, rx: 14.0, ry: 4.5, targetHeight: 50, depth: 300 },
  { cx: 74, cy: 60, rx: 9.0, ry: 8.0, targetHeight: 40, depth: 300 },
  { cx: 83, cy: 26, rx: 9.0, ry: 2.8, targetHeight: 60, depth: 400 },
];

const RIVERS: readonly RiverDef[] = [
  {
    name: '长江', nameEn: 'Yangtze',
    points: [
      [91.5, 33.8], [94.5, 33.2], [97.5, 32.2], [99.5, 29.8], [100.8, 27.5],
      [102.8, 26.5], [104.6, 28.8], [106.6, 29.6], [109.0, 30.8], [111.3, 30.7],
      [114.3, 30.6], [116.0, 29.8], [118.4, 31.4], [119.5, 32.2], [121.8, 31.4],
    ],
    width: 1.8, sourceHeight: 1800,
  },
  {
    name: '黄河', nameEn: 'Yellow River',
    points: [
      [96.0, 35.0], [99.0, 34.5], [101.5, 34.0], [103.5, 36.1], [106.2, 38.8],
      [107.5, 40.2], [109.8, 40.6], [111.2, 40.0], [110.5, 36.5], [110.3, 34.6],
      [112.5, 34.8], [114.5, 34.9], [116.8, 36.7], [119.2, 37.8],
    ],
    width: 1.6, sourceHeight: 1600,
  },
  {
    name: '珠江', nameEn: 'Pearl River',
    points: [
      [104.0, 25.0], [106.8, 24.2], [109.5, 23.6], [111.3, 23.5], [112.8, 23.1], [113.6, 22.3],
    ],
    width: 1.2, sourceHeight: 650,
  },
  {
    name: '湄公河', nameEn: 'Mekong',
    points: [
      [94.0, 33.2], [97.2, 31.1], [99.0, 27.5], [100.8, 22.0], [101.5, 18.0],
      [102.6, 17.9], [104.8, 16.5], [105.8, 13.5], [104.9, 11.6], [106.4, 9.5],
    ],
    width: 1.6, sourceHeight: 1700,
  },
  {
    name: '黑龙江', nameEn: 'Amur',
    points: [
      [120.0, 53.3], [122.5, 53.5], [127.5, 50.2], [131.0, 48.5], [135.0, 48.5],
      [137.0, 51.0], [140.7, 53.2],
    ],
    width: 1.6, sourceHeight: 800,
  },
  {
    name: '雅鲁藏布江', nameEn: 'Brahmaputra',
    points: [
      [82.5, 30.5], [85.5, 29.3], [88.9, 29.3], [91.0, 29.3], [95.3, 29.8],
      [94.8, 28.5], [93.5, 26.8], [90.5, 25.5], [90.5, 22.8],
    ],
    width: 1.8, sourceHeight: 2200,
  },
  {
    name: '恒河', nameEn: 'Ganges',
    points: [
      [79.0, 31.0], [78.2, 29.9], [80.3, 26.5], [83.0, 25.3], [85.2, 25.6],
      [88.5, 24.5], [90.5, 22.5],
    ],
    width: 1.6, sourceHeight: 1200,
  },
  {
    name: '印度河', nameEn: 'Indus',
    points: [
      [81.5, 31.2], [77.6, 34.2], [74.3, 35.9], [72.2, 33.9], [71.0, 30.0],
      [69.5, 27.5], [68.4, 25.4], [67.5, 23.8],
    ],
    width: 1.6, sourceHeight: 1900,
  },
  {
    name: '尼罗河', nameEn: 'Nile',
    points: [
      [32.5, 0.3], [31.5, 5.0], [31.5, 9.5], [32.5, 15.6], [33.5, 18.0],
      [31.5, 19.5], [32.9, 24.1], [32.6, 25.7], [31.2, 30.1], [31.5, 31.5],
    ],
    width: 1.6, sourceHeight: 900,
  },
  {
    name: '刚果河', nameEn: 'Congo',
    points: [
      [26.5, -11.0], [26.0, -5.0], [25.2, 0.5], [22.5, 2.2], [18.3, 0.0],
      [16.0, -2.0], [15.3, -4.3], [12.3, -6.0],
    ],
    width: 2.0, sourceHeight: 700,
  },
  {
    name: '亚马逊河', nameEn: 'Amazon',
    points: [
      [-76.5, -4.5], [-73.2, -3.7], [-70.0, -4.2], [-65.0, -3.2], [-60.0, -3.1],
      [-55.0, -2.2], [-51.0, -1.5], [-50.0, 0.0],
    ],
    width: 2.2, sourceHeight: 900,
  },
  {
    name: '密西西比河', nameEn: 'Mississippi',
    points: [
      [-111.5, 45.5], [-105.0, 48.0], [-100.8, 46.8], [-95.9, 41.2], [-94.6, 39.1],
      [-90.2, 38.8], [-89.2, 37.0], [-90.0, 35.1], [-91.2, 31.0], [-90.1, 29.9], [-89.2, 29.1],
    ],
    width: 2.0, sourceHeight: 1100,
  },
  {
    name: '多瑙河', nameEn: 'Danube',
    points: [
      [8.2, 48.0], [12.1, 49.0], [16.4, 48.2], [19.0, 47.5], [20.5, 44.8],
      [22.5, 44.7], [26.0, 43.8], [28.0, 45.4], [29.6, 45.3],
    ],
    width: 1.4, sourceHeight: 550,
  },
  {
    name: '莱茵河', nameEn: 'Rhine',
    points: [
      [8.8, 46.7], [9.4, 47.6], [7.6, 47.6], [7.8, 48.6], [7.6, 50.3],
      [7.0, 50.9], [6.0, 51.7], [4.1, 51.9],
    ],
    width: 1.2, sourceHeight: 600,
  },
  {
    name: '伏尔加河', nameEn: 'Volga',
    points: [
      [32.5, 57.2], [36.0, 56.8], [39.9, 57.6], [44.0, 56.3], [49.1, 55.8],
      [50.1, 53.2], [47.5, 51.0], [44.5, 48.7], [48.0, 46.0],
    ],
    width: 1.6, sourceHeight: 450,
  },
  {
    name: '叶尼塞河', nameEn: 'Yenisey',
    points: [
      [97.0, 51.5], [94.4, 51.7], [92.9, 56.0], [92.1, 58.5], [90.0, 62.0],
      [86.6, 67.5], [82.5, 72.0],
    ],
    width: 1.8, sourceHeight: 850,
  },
  {
    name: '鄂毕河', nameEn: 'Ob',
    points: [
      [88.0, 47.0], [84.0, 48.0], [78.0, 52.0], [73.4, 55.0], [69.0, 61.0],
      [66.6, 66.5], [73.0, 66.5],
    ],
    width: 1.8, sourceHeight: 750,
  },
  {
    name: '勒拿河', nameEn: 'Lena',
    points: [
      [106.0, 53.0], [108.1, 57.8], [115.0, 60.5], [129.7, 62.0], [123.4, 66.8], [127.0, 72.5],
    ],
    width: 1.8, sourceHeight: 800,
  },
  {
    name: '巴拉那河', nameEn: 'Parana',
    points: [
      [-47.0, -19.0], [-51.0, -22.5], [-54.3, -24.1], [-54.6, -25.5], [-58.8, -27.5],
      [-60.6, -32.9], [-58.4, -34.6],
    ],
    width: 1.6, sourceHeight: 600,
  },
  {
    name: '科罗拉多河', nameEn: 'Colorado',
    points: [
      [-105.8, 40.4], [-109.5, 38.6], [-111.5, 37.0], [-112.5, 36.1], [-114.7, 36.0],
      [-114.8, 31.8],
    ],
    width: 1.2, sourceHeight: 1400,
  },
  {
    name: '墨累-达令河', nameEn: 'Murray-Darling',
    points: [
      [148.3, -36.5], [146.9, -36.1], [144.0, -35.5], [142.2, -34.2], [139.0, -35.6],
    ],
    width: 1.4, sourceHeight: 500,
  },
];

interface PreparedRiver {
  readonly def: RiverDef;
  readonly minX: number;
  readonly maxX: number;
  readonly minY: number;
  readonly maxY: number;
  readonly segmentLengths: readonly number[];
  readonly totalLength: number;
}

const PREPARED_RIVERS: readonly PreparedRiver[] = RIVERS.map(def => {
  let minX = Infinity, maxX = -Infinity, minY = Infinity, maxY = -Infinity;
  const lengths: number[] = [];
  let total = 0;
  for (let i = 0; i < def.points.length; i++) {
    const [x, y] = def.points[i];
    if (x < minX) minX = x; if (x > maxX) maxX = x;
    if (y < minY) minY = y; if (y > maxY) maxY = y;
    if (i > 0) {
      const [px, py] = def.points[i - 1];
      const cos = Math.max(.2, Math.cos(((y + py) * .5) * RAD));
      const len = Math.hypot((x - px) * cos, y - py);
      lengths.push(len);
      total += len;
    }
  }
  return {
    def, minX: minX - def.width, maxX: maxX + def.width,
    minY: minY - def.width, maxY: maxY + def.width,
    segmentLengths: lengths, totalLength: Math.max(1e-4, total),
  };
});

export function earthMountainsRelief(lon: number, lat: number): number {
  let relief = 0;
  const cos = Math.max(.2, Math.cos(lat * RAD));
  for (const belt of MOUNTAINS) {
    let contribution = 0;
    for (let i = 1; i < belt.points.length; i++) {
      const [ax, ay] = belt.points[i - 1], [bx, by] = belt.points[i];
      let dx = bx - ax;
      if (dx > 180) dx -= 360;
      else if (dx < -180) dx += 360;
      const sdx = dx * cos, sdy = by - ay;
      let px = lon - ax;
      if (px > 180) px -= 360;
      else if (px < -180) px += 360;
      const spx = px * cos, spy = lat - ay;
      const t = clamp((spx * sdx + spy * sdy) / (sdx * sdx + sdy * sdy));
      const distance = Math.hypot(spx - sdx * t, spy - sdy * t);
      contribution = Math.max(contribution, belt.height * mountainProfile(distance, belt.width, belt.width * 2.8));
    }
    relief += contribution;
  }
  return Math.min(320, relief);
}

export function earthMacroPlate(lon: number, lat: number): { base: number; tier: 0 | 1 | 2 | 3 } {
  let bestBase = 0;
  let bestTier: 0 | 1 | 2 | 3 = 0;
  const cos = Math.max(.2, Math.cos(lat * RAD));
  for (const reg of PLATEAUS) {
    let dx = lon - reg.cx;
    if (dx > 180) dx -= 360;
    else if (dx < -180) dx += 360;
    const dy = lat - reg.cy;
    const d2 = ((dx * cos) / reg.rx) ** 2 + (dy / reg.ry) ** 2;
    if (d2 < 1) {
      const weight = smooth(1 - d2);
      const val = reg.base * weight;
      if (val > bestBase) {
        bestBase = val;
        if (weight > .35 && reg.tier > bestTier) bestTier = reg.tier;
      }
    }
  }
  return { base: bestBase, tier: bestTier };
}

export function earthBasinDepression(lon: number, lat: number): number {
  let sink = 0;
  const cos = Math.max(.2, Math.cos(lat * RAD));
  for (const basin of BASINS) {
    let dx = lon - basin.cx;
    if (dx > 180) dx -= 360;
    else if (dx < -180) dx += 360;
    const dy = lat - basin.cy;
    const d2 = ((dx * cos) / basin.rx) ** 2 + (dy / basin.ry) ** 2;
    if (d2 < 1) {
      const factor = smooth(1 - d2);
      sink = Math.max(sink, basin.depth * factor);
    }
  }
  return sink;
}

export interface EarthRiverSample {
  readonly inValley: boolean;
  readonly valleyFactor: number;
  readonly bedElevation: number;
  readonly incision: number;
  readonly distance: number;
}

export function earthRiverSample(lon: number, lat: number): EarthRiverSample {
  let bestFactor = 0;
  let targetBed = 0;
  let bestIncision = 0;
  let minDistance = Infinity;
  const cos = Math.max(.2, Math.cos(lat * RAD));

  for (const prep of PREPAREDRIVERS) {
    if (lon < prep.minX || lon > prep.maxX || lat < prep.minY || lat > prep.maxY) continue;
    const def = prep.def;
    let accum = 0;
    for (let i = 1; i < def.points.length; i++) {
      const [ax, ay] = def.points[i - 1], [bx, by] = def.points[i];
      const segLen = prep.segmentLengths[i - 1];
      let dx = bx - ax;
      if (dx > 180) dx -= 360;
      else if (dx < -180) dx += 360;
      const sdx = dx * cos, sdy = by - ay;
      let px = lon - ax;
      if (px > 180) px -= 360;
      else if (px < -180) px += 360;
      const spx = px * cos, spy = lat - ay;
      const t = clamp((spx * sdx + spy * sdy) / (sdx * sdx + sdy * sdy));
      const dist = Math.hypot(spx - sdx * t, spy - sdy * t);
      if (dist < def.width) {
        const factor = smooth(1 - dist / def.width);
        const progress = clamp((accum + segLen * t) / prep.totalLength);
        const bed = def.sourceHeight * ((1 - progress) ** 1.6);
        const incision = Math.max(0, 1 - dist / (def.width * .45));
        if (factor > bestFactor) {
          bestFactor = factor;
          targetBed = bed;
          bestIncision = incision;
          minDistance = dist;
        }
      }
      accum += segLen;
    }
  }

  return {
    inValley: bestFactor > 0,
    valleyFactor: bestFactor,
    bedElevation: targetBed,
    incision: bestIncision,
    distance: minDistance,
  };
}

const PREPAREDRIVERS = PREPARED_RIVERS;

export function earthMoistureField(lon: number, lat: number): number {
  let moisture = .15 + .35 * Math.cos(lat * RAD * 2);
  const cos = Math.max(.2, Math.cos(lat * RAD));

  const aridBlobs = [
    { cx: 16, cy: 23, rx: 28, ry: 9, w: .92 },
    { cx: 46, cy: 24, rx: 11, ry: 8, w: .90 },
    { cx: 56, cy: 32, rx: 8, ry: 5, w: .70 },
    { cx: 92, cy: 41, rx: 16, ry: 5.5, w: .85 },
    { cx: 132, cy: -25, rx: 14, ry: 8, w: .88 },
    { cx: -70, cy: -22, rx: 3, ry: 6, w: .90 },
    { cx: 22, cy: -23, rx: 6, ry: 6, w: .80 },
    { cx: -113, cy: 33, rx: 6, ry: 5, w: .75 },
  ];

  for (const b of aridBlobs) {
    let dx = lon - b.cx;
    if (dx > 180) dx -= 360;
    else if (dx < -180) dx += 360;
    const dy = lat - b.cy;
    const d2 = ((dx * cos) / b.rx) ** 2 + (dy / b.ry) ** 2;
    if (d2 < 1) moisture -= b.w * smooth(1 - d2);
  }

  const rainBlobs = [
    { cx: -60, cy: -3, rx: 16, ry: 10, w: .75 },
    { cx: 22, cy: 0, rx: 11, ry: 8, w: .70 },
    { cx: 108, cy: 5, rx: 18, ry: 12, w: .70 },
    { cx: 115, cy: 29, rx: 14, ry: 10, w: .55 },
    { cx: 82, cy: 22, rx: 12, ry: 8, w: .60 },
    { cx: -82, cy: 36, rx: 12, ry: 9, w: .42 },
    { cx: 12, cy: 50, rx: 15, ry: 8, w: .42 },
  ];

  for (const b of rainBlobs) {
    let dx = lon - b.cx;
    if (dx > 180) dx -= 360;
    else if (dx < -180) dx += 360;
    const dy = lat - b.cy;
    const d2 = ((dx * cos) / b.rx) ** 2 + (dy / b.ry) ** 2;
    if (d2 < 1) moisture += b.w * smooth(1 - d2);
  }

  return clamp(moisture, -1, 1);
}

export function earthTemperatureField(lon: number, lat: number, elevation = 0): number {
  const blend = Math.max(0, Math.cos(lat * RAD)) ** 1.35;
  let temp = -32 + 62 * blend;

  const cos = Math.max(.2, Math.cos(lat * RAD));
  const gulfDx = lon - 10, gulfDy = lat - 52;
  const gulfD2 = ((gulfDx * cos) / 18) ** 2 + (gulfDy / 10) ** 2;
  if (gulfD2 < 1) temp += 4.5 * smooth(1 - gulfD2);

  const sibDx = lon - 100, sibDy = lat - 60;
  const sibD2 = ((sibDx * cos) / 22) ** 2 + (sibDy / 10) ** 2;
  if (sibD2 < 1) temp -= 5.5 * smooth(1 - sibD2);

  temp -= Math.max(0, elevation) * .015;
  return temp;
}
