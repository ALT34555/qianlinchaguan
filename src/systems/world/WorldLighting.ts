import * as THREE from 'three';

export const CAMERA_LIGHT_RADIUS = 8;
const smooth = (a: number, b: number, x: number) => THREE.MathUtils.smoothstep(x, a, b);

/** 民用时间从午夜开始，与 Sky 的日月轨迹保持一致。 */
export function daylightAt(dayRatio: number): { sunHeight: number; daylight: number; dusk: number; night: number } {
  const sunHeight = -Math.cos(dayRatio * Math.PI * 2);
  const daylight = smooth(-.12, .3, sunHeight);
  return { sunHeight, daylight, dusk: Math.max(0, 1 - Math.abs(sunHeight) / .35),
    night: 1 - smooth(-.18, .08, sunHeight) };
}

const SEASON_TINTS = [0xe6ffe6, 0xffeee6, 0xfff2d6, 0xe6eeff].map(c => new THREE.Color(c));
const SUN_WARM = new THREE.Color(0xffb477);
const HEMISPHERE_DAY = new THREE.Color(0xdbeaff);
const GROUND_DAY = new THREE.Color(0x62594b);
const FOG_DAY = new THREE.Color(0xa9d3ff);
const FOG_DUSK = new THREE.Color(0xc88f82);
const ZENITH_DAY = new THREE.Color(0x3d78d2);

/** 全局太阳/月光与相机弱补光；不使用阴影贴图，避免区块加载时阴影跳动。 */
export class WorldLighting {
  readonly fogColor = new THREE.Color();
  readonly zenithColor = new THREE.Color();
  readonly sunDirection = new THREE.Vector3();
  readonly moonDirection = new THREE.Vector3();
  readonly tint = new THREE.Color();
  readonly hemisphere = new THREE.HemisphereLight(0xdbeaff, 0x62594b, .7);
  readonly sun = new THREE.DirectionalLight(0xfff2db, 1);
  readonly moon = new THREE.DirectionalLight(0xb8cdf7, .25);
  readonly cameraLight = new THREE.PointLight(0xc8d7ed, 0, CAMERA_LIGHT_RADIUS, 1);
  private readonly group = new THREE.Group();

  constructor(scene: THREE.Scene) {
    this.group.add(this.hemisphere, this.sun, this.sun.target, this.moon, this.moon.target, this.cameraLight);
    scene.add(this.group);
  }

  update(dayRatio: number, season: number, camera: THREE.Vector3): ReturnType<typeof daylightAt> {
    const state = daylightAt(dayRatio);
    const { daylight, dusk, night, sunHeight } = state;
    const angle = (dayRatio + .5) * Math.PI * 2;
    this.sunDirection.set(Math.sin(angle), sunHeight, 0).normalize();
    this.moonDirection.copy(this.sunDirection).negate();
    this.sun.position.copy(camera).addScaledVector(this.sunDirection, 200);
    this.moon.position.copy(camera).addScaledVector(this.moonDirection, 200);
    this.sun.target.position.copy(camera); this.moon.target.position.copy(camera);
    this.sun.intensity = 1.7 * smooth(-.06, .32, sunHeight);
    this.sun.color.set(0xfff2db).lerp(SUN_WARM, dusk * .7);
    this.moon.intensity = .5 * night * smooth(0, .4, -sunHeight);
    this.hemisphere.intensity = .4 + daylight * .64;
    this.hemisphere.color.set(0x8fa5cd).lerp(HEMISPHERE_DAY, daylight);
    this.hemisphere.groundColor.set(0x3c4562).lerp(GROUND_DAY, daylight);
    this.cameraLight.position.copy(camera);
    this.cameraLight.intensity = 1.4 * night;
    this.tint.set(0xffffff).lerp(SEASON_TINTS[season] ?? SEASON_TINTS[0], .12);
    this.fogColor.set(0x111c34).lerp(FOG_DAY, daylight);
    this.fogColor.lerp(FOG_DUSK, dusk * .38);
    this.zenithColor.set(0x0d1730).lerp(ZENITH_DAY, daylight);
    return state;
  }

  dispose(): void { this.group.removeFromParent(); }
}
