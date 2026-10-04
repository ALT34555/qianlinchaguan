/**
 * 游戏主体：渲染器 / 场景 / 主循环，串起世界、玩家与 UI。
 */
import * as THREE from 'three';
import { CHUNK_SIZE } from './config';
import { Input } from './Input';
import { Player } from '../entities/Player';
import { World } from '../systems/world/World';
import { WorldGenerator } from '../systems/world/WorldGenerator';
import { GENERATOR_VERSION, type ClimateWeights } from '../systems/world/WorldSettings';
import { parseWorldSave, type PlayerPosition } from '../systems/world/WorldSave';
import { DebugOverlay } from '../ui/DebugOverlay';
import { CalendarSystem, CalendarClock } from '../systems/calendar/CalendarSystem';
import type { CalendarSnapshot } from '../systems/calendar/types';


export interface GameOptions {
  seed: number;
  climateWeights: ClimateWeights;
  calendarType: 'real' | 'yuan';
  unixMs?: number;
  initialPlayer?: PlayerPosition;
  renderDistance: number;
  showOverlay: boolean;
}

const SKY_COLOR = new THREE.Color('#a9d3ff');
const UNDERWATER_COLOR = new THREE.Color('#1f4f9a');

export class Game {
  private readonly renderer: THREE.WebGLRenderer;
  private readonly scene = new THREE.Scene();
  private readonly camera: THREE.PerspectiveCamera;
  private readonly fog: THREE.Fog;
  private readonly input: Input;
  readonly calendarSystem: CalendarSystem;
  readonly calendarClock: CalendarClock;
  readonly calendarType: 'real' | 'yuan';
  currentSeason: number = 0;
  currentDayRatio: number = 0;
  currentSnapshot!: CalendarSnapshot;
  private readonly world: World;
  private readonly player: Player;
  private readonly overlay: DebugOverlay;
  private readonly clock = new THREE.Clock();
  private readonly fogNear: number;
  private readonly fogFar: number;
  private spawned = false;

  constructor(
    container: HTMLElement,
    private readonly ui: { start: HTMLElement; loading: HTMLElement; waterTint: HTMLElement },
    options: GameOptions,
  ) {
    // 先验证出生点；失败时尚未创建画布、事件监听器或 Worker。
    const spawn = options.initialPlayer
      ? { cx: Math.floor(options.initialPlayer.x / CHUNK_SIZE), cz: Math.floor(options.initialPlayer.z / CHUNK_SIZE) }
      : new WorldGenerator(options.seed, options.climateWeights).findSpawnChunk();
    // 纯色风格：关闭色彩管理，颜色按原值输出
    THREE.ColorManagement.enabled = false;
    this.renderer = new THREE.WebGLRenderer({ antialias: true, powerPreference: 'high-performance' });
    this.renderer.outputColorSpace = THREE.LinearSRGBColorSpace;
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    this.renderer.setSize(window.innerWidth, window.innerHeight);
    this.renderer.setClearColor(SKY_COLOR);
    container.appendChild(this.renderer.domElement);

    const viewDist = options.renderDistance * CHUNK_SIZE;
    this.fogNear = viewDist * 0.55;
    this.fogFar = viewDist * 0.95;
    this.fog = new THREE.Fog(SKY_COLOR.clone(), this.fogNear, this.fogFar);
    this.scene.fog = this.fog;

    this.camera = new THREE.PerspectiveCamera(75, window.innerWidth / window.innerHeight, 0.1, viewDist * 1.5 + 512);
    this.camera.rotation.order = 'YXZ';

    this.input = new Input(this.renderer.domElement);
    this.calendarType = options.calendarType;
    this.calendarSystem = new CalendarSystem({ mode: this.calendarType });
    this.calendarClock = new CalendarClock({ 
      epochUnixMs: options.unixMs ?? Date.now(), 
      dayLengthSeconds: 1200 // 20 minutes for a full day
    });
    this.updateCalendar();

    this.world = new World(this.scene, options.seed, options.renderDistance, options.climateWeights);
    this.player = new Player(this.world, this.input);
    this.overlay = new DebugOverlay(document.body, this.world, this.player, this);
    if (options.showOverlay) this.overlay.setVisible(true);

    // 出生点：最近的陆地区块中心
    this.player.spawnAt(spawn.cx * CHUNK_SIZE + CHUNK_SIZE / 2 + 0.5, spawn.cz * CHUNK_SIZE + CHUNK_SIZE / 2 + 0.5);

    if (options.initialPlayer) Object.assign(this.player, options.initialPlayer);

    this.input.onPress('F12', () => this.overlay.toggle());
    
    // Ignore clicks on buttons to prevent pointer lock
    this.ui.start.addEventListener('click', (e) => {
      if (!(e.target as HTMLElement).closest('button, input, a')) {
        this.input.requestPointerLock();
      }
    });

    const btnSave = document.getElementById('btn-save-map');
    const btnLoad = document.getElementById('btn-load-map');
    const inputLoad = document.getElementById('input-load-map') as HTMLInputElement;

    if (btnSave) {
      btnSave.addEventListener('click', (e) => {
        e.stopPropagation();
        const data = {
          calendarType: this.calendarType,
          unixMs: this.calendarClock.unixMs,
          generatorVersion: GENERATOR_VERSION,
          climateWeights: this.world.generator.climateWeights,
          seed: this.world.generator.seed,
          player: {
            x: this.player.x,
            y: this.player.y,
            z: this.player.z,
            yaw: this.player.yaw,
            pitch: this.player.pitch
          }
        };
        const blob = new Blob([JSON.stringify(data)], { type: 'application/json' });
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = `qianlin_map_${data.seed}.json`;
        a.click();
        URL.revokeObjectURL(url);
      });
    }

    if (btnLoad && inputLoad) {
      btnLoad.addEventListener('click', (e) => {
        e.stopPropagation();
        inputLoad.click();
      });
      inputLoad.addEventListener('change', (e) => {
        const file = (e.target as HTMLInputElement).files?.[0];
        if (!file) return;
        const reader = new FileReader();
        reader.onload = (re) => {
          try {
            const data = parseWorldSave(re.target?.result as string);
            sessionStorage.setItem('loadWorldSave', JSON.stringify(data));
            const url = new URL(location.href);
            url.searchParams.set('resume', '1');
            location.href = url.toString();
          } catch (err) {
            console.error('Failed to load map', err);
            alert(err instanceof Error ? err.message : '地图加载失败！');
          }
        };
        reader.readAsText(file);
        inputLoad.value = '';
      });
      
    }
    document.getElementById('btn-main-menu')?.addEventListener('click', () => {
      const url = new URL(location.href);
      url.searchParams.delete('resume');
      url.searchParams.set('seed', String(this.world.seed));
      url.searchParams.set('climate', this.world.generator.climateWeights.join(','));
      location.href = url.toString();
    });

    this.renderer.domElement.addEventListener('click', () => this.input.requestPointerLock());
    document.addEventListener('pointerlockchange', () => {
      this.ui.start.classList.toggle('hidden', document.pointerLockElement === this.renderer.domElement);
    });
    // Toggle pointer lock with Ctrl when debug overlay is visible
    document.addEventListener('keydown', (e) => {
      if ((e.code === 'ControlLeft' || e.code === 'ControlRight') && this.overlay.isVisible) {
        if (document.pointerLockElement === this.renderer.domElement) {
          document.exitPointerLock();
        } else {
          this.input.requestPointerLock();
        }
      }
    });

    window.addEventListener('resize', () => this.onResize());
  }

