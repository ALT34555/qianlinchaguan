import * as THREE from 'three';
import { MIST_EMITTER_STRIDE } from './WaterfallSpray';

const DROPS_PER_EMITTER = 16;
const MIST_PER_EMITTER = 8;

const mistVertexShader = `
uniform float uMistTime;
uniform float uMistDaylight;
uniform float uMistProj;
attribute vec4 aDrift;
attribute vec3 aZone;
attribute float aSeed;
varying float vFade;
varying float vKind;
varying vec4 vZone;
#include <fog_pars_vertex>
void main() {
  vKind = aZone.z;
  float life = vKind < 0.5 ? mix(1.4, 2.8, fract(aSeed * 7.13)) : mix(0.28, 0.58, fract(aSeed * 7.13));
  float age = fract(uMistTime / life + aSeed);
  vec3 p = position;
  float size;
  if (vKind < 0.5) {
    float ease = 1.0 - (1.0 - age) * (1.0 - age);
    p += vec3(aDrift.x * ease, aDrift.y * age, aDrift.z * ease);
    size = aDrift.w * (0.7 + age * 0.6);
  } else {
    p += vec3(aDrift.x * age, aDrift.y * 4.0 * age * (1.0 - age) - 0.2 * age * age, aDrift.z * age);
    size = aDrift.w;
  }
  vFade = smoothstep(0.0, 0.12, age) * (1.0 - smoothstep(0.65, 1.0, age)) * (0.3 + 0.7 * uMistDaylight);
  vec4 mvPosition = modelViewMatrix * vec4(p, 1.0);
  float projection = uMistProj / max(1.0, -mvPosition.z);
  gl_PointSize = clamp(size * projection, 1.0, vKind < 0.5 ? 180.0 : 7.0);
  vZone = vec4(aZone.xy, (modelMatrix * vec4(p, 1.0)).y, gl_PointSize / projection * viewMatrix[1][1]);
  gl_Position = projectionMatrix * mvPosition;
  #include <fog_vertex>
}
`;

const mistFragmentShader = `
varying float vFade;
varying float vKind;
varying vec4 vZone;
#include <fog_pars_fragment>
void main() {
  float d = length(gl_PointCoord - 0.5);
  float alpha;
  if (vKind < 0.5) {
    float worldY = vZone.z + (0.5 - gl_PointCoord.y) * vZone.w;
    if (worldY < vZone.x || worldY > vZone.y) discard;
    float edge = max(0.001, (vZone.y - vZone.x) * 0.12);
    float zoneFade = smoothstep(vZone.x, vZone.x + edge, worldY) * (1.0 - smoothstep(vZone.y - edge, vZone.y, worldY));
    alpha = (1.0 - smoothstep(0.08, 0.5, d)) * vFade * zoneFade * 0.25;
  } else {
    alpha = (1.0 - smoothstep(0.25, 0.5, d)) * vFade * 0.8;
  }
  if (alpha < 0.004) discard;
  gl_FragColor = vec4(mix(vec3(0.82, 0.9, 0.95), vec3(0.9, 0.97, 1.0), vKind), alpha);
  #include <fog_fragment>
}
`;

function mistRandom(n: number): number {
  const s = Math.sin(n) * 43758.5453;
  return s - Math.floor(s);
}

export class WaterfallMist {
  private readonly material: THREE.ShaderMaterial;

  constructor() {
    const uniforms = THREE.UniformsUtils.merge([THREE.UniformsLib.fog]) as Record<string, THREE.IUniform>;
    uniforms.uMistTime = { value: 0 };
    uniforms.uMistDaylight = { value: 1 };
    uniforms.uMistProj = { value: 420 };
    this.material = new THREE.ShaderMaterial({uniforms, vertexShader: mistVertexShader, fragmentShader: mistFragmentShader,
      transparent: true, depthWrite: false, fog: true});
  }

