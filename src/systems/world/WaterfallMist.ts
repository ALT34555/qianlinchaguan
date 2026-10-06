import * as THREE from 'three';

const PARTICLES_PER_EMITTER = 16;

const mistVertexShader = `
uniform float uMistTime;
uniform float uMistDaylight;
uniform float uMistProj;
attribute vec4 aDrift;
attribute float aSeed;
varying float vFade;
#include <fog_pars_vertex>
void main() {
  float life = mix(1.4, 2.8, fract(aSeed * 7.13));
  float age = fract(uMistTime / life + aSeed);
  float ease = 1.0 - (1.0 - age) * (1.0 - age);
  vec3 p = position + vec3(aDrift.x, 0.0, aDrift.z) * ease;
  p.y += aDrift.y * age * (1.6 - age * 0.6);
  p.x += sin(uMistTime * 0.9 + aSeed * 17.0) * 0.18 * age;
  p.z += cos(uMistTime * 0.7 + aSeed * 23.0) * 0.18 * age;
  vFade = smoothstep(0.0, 0.18, age) * (1.0 - smoothstep(0.5, 1.0, age)) * (0.3 + 0.7 * uMistDaylight);
  vec4 mvPosition = modelViewMatrix * vec4(p, 1.0);
  float size = aDrift.w * (0.5 + age * 1.5) * 1.4;
  gl_PointSize = clamp(size * uMistProj / max(1.0, -mvPosition.z), 1.0, 220.0);
  gl_Position = projectionMatrix * mvPosition;
  #include <fog_vertex>
}
`;

const mistFragmentShader = `
varying float vFade;
#include <fog_pars_fragment>
void main() {
  float d = length(gl_PointCoord - 0.5);
  float alpha = smoothstep(0.5, 0.08, d) * vFade * 0.34;
  if (alpha < 0.004) discard;
  gl_FragColor = vec4(0.82, 0.9, 0.95, alpha);
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
    this.material = new THREE.ShaderMaterial({
      uniforms,
      vertexShader: mistVertexShader,
      fragmentShader: mistFragmentShader,
      transparent: true,
      depthWrite: false,
      fog: true,
    });
  }

  build(emitters: Float32Array, cx: number, cz: number, chunkSize: number): THREE.Points | null {
    const count = Math.floor(emitters.length / 6);
    if (!count) return null;
    const total = count * PARTICLES_PER_EMITTER;
    const positions = new Float32Array(total * 3);
    const drifts = new Float32Array(total * 4);
    const seeds = new Float32Array(total);
    const center = new THREE.Vector3();
    for (let e = 0; e < count; e++) center.add(new THREE.Vector3(emitters[e * 6], emitters[e * 6 + 1], emitters[e * 6 + 2]));
    center.multiplyScalar(1 / count);
    let radius = 1;
    let cursor = 0;
    for (let e = 0; e < count; e++) {
      const x = emitters[e * 6], y = emitters[e * 6 + 1], z = emitters[e * 6 + 2], scale = emitters[e * 6 + 3];
      const dx = emitters[e * 6 + 4], dz = emitters[e * 6 + 5];
      radius = Math.max(radius, Math.hypot(x - center.x, y - center.y, z - center.z) + scale * 4);
      for (let k = 0; k < PARTICLES_PER_EMITTER; k++, cursor++) {
        const seed = mistRandom(x * 12.99 + z * 78.23 + k * 3.71);
        const angle = mistRandom(seed * 91.7 + k) * Math.PI * 2;
        const spread = (0.8 + mistRandom(seed * 41.3) * 1.3) * scale;
        const out = spread * 0.7;
        positions[cursor * 3] = x; positions[cursor * 3 + 1] = y; positions[cursor * 3 + 2] = z;
        drifts[cursor * 4] = Math.cos(angle) * out + dx * spread * 0.9;
        drifts[cursor * 4 + 1] = (1.1 + mistRandom(seed * 17.9) * 1.7) * scale;
        drifts[cursor * 4 + 2] = Math.sin(angle) * out + dz * spread * 0.9;
        drifts[cursor * 4 + 3] = scale * (0.7 + mistRandom(seed * 29.7) * 0.6);
        seeds[cursor] = seed;
      }
    }
    const geometry = new THREE.BufferGeometry();
    geometry.setAttribute('position', new THREE.BufferAttribute(positions, 3));
    geometry.setAttribute('aDrift', new THREE.BufferAttribute(drifts, 4));
    geometry.setAttribute('aSeed', new THREE.BufferAttribute(seeds, 1));
    geometry.boundingSphere = new THREE.Sphere(center, radius);
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