  start(): void {
    this.clock.start();
    this.renderer.setAnimationLoop(() => this.frame());
  }

  private updateCalendar(): void {
    this.currentSnapshot = this.calendarSystem.snapshotFromUnixMs(this.calendarClock.unixMs);
    // calc season
    if (this.currentSnapshot.season) {
      this.currentSeason = this.currentSnapshot.season.index;
    }
    // time of day: 0 to 1
    this.currentDayRatio = this.currentSnapshot.instant.frac;
  }

  private updateLighting(): void {
    // 0 = noon, 0.5 = midnight
    const timeToMidnight = Math.abs(this.currentDayRatio - 0.5);
    const brightness = Math.max(0.1, Math.min(1.0, timeToMidnight * 2 + 0.1));
    
    // adjust fog and sky color based on season and time
    const sky = SKY_COLOR.clone();
    
    // seasonal tint
    if (this.currentSeason === 0) sky.lerp(new THREE.Color('#d4ffd4'), 0.2); // Spring
    if (this.currentSeason === 1) sky.lerp(new THREE.Color('#ffd4d4'), 0.2); // Summer
    if (this.currentSeason === 2) sky.lerp(new THREE.Color('#ffffd4'), 0.2); // Autumn
    if (this.currentSeason === 3) sky.lerp(new THREE.Color('#d4d4ff'), 0.2); // Winter

    sky.multiplyScalar(brightness);
    this.renderer.setClearColor(sky);
    this.scene.fog!.color.copy(sky);

    // Apply brightness to world material by updating material color
    // We'll iterate the chunk meshes if needed, or pass it to World
    this.world.setGlobalBrightness(brightness, this.currentSeason);
  }

  private onResize(): void {
    this.camera.aspect = window.innerWidth / window.innerHeight;
    this.camera.updateProjectionMatrix();
    this.renderer.setSize(window.innerWidth, window.innerHeight);
  }

  private frame(): void {
    const dt = Math.min(this.clock.getDelta(), 0.05);
    this.calendarClock.advance(dt);
    this.updateCalendar();
    this.updateLighting();
    const p = this.player;

    this.world.update(p.x, p.z);

    // 等出生点周围 3x3 区块就绪后再开始物理，避免在空白中下落
    if (!this.spawned) {
      const cx = Math.floor(p.x / CHUNK_SIZE);
      const cz = Math.floor(p.z / CHUNK_SIZE);
      let ready = true;
      for (let dz = -1; dz <= 1 && ready; dz++)
        for (let dx = -1; dx <= 1 && ready; dx++) ready = this.world.isLoaded(cx + dx, cz + dz);
      if (ready) {
        this.spawned = true;
        this.ui.loading.classList.add('hidden');
      }
    } else if (document.pointerLockElement === this.renderer.domElement) {
      p.update(dt);
    }

    this.camera.position.set(p.x, p.eyeY, p.z);
    this.camera.rotation.set(p.pitch, p.yaw, 0);

    // 水下效果
    const eyeUnderwater = p.eyeY < this.world.getWaterLevel(p.x, p.z) - 0.12 && this.world.getHeight(p.x, p.z) < p.eyeY;
    if (eyeUnderwater) {
      this.fog.color.copy(UNDERWATER_COLOR);
      this.fog.near = 1;
      this.fog.far = 48;
      this.renderer.setClearColor(UNDERWATER_COLOR);
    } else {
      this.fog.color.copy(SKY_COLOR);
      this.fog.near = this.fogNear;
      this.fog.far = this.fogFar;
      this.renderer.setClearColor(SKY_COLOR);
    }
    this.ui.waterTint.classList.toggle('hidden', !eyeUnderwater);

    this.overlay.update(performance.now());
    this.renderer.render(this.scene, this.camera);
  }
}
