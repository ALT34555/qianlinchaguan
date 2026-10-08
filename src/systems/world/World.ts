/** 世界 / 区块管理（主线程） */
import * as THREE from 'three';
import type { ClimateWeights, WorldGeneration } from './WorldSettings';
import { CHUNK_SIZE } from '../../core/config';
import { renderChunkOffsets } from '../../core/RenderDistance';
import type { MeshData } from './ChunkMesher';
import type { ChunkResultMessage } from './ChunkProtocol';
import { ChunkWorkerPool } from './ChunkWorkerPool';
import { WorldGenerator } from './WorldGenerator';
import { terrainHeightAt, terrainGroundUnder } from './TerrainSurface';
import { WorldLighting } from './WorldLighting';
import { WaterMaterial, decodeSurfaceColors } from './WaterMaterial';
import { waterHeightAt } from './WaterSurface';
import type {WaterField} from './DynamicWater';
import { WaterfallMist } from './WaterfallMist';
import { ArtificialWorld } from './artificial/ArtificialWorld';
import { allowsNaturalDecoration } from './artificial/ArtificialState';
import { territoryFlag } from './artificial/TerritoryFlag';

export interface LoadedChunk {
  cx: number;
  cz: number;
  type: number;
  /** 区块内高度图 64x64 */
  heights: Float32Array;
  surfaces: Uint32Array;
  waterLevels: Float32Array;
  waterField: WaterField;
  minimap: ImageData;
  terrain: THREE.Mesh | null;
  water: THREE.Mesh | null;
  decoration: THREE.Mesh | null;
  rocks: THREE.Mesh | null;
  mist: THREE.Points | null;
  mistEmitters: Float32Array | null;
  flag: THREE.Mesh | null;
}

const chunkKey = (cx: number, cz: number) => `${cx},${cz}`;

/** 每帧最多上传到 GPU 的区块数，避免卡顿 */
const UPLOADS_PER_FRAME = 4;

export class World {
  readonly artificial: ArtificialWorld;
  private artificialRevision = -1;
  readonly generator: WorldGenerator;
  private readonly chunks = new Map<string, LoadedChunk>();
  private readonly pending = new Set<string>();
  private readonly ready: ChunkResultMessage[] = [];
  private readonly pool: ChunkWorkerPool;
  private readonly group = new THREE.Group();
  private readonly terrainMaterial = new THREE.MeshStandardMaterial({ vertexColors: true, flatShading: true, roughness: .95 });
  private readonly waterMaterial = new WaterMaterial();
  private readonly decorationMaterial = new THREE.MeshStandardMaterial({ vertexColors: true, flatShading: true, roughness: .95, side: THREE.DoubleSide });
  private readonly mist = new WaterfallMist();
  private mistEnabled = true;
  private readonly lighting: WorldLighting;
  /** 以距离排序的加载偏移表（圆形范围） */
  private offsets: [number, number][] = [];
  private renderDistance = 0;
  private vegetationDistance = 16;
  private centerCx = Number.NaN;
  private centerCz = Number.NaN;
  private readonly listeners: ((c: LoadedChunk) => void)[] = [];

  constructor(scene: THREE.Scene, readonly seed: number, renderDistance: number, climateWeights: ClimateWeights, generation?: WorldGeneration, artificial?: ArtificialWorld) {
    this.generator = new WorldGenerator(seed, climateWeights, generation);
    this.artificial = artificial ?? new ArtificialWorld(this.generator);
    this.terrainMaterial.onBeforeCompile = decodeSurfaceColors;
    this.decorationMaterial.onBeforeCompile = decodeSurfaceColors;
    this.lighting = new WorldLighting(scene);
    this.pool = new ChunkWorkerPool(seed, climateWeights, this.generator.generation, (msg) => this.ready.push(msg));
    this.setRenderDistance(renderDistance);
    scene.add(this.group);
  }

  updateLighting(dayRatio: number, season: number, camera: THREE.Vector3, time: number): THREE.Color {
    const state = this.lighting.update(dayRatio, season, camera);
    this.terrainMaterial.color.copy(this.lighting.tint);
    this.decorationMaterial.color.copy(this.lighting.tint);
    this.waterMaterial.update(time, this.lighting, state.daylight, state.night);
    this.mist.update(time, state.daylight);
    return this.lighting.fogColor;
  }

  setMistEnabled(on: boolean): void {
    if (on === this.mistEnabled) return;
    this.mistEnabled = on;
    for (const chunk of this.chunks.values()) {
      if (on && !chunk.mist && chunk.mistEmitters) {
        chunk.mist = this.mist.build(chunk.mistEmitters, chunk.cx, chunk.cz, CHUNK_SIZE);
        if (chunk.mist) {
          chunk.mist.visible = this.inRange(chunk.cx, chunk.cz, 0);
          this.group.add(chunk.mist);
        }
      } else if (!on && chunk.mist) {
        this.mist.disposePoints(chunk.mist);
        chunk.mist = null;
      }
    }
  }

  setMistProjection(fov: number, viewportHeight: number, pixelRatio: number): void {
    this.mist.setProjection(fov, viewportHeight, pixelRatio);
  }

