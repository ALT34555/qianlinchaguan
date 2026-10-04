/**
 * 世界 / 区块管理（主线程）：按玩家位置加载、卸载区块，并把 Worker 产出的网格挂进场景。
 */
import * as THREE from 'three';
import type { ClimateWeights } from './WorldSettings';
import { CHUNK_SIZE } from '../../core/config';
import type { MeshData } from './ChunkMesher';
import type { ChunkResultMessage } from './ChunkProtocol';
import { ChunkWorkerPool } from './ChunkWorkerPool';
import { WorldGenerator } from './WorldGenerator';

export interface LoadedChunk {
  cx: number;
  cz: number;
  type: number;
  /** 区块内高度图 64x64 */
  heights: Int16Array;
  surfaces: Uint8Array;
  waterLevels: Float32Array;
  minimap: ImageData;
  terrain: THREE.Mesh | null;
  water: THREE.Mesh | null;
}

const chunkKey = (cx: number, cz: number) => `${cx},${cz}`;

/** 每帧最多上传到 GPU 的区块数，避免卡顿 */
const UPLOADS_PER_FRAME = 4;

export class World {
  readonly generator: WorldGenerator;
  private readonly chunks = new Map<string, LoadedChunk>();
  private readonly pending = new Set<string>();
  private readonly ready: ChunkResultMessage[] = [];
  private readonly pool: ChunkWorkerPool;
  private readonly group = new THREE.Group();
  private readonly terrainMaterial = new THREE.MeshBasicMaterial({ vertexColors: true });
  private readonly waterMaterial = new THREE.MeshBasicMaterial({
    vertexColors: true,
    transparent: true,
    opacity: 0.66,
    depthWrite: false,
    side: THREE.DoubleSide,
  });
  /** 以距离排序的加载偏移表（圆形范围） */
  private offsets: [number, number][] = [];
  private renderDistance = 0;
  private centerCx = Number.NaN;
  private centerCz = Number.NaN;
  private readonly listeners: ((c: LoadedChunk) => void)[] = [];

  constructor(scene: THREE.Scene, readonly seed: number, renderDistance: number, climateWeights: ClimateWeights) {
    this.generator = new WorldGenerator(seed, climateWeights);
    this.pool = new ChunkWorkerPool(seed, climateWeights, (msg) => this.ready.push(msg));
    this.setRenderDistance(renderDistance);
    scene.add(this.group);
  }

  setGlobalBrightness(brightness: number, season: number): void {
    const c = new THREE.Color(0xffffff);
    // Apply season tint
    if (season === 0) c.lerp(new THREE.Color('#e6ffe6'), 0.1); // Spring
    else if (season === 1) c.lerp(new THREE.Color('#ffe6e6'), 0.1); // Summer
    else if (season === 2) c.lerp(new THREE.Color('#ffffe6'), 0.1); // Autumn
    else if (season === 3) c.lerp(new THREE.Color('#e6e6ff'), 0.1); // Winter
    
    c.multiplyScalar(brightness);
    this.terrainMaterial.color.copy(c);
    this.waterMaterial.color.copy(c);
  }

  setRenderDistance(rd: number): void {
    this.renderDistance = rd;
    this.offsets = [];
    for (let dz = -rd; dz <= rd; dz++) {
      for (let dx = -rd; dx <= rd; dx++) {
        if (dx * dx + dz * dz <= (rd + 0.5) * (rd + 0.5)) this.offsets.push([dx, dz]);
      }
    }
    this.offsets.sort((a, b) => a[0] * a[0] + a[1] * a[1] - (b[0] * b[0] + b[1] * b[1]));
    this.centerCx = Number.NaN; // 强制重新评估卸载
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
    return this.getChunk(cx, cz)?.type ?? this.generator.getChunkType(cx, cz);
  }

  /** 地表高度（顶面 y）。已加载区块查表，否则直接用生成器计算（结果一致）。 */
  getHeight(wx: number, wz: number): number {
    const bx = Math.floor(wx);
    const bz = Math.floor(wz);
    const cx = Math.floor(bx / CHUNK_SIZE);
    const cz = Math.floor(bz / CHUNK_SIZE);
    const c = this.chunks.get(chunkKey(cx, cz));
    if (c) return c.heights[(bz - cz * CHUNK_SIZE) * CHUNK_SIZE + (bx - cx * CHUNK_SIZE)];
    return this.generator.getHeight(bx, bz);
  }

  getWaterLevel(wx: number, wz: number): number {
    const bx = Math.floor(wx), bz = Math.floor(wz);
    const cx = Math.floor(bx / CHUNK_SIZE), cz = Math.floor(bz / CHUNK_SIZE);
    const c = this.getChunk(cx, cz);
    return c ? c.waterLevels[(bz - cz * CHUNK_SIZE) * CHUNK_SIZE + bx - cx * CHUNK_SIZE]
      : this.generator.getWaterLevel(bx, bz);
  }

  update(playerX: number, playerZ: number): void {
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
        if (!this.pool.request(cx, cz)) break;
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
      if (this.inRange(c.cx, c.cz, 1)) continue;
      this.disposeChunk(c);
      this.chunks.delete(key);
    }
  }

  private buildMesh(data: MeshData, material: THREE.Material, cx: number, cz: number): THREE.Mesh {
    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position', new THREE.BufferAttribute(data.positions, 3));
    geo.setAttribute('color', new THREE.BufferAttribute(data.colors, 3, true));
    geo.setIndex(new THREE.BufferAttribute(data.indices, 1));
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
      minimap: new ImageData(msg.minimap, CHUNK_SIZE, CHUNK_SIZE),
      terrain: msg.terrain ? this.buildMesh(msg.terrain, this.terrainMaterial, msg.cx, msg.cz) : null,
      water: msg.water ? this.buildMesh(msg.water, this.waterMaterial, msg.cx, msg.cz) : null,
    };
    if (chunk.terrain) this.group.add(chunk.terrain);
    if (chunk.water) {
      chunk.water.renderOrder = 1;
      this.group.add(chunk.water);
    }
    this.chunks.set(key, chunk);
    for (const fn of this.listeners) fn(chunk);
  }

  private disposeChunk(c: LoadedChunk): void {
    for (const m of [c.terrain, c.water]) {
      if (!m) continue;
      this.group.remove(m);
      m.geometry.dispose();
    }
  }

  dispose(): void {
    for (const c of this.chunks.values()) this.disposeChunk(c);
    this.chunks.clear();
    this.pool.dispose();
    this.terrainMaterial.dispose();
    this.waterMaterial.dispose();
  }
}