  build(emitters: Float32Array, cx: number, cz: number, chunkSize: number): THREE.Points | null {
    const count = Math.floor(emitters.length / MIST_EMITTER_STRIDE);
    if (!count) return null;
    const positions: number[] = [], drifts: number[] = [], zones: number[] = [], seeds: number[] = [];
    const bounds = new THREE.Box3();
    const append = (x: number, y: number, z: number, dx: number, rise: number, dz: number, size: number, base: number, ceiling: number, kind: number, seed: number) => {
      positions.push(x, y, z); drifts.push(dx, rise, dz, size); zones.push(base, ceiling, kind); seeds.push(seed);
      const padding = size + .3;
      bounds.expandByPoint(new THREE.Vector3(x + Math.min(0, dx) - padding, y - padding, z + Math.min(0, dz) - padding));
      bounds.expandByPoint(new THREE.Vector3(x + Math.max(0, dx) + padding, y + rise + padding, z + Math.max(0, dz) + padding));
    };
    for (let e = 0; e < count; e++) {
      const o = e * MIST_EMITTER_STRIDE;
      const x = emitters[o], y = emitters[o + 1], z = emitters[o + 2], scale = emitters[o + 3];
      const dx = emitters[o + 4], dz = emitters[o + 5], localTop = emitters[o + 6];
      const base = emitters[o + 7], ceiling = base + (emitters[o + 8] - base) * emitters[o + 9];
      const globalSeed = (x + cx * chunkSize) * 12.99 + (z + cz * chunkSize) * 78.23;
      for (let k = 0; k < DROPS_PER_EMITTER; k++) {
        const seed = mistRandom(globalSeed + k * 3.71), side = (mistRandom(seed * 41.3) - .5) * .8;
        const drift = .25 + mistRandom(seed * 91.7) * .55;
        const rise = .08 + mistRandom(seed * 17.9) * .18;
        append(x - dz * side, y + (localTop - y) * seed + .04, z + dx * side,
          dx * drift - dz * side * .35, rise, dz * drift + dx * side * .35,
          .045 + mistRandom(seed * 29.7) * .065, 0, 0, 1, seed);
      }
      if (emitters[o + 9] <= 0 || y > ceiling || localTop < base) continue;
      const zoneHeight = ceiling - base;
      for (let k = 0; k < MIST_PER_EMITTER; k++) {
        const seed = mistRandom(globalSeed + 101 + k * 3.71), angle = seed * Math.PI * 2;
        const spread = (.5 + mistRandom(seed * 41.3)) * scale;
        const size = Math.min(scale * (.5 + seed * .5), zoneHeight * .45);
        const start = Math.min(ceiling - size * .5, Math.max(base + size * .5, y));
        const rise = Math.max(0, Math.min(zoneHeight * .35, ceiling - start - size * .65));
        append(x, start, z, Math.cos(angle) * spread + dx * spread * .65, rise,
          Math.sin(angle) * spread + dz * spread * .65, size, base, ceiling, 0, seed);
      }
    }
    const geometry = new THREE.BufferGeometry();
    geometry.setAttribute('position', new THREE.Float32BufferAttribute(positions, 3));
    geometry.setAttribute('aDrift', new THREE.Float32BufferAttribute(drifts, 4));
    geometry.setAttribute('aZone', new THREE.Float32BufferAttribute(zones, 3));
    geometry.setAttribute('aSeed', new THREE.Float32BufferAttribute(seeds, 1));
    geometry.boundingSphere = bounds.getBoundingSphere(new THREE.Sphere());
    const points = new THREE.Points(geometry, this.material);
    points.position.set(cx * chunkSize, 0, cz * chunkSize);
    points.matrixAutoUpdate = false;
    points.updateMatrix();
    points.renderOrder = 2;
    return points;
  }

  update(time: number, daylight: number): void {
    this.material.uniforms.uMistTime.value = time;
    this.material.uniforms.uMistDaylight.value = daylight;
  }

  setProjection(fov: number, viewportHeight: number, pixelRatio: number): void {
    this.material.uniforms.uMistProj.value = viewportHeight * Math.min(pixelRatio, 2) / (2 * Math.tan(THREE.MathUtils.degToRad(fov) / 2));
  }

  disposePoints(points: THREE.Points): void {
    points.removeFromParent();
    points.geometry.dispose();
  }

  dispose(): void {
    this.material.dispose();
  }
}