  setRenderDistance(rd: number): void {
    this.renderDistance = rd;
    this.offsets = renderChunkOffsets(rd);
    this.centerCx = Number.NaN; // 强制重新评估卸载
  }

  setVegetationDistance(distance: number): void {
    this.vegetationDistance = distance;
    for (const chunk of this.chunks.values()) this.updateChunkVisibility(chunk);
  }

  onChunkLoaded(fn: (c: LoadedChunk) => void): void {
    this.listeners.push(fn);
  }

  get loadedCount(): number {
    return this.chunks.size;
  }

  get pendingCount(): number {
    return this.pending.size;
  }

  getChunk(cx: number, cz: number): LoadedChunk | undefined {
    return this.chunks.get(chunkKey(cx, cz));
  }

  isLoaded(cx: number, cz: number): boolean {
    return this.chunks.has(chunkKey(cx, cz));
  }

  getChunkType(cx: number, cz: number): number {
    return this.artificial.getClaim(cx, cz)?.type ?? this.getChunk(cx, cz)?.type ?? this.generator.getChunkType(cx, cz);
  }

  /** 原始格中心高度：仅供连续地表的公共插值规则采样。 */
  private readonly columnHeight = (wx: number, wz: number): number => {
    const bx = Math.floor(wx);
    const bz = Math.floor(wz);
    const cx = Math.floor(bx / CHUNK_SIZE);
    const cz = Math.floor(bz / CHUNK_SIZE);
    const c = this.chunks.get(chunkKey(cx, cz));
    if (c) return c.heights[(bz - cz * CHUNK_SIZE) * CHUNK_SIZE + (bx - cx * CHUNK_SIZE)];
    return this.generator.getHeight(bx, bz);
  };

  /** 精确采样可见三角面，跨区块/负坐标也采用同一规则。 */
  getHeight(wx: number, wz: number): number {
    return terrainHeightAt(wx, wz, this.seed, this.columnHeight);
  }

  getGroundUnder(wx: number, wz: number, halfWidth: number): number {
    return terrainGroundUnder(wx, wz, halfWidth, this.seed, (x, z) => this.getHeight(x, z));
  }

  private readonly columnWater = (wx: number, wz: number): number => {
    const bx = Math.floor(wx), bz = Math.floor(wz);
    const cx = Math.floor(bx / CHUNK_SIZE), cz = Math.floor(bz / CHUNK_SIZE);
    const c = this.getChunk(cx, cz);
    return c ? c.waterLevels[(bz - cz * CHUNK_SIZE) * CHUNK_SIZE + bx - cx * CHUNK_SIZE]
      : this.generator.getWaterLevel(bx, bz);
  };

  getWaterLevel(wx: number, wz: number): number {
    return waterHeightAt(wx, wz, this.seed, this.columnHeight, this.columnWater);
  }

  update(playerX: number, playerZ: number): void {
    if (this.artificialRevision !== this.artificial.revision) {
      for (const chunk of this.chunks.values()) this.updateTerritory(chunk);
      this.artificialRevision = this.artificial.revision;
    }
    const pcx = Math.floor(playerX / CHUNK_SIZE);
    const pcz = Math.floor(playerZ / CHUNK_SIZE);

    if (pcx !== this.centerCx || pcz !== this.centerCz) {
      this.centerCx = pcx;
      this.centerCz = pcz;
      this.unloadFar();
    }

    // 由近到远派发生成任务
    if (this.pool.capacity > 0) {
      for (const [dx, dz] of this.offsets) {
        const cx = pcx + dx;
        const cz = pcz + dz;
        const key = chunkKey(cx, cz);
        if (this.chunks.has(key) || this.pending.has(key)) continue;
        if (!this.pool.request(cx, cz, this.artificial.getClaim(cx, cz)?.type)) break;
        this.pending.add(key);
      }
    }

    // 上传已完成的区块
    for (let i = 0; i < UPLOADS_PER_FRAME && this.ready.length > 0; i++) {
      this.addChunk(this.ready.shift()!);
    }
  }

  private inRange(cx: number, cz: number, margin: number): boolean {
    const dx = cx - this.centerCx;
    const dz = cz - this.centerCz;
    const r = this.renderDistance + 0.5 + margin;
    return dx * dx + dz * dz <= r * r;
  }

  private unloadFar(): void {
    for (const [key, c] of this.chunks) {
      this.updateChunkVisibility(c);
      if (this.inRange(c.cx, c.cz, 1)) continue;
      this.disposeChunk(c);
      this.chunks.delete(key);
    }
  }

  private updateChunkVisibility(chunk: LoadedChunk): void {
    const visible = this.inRange(chunk.cx, chunk.cz, 0);
    if (chunk.terrain) chunk.terrain.visible = visible;
    if (chunk.water) chunk.water.visible = visible;
    if (chunk.decoration) chunk.decoration.visible = visible && (chunk.cx - this.centerCx) ** 2 + (chunk.cz - this.centerCz) ** 2 <= (this.vegetationDistance + .5) ** 2;
    if (chunk.rocks) chunk.rocks.visible = visible;
    if (chunk.mist) chunk.mist.visible = visible;
    if (chunk.flag) chunk.flag.visible = visible;
  }

