import * as THREE from 'three';
import { WEATHER, type WeatherId } from './Weather';

const COUNT = 1200;
const HALF = 28;
export class WeatherVisual {
  private readonly drops = new Float32Array(COUNT * 6);
  private readonly flakes = new Float32Array(COUNT * 3);
  private readonly rain: THREE.LineSegments;
  private readonly snow: THREE.Points;
  private readonly bolt: THREE.Line;
  private readonly local = new Float32Array(COUNT * 6);
  private time = 0;
  private id: WeatherId = 0;
  private nextFlash = 5;
  private flashUntil = 0;
  flash = 0;

  constructor(scene: THREE.Scene) {
    const rainGeometry = new THREE.BufferGeometry();
    rainGeometry.setAttribute('position', new THREE.BufferAttribute(this.drops, 3).setUsage(THREE.DynamicDrawUsage));
    this.rain = new THREE.LineSegments(rainGeometry, new THREE.LineBasicMaterial({ color: '#b4cee6', transparent: true, opacity: .55, depthWrite: false }));
    const snowGeometry = new THREE.BufferGeometry();
    snowGeometry.setAttribute('position', new THREE.BufferAttribute(this.flakes, 3).setUsage(THREE.DynamicDrawUsage));
    this.snow = new THREE.Points(snowGeometry, new THREE.PointsMaterial({ color: '#ffffff', size: .12, transparent: true, opacity: .8, depthWrite: false }));
    (this.snow.material as THREE.PointsMaterial).onBeforeCompile = shader => {
      shader.fragmentShader = shader.fragmentShader.replace('#include <map_particle_fragment>',
        '#include <map_particle_fragment>\n float radius = length(gl_PointCoord - .5);\n if(radius > .5) discard;\n diffuseColor.a *= 1.0 - smoothstep(.15, .5, radius);');
    };
    this.bolt = new THREE.Line(new THREE.BufferGeometry(), new THREE.LineBasicMaterial({ color: '#eef3ff', transparent: true, depthWrite: false, fog: false }));
    this.rain.frustumCulled = this.snow.frustumCulled = this.bolt.frustumCulled = false;
    this.rain.visible = this.snow.visible = this.bolt.visible = false;
    scene.add(this.rain, this.snow, this.bolt);
    for (let i = 0; i < COUNT * 2; i++) this.spawn(i);
  }
  private spawn(i: number): void {
    this.local[i * 3] = (Math.random() * 2 - 1) * HALF;
    this.local[i * 3 + 1] = Math.random() * 36 - 8;
    this.local[i * 3 + 2] = (Math.random() * 2 - 1) * HALF;
  }
  update(id: WeatherId, dt: number, camera: THREE.Vector3, underwater: boolean, enabled: boolean,
    surface: (x: number, z: number) => number, weatherAt?: (x: number, z: number) => WeatherId, advanced = true): void {
    if (id !== this.id) {
      this.id = id; this.flashUntil = 0; this.nextFlash = this.time + 3;
      for (let i = 0; i < COUNT * 2; i++) this.spawn(i);
    }
    this.time += dt;
    const effect = WEATHER[id];
    if (effect.lightning && this.time >= this.nextFlash) {
      this.flashUntil = this.time + .24; this.nextFlash = this.time + 5 + Math.random() * 9;
      const points = [];
      const x = camera.x + 50, z = camera.z - 90;
      for (let n = 0; n < 8; n++) points.push(new THREE.Vector3(x + Math.random() * 12, camera.y + 110 - n * 14, z + Math.random() * 8));
      this.bolt.geometry.setFromPoints(points);
    }
    this.flash = !underwater && this.time < this.flashUntil ? .8 : 0;
    this.bolt.visible = enabled && advanced && this.flash > 0;
    const limit = advanced ? COUNT : 240;
    const rainCount = weatherAt ? limit : Math.floor(limit * effect.rain);
    const snowCount = weatherAt ? limit : Math.floor(limit * Math.max(effect.snow, effect.hail, effect.dust));
    this.rain.visible = enabled && !underwater && rainCount > 0;
    this.snow.visible = enabled && !underwater && snowCount > 0;
    this.rain.geometry.setDrawRange(0, rainCount * 2);
    this.snow.geometry.setDrawRange(0, snowCount);
    const material = this.snow.material as THREE.PointsMaterial;
    material.color.set(effect.dust ? (id === 304 ? '#cdb17a' : '#c0c2b3') : '#ffffff');
    material.size = effect.hail ? .2 : effect.dust ? .07 : .12;
    if (!enabled || underwater || (!rainCount && !snowCount)) return;
    for (let i = 0; i < rainCount + snowCount; i++) {
      const p = i * 3, d = i * 6;
      const rain = i < rainCount;
      const localEffect = weatherAt ? WEATHER[weatherAt(camera.x + this.local[p], camera.z + this.local[p + 2])] : effect;
      this.local[p] += dt * localEffect.wind * (localEffect.vortex ? Math.cos(this.time + i) * 3 : 1);
      this.local[p + 2] += dt * localEffect.wind * (localEffect.vortex ? Math.sin(this.time + i) * 3 : .34);
      this.local[p + 1] -= dt * (rain || localEffect.hail ? 26 : localEffect.dust ? .8 : 2.5);
      if (Math.abs(this.local[p]) > HALF || Math.abs(this.local[p + 2]) > HALF || this.local[p + 1] < -8) {
        this.spawn(i); this.local[p + 1] = 28;
      }
      const x = camera.x + this.local[p], y = camera.y + this.local[p + 1], z = camera.z + this.local[p + 2];
      const density = rain ? localEffect.rain : Math.max(localEffect.snow, localEffect.hail, localEffect.dust);
      const emitting = !weatherAt || (i % limit) / limit < density;
      if (!emitting) {
        if (rain) this.drops.set([x, camera.y - 10000, z, x, camera.y - 10000, z], d);
        else this.flakes.set([x, camera.y - 10000, z], (i - rainCount) * 3);
        continue;
      }
      const floor = surface(x, z);
      const visibleY = emitting && y > floor + .1 ? y : floor - 2;
      if (rain) this.drops.set([x, visibleY, z, x - localEffect.wind * .035, visibleY + (emitting && y > floor + .1 ? 1.2 : 0), z - localEffect.wind * .012], d);
      else this.flakes.set([x, visibleY, z], (i - rainCount) * 3);
    }
    this.rain.geometry.attributes.position.needsUpdate = true;
    this.snow.geometry.attributes.position.needsUpdate = true;
  }
  dispose(): void {
    for (const object of [this.rain, this.snow, this.bolt]) {
      object.removeFromParent(); object.geometry.dispose(); (object.material as THREE.Material).dispose();
    }
  }
}
