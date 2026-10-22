import { mercatorZ, inverseMercatorZ } from '../AtlasProjection';
import { TAU, clamp } from './tiles.mjs';
import type { PlanetView } from '../PlanetAtlasLayer';

export interface AtlasCameraSnapshot {
  width: number;
  height: number;
  view: 'globe' | 'mercator' | 'projection';
  lon: number;
  lat: number;
  scale: number;
  centerCx: number;
  centerCz: number;
  panX: number;
  panY: number;
  zoomScale: number;
  baseCellSize: number;
  equatorChunks: number;
  globeRadius: number;
  isGlobe: boolean;
  isMercator: boolean;
}

export interface AtlasCameraOptions {
  equatorChunks: number;
  isPlanet: boolean;
  baseCellSize?: number;
  initialCenter?: { cx: number; cz: number };
  initialView?: PlanetView;
  preferredOverview?: PlanetView;
  onViewChange?: () => void;
}

// 舆图相机与投影控制
export class AtlasCameraController {
  centerCx = 0;
  centerCz = 0;
  panX = 0;
  panY = 0;
  scale = 1.0;
  baseCellSize: number;
  equatorChunks: number;
  isPlanet: boolean;
  planetView: PlanetView = 'globe';
  preferredOverview: PlanetView = 'globe';
  private onViewChange?: () => void;

  constructor(options: AtlasCameraOptions) {
    this.equatorChunks = options.equatorChunks;
    this.isPlanet = options.isPlanet;
    this.baseCellSize = options.baseCellSize ?? 36;
    this.planetView = options.initialView ?? (options.isPlanet ? 'globe' : 'projection');
    this.preferredOverview = options.preferredOverview ?? (options.isPlanet ? 'globe' : 'projection');
    this.onViewChange = options.onViewChange;
    if (options.initialCenter) {
      this.centerCx = options.initialCenter.cx;
      this.centerCz = options.initialCenter.cz;
    }
  }

  get isGlobe(): boolean { return this.planetView === 'globe'; }
  get isMercator(): boolean { return this.planetView === 'mercator'; }
  get globeRadius(): number {
    return this.baseCellSize * this.scale * this.equatorChunks / TAU;
  }
  get overviewScale(): number {
    return 1000 / (this.equatorChunks * this.baseCellSize);
  }
  get projectionMinScale(): number {
    return 1000 / (this.equatorChunks * this.baseCellSize);
  }

  worldZ(z: number): number {
    return this.isMercator ? inverseMercatorZ(z, this.equatorChunks) : z;
  }

  mapZ(z: number): number {
    return this.isMercator ? mercatorZ(z, this.equatorChunks) : z;
  }

  snapshot(width: number, height: number): AtlasCameraSnapshot {
    const size = this.equatorChunks;
    const cell = this.baseCellSize * this.scale;
    const isGlobe = this.isGlobe;
    const lon = (this.centerCx + (isGlobe ? 0.5 : -this.panX / cell)) / size * TAU;
    const lat = -(isGlobe ? this.centerCz + 0.5 : this.worldZ(this.mapZ(this.centerCz) - this.panY / cell)) / size * TAU;
    const scale = isGlobe ? this.globeRadius : cell * size / TAU;
    const view: 'globe' | 'mercator' | 'projection' = isGlobe ? 'globe' : this.isMercator ? 'mercator' : 'projection';

    return {
      width,
      height,
      view,
      lon,
      lat,
      scale,
      centerCx: this.centerCx,
      centerCz: this.centerCz,
      panX: this.panX,
      panY: this.panY,
      zoomScale: this.scale,
      baseCellSize: this.baseCellSize,
      equatorChunks: this.equatorChunks,
      globeRadius: this.globeRadius,
      isGlobe,
      isMercator: this.isMercator,
    };
  }

  panBy(dx: number, dy: number): void {
    if (this.isGlobe) {
      const radius = this.globeRadius;
      if (radius <= 0) return;
      this.centerCx -= dx / radius * (this.equatorChunks / TAU);
      this.centerCz -= dy / radius * (this.equatorChunks / TAU);
      const maxLat = this.equatorChunks / 4 - 0.5;
      this.centerCz = clamp(this.centerCz, -maxLat, maxLat);
      this.centerCx = ((this.centerCx + this.equatorChunks / 2) % this.equatorChunks + this.equatorChunks) % this.equatorChunks - this.equatorChunks / 2;
    } else {
      this.panX += dx;
      this.panY += dy;
      if (this.isPlanet) {
        const cell = this.baseCellSize * this.scale;
        const total = this.equatorChunks * cell;
        if (total > 0) this.panX = ((this.panX % total) + total) % total;
      }
    }
    this.onViewChange?.();
  }

  centerAt(cx: number, cz: number): void {
    this.scale = 1;
    this.planetView = this.preferredOverview === 'mercator' ? 'mercator' : 'projection';
    this.centerCx = cx;
    this.centerCz = cz;
    this.panX = 0;
    this.panY = 0;
    this.onViewChange?.();
  }

  resetView(): void {
    if (this.isPlanet) {
      this.planetView = this.preferredOverview;
      this.panX = 0;
      this.panY = 0;
      this.onViewChange?.();
      return;
    }
    this.scale = 1.0;
    this.panX = 0;
    this.panY = 0;
    this.onViewChange?.();
  }

  showOverview(width = 800, height = 600): void {
    if (!this.isPlanet) return;
    this.panX = 0;
    this.panY = 0;
    this.planetView = this.preferredOverview;
    const equatorPx = this.equatorChunks * this.baseCellSize;
    if (this.isGlobe) {
      const fitRadius = Math.min(width, height) * 0.43;
      this.scale = fitRadius * TAU / equatorPx;
    } else {
      this.scale = Math.min((width - 40) / equatorPx, (height - 60) / (equatorPx * 0.5));
    }
    this.onViewChange?.();
  }

  getCenterCoordinates(): { cx: number; cz: number } {
    if (this.isGlobe) return { cx: Math.round(this.centerCx), cz: Math.round(this.centerCz) };
    const cell = this.baseCellSize * this.scale;
    return {
      cx: Math.round(this.centerCx - this.panX / cell),
      cz: Math.round(this.worldZ(this.mapZ(this.centerCz) - this.panY / cell)),
    };
  }

  getScalePercent(): number {
    return Math.round(this.scale * 100);
  }
}