  private buildMesh(data: MeshData, material: THREE.Material, cx: number, cz: number): THREE.Mesh {
    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position', new THREE.BufferAttribute(data.positions, 3));
    geo.setAttribute('color', new THREE.BufferAttribute(data.colors, 3, true));
    if (data.waterDepths) geo.setAttribute('waterDepth', new THREE.BufferAttribute(data.waterDepths, 1));
    if (data.waterFlows) geo.setAttribute('waterFlow',new THREE.BufferAttribute(data.waterFlows,2));
    if (data.waterFalls) geo.setAttribute('waterFall',new THREE.BufferAttribute(data.waterFalls,1));
    geo.setIndex(new THREE.BufferAttribute(data.indices, 1));
    geo.computeVertexNormals();
    geo.computeBoundingSphere();
    const mesh = new THREE.Mesh(geo, material);
    mesh.position.set(cx * CHUNK_SIZE, 0, cz * CHUNK_SIZE);
    mesh.matrixAutoUpdate = false;
    mesh.updateMatrix();
    return mesh;
  }

  private addChunk(msg: ChunkResultMessage): void {
    const key = chunkKey(msg.cx, msg.cz);
    this.pending.delete(key);
    // 玩家已远离：丢弃
    if (!this.inRange(msg.cx, msg.cz, 1) || this.chunks.has(key)) return;

    const chunk: LoadedChunk = {
      cx: msg.cx,
      cz: msg.cz,
      type: msg.type,
      heights: msg.heights,
      surfaces: msg.surfaces,
      waterLevels: msg.waterLevels,
      waterField:msg.waterField,
      minimap: new ImageData(msg.minimap, CHUNK_SIZE, CHUNK_SIZE),
      terrain: msg.terrain ? this.buildMesh(msg.terrain, this.terrainMaterial, msg.cx, msg.cz) : null,
      water: msg.water ? this.buildMesh(msg.water, this.waterMaterial, msg.cx, msg.cz) : null,
      decoration: msg.decoration ? this.buildMesh(msg.decoration, this.decorationMaterial, msg.cx, msg.cz) : null,
      rocks: msg.rocks ? this.buildMesh(msg.rocks, this.decorationMaterial, msg.cx, msg.cz) : null,
      mist: null,
      flag: null,
      mistEmitters: msg.mist ?? null,
    };
    if (chunk.terrain) this.group.add(chunk.terrain);
    if (chunk.rocks) this.group.add(chunk.rocks);
    if (chunk.decoration) {
      chunk.decoration.name = `decoration:${key}`;
      this.group.add(chunk.decoration);
    }
    if (chunk.water) {
      chunk.water.renderOrder = 1;
      this.group.add(chunk.water);
    }
    if (this.mistEnabled && chunk.mistEmitters) {
      chunk.mist = this.mist.build(chunk.mistEmitters, msg.cx, msg.cz, CHUNK_SIZE);
      if (chunk.mist) this.group.add(chunk.mist);
    }
    this.chunks.set(key, chunk);
    this.updateTerritory(chunk);
    this.updateChunkVisibility(chunk);
    for (const fn of this.listeners) fn(chunk);
  }

  private disposeChunk(c: LoadedChunk): void {
    for (const m of [c.terrain, c.water, c.decoration, c.rocks, c.flag]) {
      if (!m) continue;
      this.group.remove(m);
      m.geometry.dispose();
    }
    if (c.mist) {
      this.mist.disposePoints(c.mist);
      c.mist = null;
    }
  }

  dispose(): void {
    for (const c of this.chunks.values()) this.disposeChunk(c);
    this.chunks.clear();
    this.pool.dispose();
    this.terrainMaterial.dispose();
    this.waterMaterial.dispose();
    this.decorationMaterial.dispose();
    this.mist.dispose();
    this.lighting.dispose();
    this.group.removeFromParent();
  }

  private updateTerritory(chunk: LoadedChunk): void {
    const claim = this.artificial.getClaim(chunk.cx, chunk.cz);
    if (!claim) return;
    chunk.type = claim.type;
    if (!allowsNaturalDecoration(claim.type)) {
      for (const name of ['decoration', 'rocks'] as const) {
        const mesh = chunk[name];
        if (mesh) { this.group.remove(mesh); mesh.geometry.dispose(); chunk[name] = null; }
      }
    }
    const t = this.artificial.territories.get(claim.faction)!;
    if (!chunk.flag && t.cx === this.artificial.generator.wrap(chunk.cx) && t.cz === chunk.cz) {
      const h = this.getHeight(chunk.cx * CHUNK_SIZE + CHUNK_SIZE / 2 + .5, chunk.cz * CHUNK_SIZE + CHUNK_SIZE / 2 + .5);
      chunk.flag = this.buildMesh(territoryFlag(t, h), this.decorationMaterial, chunk.cx, chunk.cz);
      chunk.flag.name = `territory:${t.id}`;
      this.group.add(chunk.flag);
    }
    this.updateChunkVisibility(chunk);
  }
}
