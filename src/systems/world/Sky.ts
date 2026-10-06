import * as THREE from 'three';

/** 天空与云层 */
export class Sky {
  private readonly group = new THREE.Group();
  private readonly scene: THREE.Scene;
  private readonly sun: THREE.Mesh;
  private readonly moon: THREE.Mesh;
  private readonly dome: THREE.Mesh;
  private readonly stars: THREE.Points;
  private readonly clouds: THREE.Mesh;
  private readonly cloudDrift: THREE.InstancedBufferAttribute;
  /** 每个云片实例的漂移速度（x, z，单位/秒） */
  private readonly cloudVelocity: Float32Array;
  private readonly cloudInstances: number;
  private readonly textures: THREE.Texture[] = [];
  private readonly tint = new THREE.Color();
  /** 水面上的雾距 */
  private surfaceFogFar = 365;
  private underwater = false;
  private underwaterMix = 0;
  private lastNow = -1;

  constructor(scene: THREE.Scene) {
    this.scene = scene;
    const fog = scene.fog;
    if (fog instanceof THREE.Fog) this.surfaceFogFar = fog.far;

    // 天穹 (Sky Dome)
    const domeMaterial = new THREE.ShaderMaterial({
      side: THREE.BackSide,
      depthTest: false,
      depthWrite: false,
      fog: false,
      uniforms: {
        uHorizon: { value: HORIZON_FALLBACK.clone() },
        uZenith: { value: ZENITH_DAY.clone() },
        uFogColor: { value: HORIZON_FALLBACK.clone() },
        uGlowColor: { value: GLOW_FALLBACK.clone() },
        uSunDir: { value: new THREE.Vector3(0, 1, 0) },
        uGlow: { value: 0.2 },
        uUnderwater: { value: 0 },
      },
      vertexShader: `
        varying vec3 vDir;
        void main() {
          vDir = normalize(position);
          gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
        }
      `,
      fragmentShader: `
        uniform vec3 uHorizon;
        uniform vec3 uZenith;
        uniform vec3 uFogColor;
        uniform vec3 uGlowColor;
        uniform vec3 uSunDir;
        uniform float uGlow;
        uniform float uUnderwater;
        varying vec3 vDir;
        void main() {
          vec3 dir = normalize(vDir);
          // 指数 > 1：靠近地平线的一段尽量贴近雾色，与远处地形的雾面接得更自然
          vec3 color = mix(uHorizon, uZenith, pow(clamp(dir.y, 0.0, 1.0), 1.35));
          // 地平线以下略微压暗，靠近地面时不至于发白
          color = mix(color, uHorizon * 0.92, smoothstep(0.0, -0.14, dir.y));
          float sun = max(dot(dir, uSunDir), 0.0);
          color += uGlowColor * uGlow * pow(sun, 4.0);
          gl_FragColor = vec4(mix(color, uFogColor, uUnderwater), 1.0);
          #include <tonemapping_fragment>
          #include <colorspace_fragment>
        }
      `,
    });
    // 放在不透明队列最前面
    this.dome = new THREE.Mesh(new THREE.SphereGeometry(DOME_RADIUS, 32, 20), domeMaterial);
    this.dome.renderOrder = -1000;
    this.dome.frustumCulled = false;
    this.group.add(this.dome);

    // 星空 (Stars)：只铺上半球，随天球一起转
    const starTexture = makeRadialTexture(64, [
      [0, 'rgba(255,255,255,1)'],
      [0.32, 'rgba(255,255,255,0.5)'],
      [1, 'rgba(255,255,255,0)'],
    ]);
    this.textures.push(starTexture);
    const starCount = 900;
    const positions = new Float32Array(starCount * 3);
    const colors = new Float32Array(starCount * 3);
    for (let i = 0; i < starCount; i++) {
      // 整球分布
      const u = Math.random() * 2 - 1;
      const y = Math.sign(u) * Math.pow(Math.abs(u), 0.72);
      const phi = Math.random() * Math.PI * 2;
      const ring = Math.sqrt(Math.max(0, 1 - y * y));
      positions[i * 3] = Math.cos(phi) * ring * STAR_RADIUS;
      positions[i * 3 + 1] = y * STAR_RADIUS;
      positions[i * 3 + 2] = Math.sin(phi) * ring * STAR_RADIUS;
      const brightness = 0.45 + Math.random() * 0.55;
      colors[i * 3] = brightness * (0.92 + Math.random() * 0.08);
      colors[i * 3 + 1] = brightness * (0.94 + Math.random() * 0.06);
      colors[i * 3 + 2] = brightness;
    }
    const starGeometry = new THREE.BufferGeometry();
    starGeometry.setAttribute('position', new THREE.BufferAttribute(positions, 3));
    starGeometry.setAttribute('color', new THREE.BufferAttribute(colors, 3));
    const starMaterial = new THREE.PointsMaterial({
      size: 2.4 * Math.min(window.devicePixelRatio || 1, 2),
      sizeAttenuation: false,
      map: starTexture,
      vertexColors: true,
      transparent: true,
      depthWrite: false,
      blending: THREE.AdditiveBlending,
      fog: false,
      opacity: 0,
    });
    this.stars = new THREE.Points(starGeometry, starMaterial);
    this.stars.renderOrder = -900;
    this.stars.frustumCulled = false;
    this.group.add(this.stars);

    // 太阳与月亮 (Sun / Moon)
    const sunTexture = makeRadialTexture(256, [
      [0, 'rgba(255,255,250,1)'],
      [0.16, 'rgba(255,250,226,1)'],
      [0.23, 'rgba(255,238,190,0.9)'],
      [0.44, 'rgba(255,206,140,0.26)'],
      [1, 'rgba(255,190,120,0)'],
    ]);
    const moonTexture = makeRadialTexture(256, [
      [0, 'rgba(244,248,255,1)'],
      [0.2, 'rgba(232,240,255,1)'],
      [0.24, 'rgba(210,222,255,0.5)'],
      [0.46, 'rgba(186,204,255,0.16)'],
      [1, 'rgba(180,200,255,0)'],
    ], 6, 0.22);
    this.textures.push(sunTexture, moonTexture);
    this.sun = new THREE.Mesh(new THREE.CircleGeometry(34, 48), new THREE.MeshBasicMaterial({
      map: sunTexture, color: 0xffffff, transparent: true, depthWrite: false, fog: false, side: THREE.DoubleSide,
    }));
    this.sun.renderOrder = -880;
    this.group.add(this.sun);
    this.moon = new THREE.Mesh(new THREE.CircleGeometry(26, 48), new THREE.MeshBasicMaterial({
      map: moonTexture, color: 0xffffff, transparent: true, depthWrite: false, fog: false, side: THREE.DoubleSide,
    }));
    this.moon.renderOrder = -880;
    this.group.add(this.moon);

    // 云层 (Clouds)
    const cloudTexture = makeCloudTexture();
    this.textures.push(cloudTexture);
    const half = this.cloudHalf();
    const clouds = buildCloudField(half);
    const geometry = new THREE.InstancedBufferGeometry();
    const quad = new THREE.PlaneGeometry(1, 1);
    geometry.index = quad.index;
    geometry.setAttribute('position', quad.getAttribute('position'));
    geometry.setAttribute('uv', quad.getAttribute('uv'));
    quad.dispose();
    geometry.instanceCount = clouds.base.length / 3;
    geometry.setAttribute('aBase', new THREE.InstancedBufferAttribute(clouds.base, 3));
    geometry.setAttribute('aSize', new THREE.InstancedBufferAttribute(clouds.size, 2));
    geometry.setAttribute('aRot', new THREE.InstancedBufferAttribute(clouds.rot, 1));
    geometry.setAttribute('aPhase', new THREE.InstancedBufferAttribute(clouds.phase, 4));
    geometry.setAttribute('aOpacity', new THREE.InstancedBufferAttribute(clouds.opacity, 1));
    geometry.setAttribute('aTint', new THREE.InstancedBufferAttribute(clouds.tint, 1));
    this.cloudDrift = new THREE.InstancedBufferAttribute(new Float32Array(clouds.base.length / 3 * 2), 2);
    this.cloudDrift.setUsage(THREE.DynamicDrawUsage);
    geometry.setAttribute('aDrift', this.cloudDrift);
    this.cloudVelocity = clouds.velocity;
    this.cloudInstances = clouds.base.length / 3;
    const cloudMaterial = new THREE.ShaderMaterial({
      transparent: true,
      depthWrite: false,
      side: THREE.DoubleSide,
      fog: false,
      uniforms: {
        uMap: { value: cloudTexture },
        uPhase: { value: 0 },
        uHalf: { value: half },
        uFadeNear: { value: half * CLOUD_FADE_NEAR_RATIO },
        uFadeFar: { value: half },
        uColor: { value: CLOUD_DAY.clone() },
        uHorizonColor: { value: HORIZON_FALLBACK.clone() },
        uAlphaScale: { value: 1 },
      },
      vertexShader: `
        uniform float uPhase;
        uniform float uHalf;
        uniform float uFadeNear;
        uniform float uFadeFar;
        attribute vec3 aBase;
        attribute vec2 aSize;
        attribute float aRot;
        attribute vec4 aPhase;
        attribute vec2 aDrift;
        attribute float aOpacity;
        attribute float aTint;
        varying vec2 vUv;
        varying float vFade;
        varying float vOpacity;
        varying float vTint;
        varying float vBreathe;
        varying float vSeed;
        const float TAU = 6.28318531;
        void main() {
          // 漂移量在 CPU 端用双精度取模后传入，这里只加环绕与低频涡动
          vec2 xz = aBase.xz + aDrift;
          float amp = 6.0 + 9.0 * aPhase.z;
          xz += vec2(
            sin(TAU * (uPhase * 3.0 + aPhase.x)),
            cos(TAU * (uPhase * 5.0 + aPhase.y))
          ) * amp;
          xz = mod(xz + uHalf, uHalf * 2.0) - uHalf;
          float fade = 1.0 - smoothstep(uFadeNear, uFadeFar, length(xz));

          float y = aBase.y + sin(TAU * (uPhase * 2.0 + aPhase.w)) * 3.5;
          float breathe = 0.93 + 0.07 * sin(TAU * (uPhase * 4.0 + aPhase.w * 1.7));
          float rot = aRot + 0.16 * sin(TAU * (uPhase + aPhase.w * 2.3));

          // 公告板：用相机的右、上向量展开云片
          vec3 right = vec3(viewMatrix[0][0], viewMatrix[1][0], viewMatrix[2][0]);
          vec3 up = vec3(viewMatrix[0][1], viewMatrix[1][1], viewMatrix[2][1]);
          float c = cos(rot);
          float s = sin(rot);
          vec2 local = vec2(position.x * c - position.y * s, position.x * s + position.y * c) * (aSize * (2.0 * breathe));

          vec4 world = modelMatrix * vec4(xz.x, y, xz.y, 1.0);
          world.xyz += right * local.x + up * local.y;
          gl_Position = projectionMatrix * viewMatrix * world;

          vUv = uv;
          vFade = fade;
          vOpacity = aOpacity;
          vTint = aTint;
          vBreathe = breathe;
          vSeed = aPhase.w;
        }
      `,
      fragmentShader: `
        uniform sampler2D uMap;
        uniform vec3 uColor;
        uniform vec3 uHorizonColor;
        uniform float uAlphaScale;
        varying vec2 vUv;
        varying float vFade;
        varying float vOpacity;
        varying float vTint;
        varying float vBreathe;
        varying float vSeed;
        void main() {
          // 轻微域扭曲：同一张贴图在每片云上长出各自不同的柔边
          vec2 p = vUv - 0.5;
          p += 0.055 * vec2(sin(p.y * 7.0 + vSeed * 31.4), cos(p.x * 6.0 + vSeed * 47.1));
          float alpha = texture2D(uMap, p + 0.5).a * vOpacity * vBreathe * vFade * uAlphaScale;
          if (alpha < 0.004) discard;

          // 顶部受光、底部略暗，云才有体积感
          float shade = mix(0.80, 1.14, smoothstep(0.04, 0.96, vUv.y));
          vec3 color = uColor * vTint * shade;
          // 远处云片整体溶进天光，避免出现切割感
          float dissolve = (1.0 - vFade) * (1.0 - vFade) * 0.6;
          color = mix(color, uHorizonColor, dissolve);
          gl_FragColor = vec4(color, alpha);
          #include <tonemapping_fragment>
          #include <colorspace_fragment>
        }
      `,
    });
    this.clouds = new THREE.Mesh(geometry, cloudMaterial);
    this.clouds.renderOrder = -860;
    this.clouds.frustumCulled = false;
    this.group.add(this.clouds);

    scene.add(this.group);
  }

  /** 云层淡出半径 */
  private cloudHalf(): number {
    return THREE.MathUtils.clamp(this.surfaceFogFar * 1.9, CLOUD_FADE_MIN, CLOUD_FADE_MAX);
  }

  /** 水下状态由 Game 同步 */
  setUnderwater(underwater: boolean): void {
    this.underwater = underwater;
  }

  update(dayRatio: number, cameraPos: THREE.Vector3, _timeMs: number): void {
    const now = performance.now() / 1000;
    const delta = this.lastNow < 0 ? 0 : Math.min(Math.max(now - this.lastNow, 0), 0.1);
    this.lastNow = now;
    this.underwaterMix += ((this.underwater ? 1 : 0) - this.underwaterMix) * Math.min(1, delta * 2.4);
    if (this.underwaterMix < 0.001) this.underwaterMix = 0;

    // 跟随相机（云、日月都在这个坐标系里）
    this.group.position.set(cameraPos.x, 0, cameraPos.z);
    this.dome.position.y = cameraPos.y;
    this.stars.position.y = cameraPos.y;
    this.clouds.position.y = cameraPos.y;

    // 昼夜：dayRatio 0 为正午、0.5 为子夜
    const angle = dayRatio * Math.PI * 2;
    const sunHeight = Math.cos(angle);
    const day = smoothstep(-0.12, 0.3, sunHeight);
    const dusk = Math.max(0, 1 - Math.abs(sunHeight) / 0.35);
    const night = THREE.MathUtils.clamp((0.02 - sunHeight) / 0.34, 0, 1);

    // 日月轨迹与朝向
    const sinA = Math.sin(angle);
    const cosA = Math.cos(angle);
    this.sun.position.set(sinA * SKY_DISTANCE, cameraPos.y + cosA * SKY_DISTANCE, 0);
    this.sun.lookAt(cameraPos);
    this.moon.position.set(-sinA * SKY_DISTANCE, cameraPos.y - cosA * SKY_DISTANCE, 0);
    this.moon.lookAt(cameraPos);
    this.stars.rotation.z = -angle;
    const sunTextureAlpha = 1 - this.underwaterMix;
    (this.sun.material as THREE.MeshBasicMaterial).opacity = sunTextureAlpha;
    (this.moon.material as THREE.MeshBasicMaterial).opacity = sunTextureAlpha;
    const moonColor = this.tint.copy(MOON_COLOR).multiplyScalar(0.55 + 0.45 * day);
    (this.moon.material as THREE.MeshBasicMaterial).color.copy(moonColor);
    this.sun.visible = sunHeight > -0.25 && this.underwaterMix < 0.99;
    this.moon.visible = sunHeight < 0.25 && this.underwaterMix < 0.99;

    // 天穹：地平线取雾色，与地形雾严格对齐
    const fog = this.scene.fog;
    const horizon = fog ? fog.color : HORIZON_FALLBACK;
    const domeUniforms = (this.dome.material as THREE.ShaderMaterial).uniforms;
    (domeUniforms.uHorizon.value as THREE.Color).copy(horizon);
    // 白天用深蓝，夜里换成暗夜蓝，避免天顶直接黑成一片
    (domeUniforms.uZenith.value as THREE.Color)
      .copy(horizon)
      .lerp(ZENITH_DAY, 0.82 * day)
      .lerp(ZENITH_NIGHT, night * 0.9);
    (domeUniforms.uFogColor.value as THREE.Color).copy(horizon);
    (domeUniforms.uGlowColor.value as THREE.Color).copy(GLOW_DAY).lerp(GLOW_DUSK, dusk);
    domeUniforms.uGlow.value = 0.2 * day + 1.35 * dusk;
    (domeUniforms.uSunDir.value as THREE.Vector3).set(sinA, cosA, 0).normalize();
    domeUniforms.uUnderwater.value = this.underwaterMix;

    // 星空只在真正入夜后浮现
    const starMaterial = this.stars.material as THREE.PointsMaterial;
    starMaterial.opacity = Math.pow(night, 1.4) * 0.95 * (1 - this.underwaterMix);
    this.stars.visible = starMaterial.opacity > 0.01;

    // 云层颜色：白天纯白，黄昏偏暖，夜里压成暗蓝灰
    const cloudColor = this.tint.copy(CLOUD_NIGHT).lerp(CLOUD_DAY, day).lerp(CLOUD_WARM, dusk * 0.55);
    const cloudUniforms = (this.clouds.material as THREE.ShaderMaterial).uniforms;
    (cloudUniforms.uColor.value as THREE.Color).copy(cloudColor);
    (cloudUniforms.uHorizonColor.value as THREE.Color).copy(horizon);
    cloudUniforms.uAlphaScale.value = 1 - 0.92 * this.underwaterMix;

    // 云层淡出范围跟随雾距（水下雾很短，不作为依据）
    if (!this.underwater) this.surfaceFogFar = fog instanceof THREE.Fog ? fog.far : this.surfaceFogFar;
    const half = this.cloudHalf();
    cloudUniforms.uHalf.value = half;
    cloudUniforms.uFadeNear.value = half * CLOUD_FADE_NEAR_RATIO;
    cloudUniforms.uFadeFar.value = half;
    cloudUniforms.uPhase.value = (now / CLOUD_OSC_PERIOD) % 1;
    this.updateCloudDrift(now, half);
  }

  /** 云片漂移量 */
  private updateCloudDrift(now: number, half: number): void {
    const drift = this.cloudDrift.array as Float32Array;
    const span = half * 2;
    for (let i = 0; i < this.cloudInstances; i++) {
      drift[i * 2] = wrapToHalf(this.cloudVelocity[i * 2] * now, span, half);
      drift[i * 2 + 1] = wrapToHalf(this.cloudVelocity[i * 2 + 1] * now, span, half);
    }
    this.cloudDrift.needsUpdate = true;
  }

  dispose(): void {
    this.sun.geometry.dispose();
    (this.sun.material as THREE.Material).dispose();
    this.moon.geometry.dispose();
    (this.moon.material as THREE.Material).dispose();
    this.dome.geometry.dispose();
    (this.dome.material as THREE.Material).dispose();
    this.stars.geometry.dispose();
    (this.stars.material as THREE.Material).dispose();
    this.clouds.geometry.dispose();
    (this.clouds.material as THREE.Material).dispose();
    for (const texture of this.textures) texture.dispose();
    this.textures.length = 0;
    this.group.removeFromParent();
  }
}

/** 天穹半径 */
const DOME_RADIUS = 460;
/** 星空半径：紧贴天穹内侧 */
const STAR_RADIUS = 430;
/** 日月距离 */
const SKY_DISTANCE = 560;

/** 云层高度带 */
const CLOUD_ALTITUDE_MIN = 300;
const CLOUD_ALTITUDE_SPAN = 90;
/** 云团数量 */
const CLOUD_CLUSTERS = 30;
/** 云层淡出半径的上下限，以及与雾距的比例 */
const CLOUD_FADE_MIN = 260;
const CLOUD_FADE_MAX = 700;
const CLOUD_FADE_NEAR_RATIO = 0.4;
/** 云层起伏周期（秒） */
const CLOUD_OSC_PERIOD = 1024;
/** 基础风向（单位向量）与风级速度（单位/秒） */
const CLOUD_WIND = new THREE.Vector2(1, 0.34).normalize();
const CLOUD_WIND_SPEED = 1.15;

const HORIZON_FALLBACK = new THREE.Color('#a9d3ff');
const ZENITH_DAY = new THREE.Color('#3d78d2');
const ZENITH_NIGHT = new THREE.Color('#0d1730');
const CLOUD_DAY = new THREE.Color('#ffffff');
const CLOUD_NIGHT = new THREE.Color('#38445f');
const CLOUD_WARM = new THREE.Color('#ffb277');
const GLOW_DAY = new THREE.Color('#fff2cf');
const GLOW_DUSK = new THREE.Color('#ff9a4c');
const GLOW_FALLBACK = new THREE.Color('#ffd9a0');
const MOON_COLOR = new THREE.Color('#e8efff');

/** 与 GLSL smoothstep 一致的缓动 */
function smoothstep(edge0: number, edge1: number, x: number): number {
  const t = THREE.MathUtils.clamp((x - edge0) / (edge1 - edge0), 0, 1);
  return t * t * (3 - 2 * t);
}

/** 把数值折进 [-half, half) */
function wrapToHalf(value: number, span: number, half: number): number {
  return ((value % span) + span) % span - half;
}

function makeCanvas(size: number): { canvas: HTMLCanvasElement; ctx: CanvasRenderingContext2D } {
  const canvas = document.createElement('canvas');
  canvas.width = size;
  canvas.height = size;
  return { canvas, ctx: canvas.getContext('2d')! };
}

function finishTexture(canvas: HTMLCanvasElement): THREE.CanvasTexture {
  const texture = new THREE.CanvasTexture(canvas);
  texture.minFilter = THREE.LinearFilter;
  texture.magFilter = THREE.LinearFilter;
  texture.generateMipmaps = false;
  return texture;
}

/** 径向渐变贴图 */
function makeRadialTexture(
  size: number,
  stops: Array<[number, string]>,
  blobs = 0,
  blobAlpha = 0,
): THREE.CanvasTexture {
  const { canvas, ctx } = makeCanvas(size);
  const radius = size / 2;
  const gradient = ctx.createRadialGradient(radius, radius, 0, radius, radius, radius);
  for (const [at, color] of stops) gradient.addColorStop(at, color);
  ctx.fillStyle = gradient;
  ctx.beginPath();
  ctx.arc(radius, radius, radius, 0, Math.PI * 2);
  ctx.fill();
  if (blobs > 0) {
    ctx.globalCompositeOperation = 'destination-out';
    for (let i = 0; i < blobs; i++) {
      const a = Math.random() * Math.PI * 2;
      const r = Math.pow(Math.random(), 0.7) * radius * 0.4;
      const cx = radius + Math.cos(a) * r;
      const cy = radius + Math.sin(a) * r;
      const blobRadius = size * (0.04 + Math.random() * 0.06);
      const blob = ctx.createRadialGradient(cx, cy, 0, cx, cy, blobRadius);
      blob.addColorStop(0, `rgba(0,0,0,${blobAlpha})`);
      blob.addColorStop(1, 'rgba(0,0,0,0)');
      ctx.fillStyle = blob;
      ctx.beginPath();
      ctx.arc(cx, cy, blobRadius, 0, Math.PI * 2);
      ctx.fill();
    }
    ctx.globalCompositeOperation = 'source-over';
  }
  return finishTexture(canvas);
}

/** 一朵云片的柔边 */
function makeCloudTexture(): THREE.CanvasTexture {
  const size = 256;
  const { canvas, ctx } = makeCanvas(size);
  const radius = size / 2;
  ctx.globalCompositeOperation = 'lighter';
  for (let i = 0; i < 16; i++) {
    const a = Math.random() * Math.PI * 2;
    const r = Math.pow(Math.random(), 0.65) * radius * 0.48;
    const cx = radius + Math.cos(a) * r;
    const cy = radius + Math.sin(a) * r * 0.72;
    const blobRadius = size * (0.14 + Math.random() * 0.15);
    const blob = ctx.createRadialGradient(cx, cy, 0, cx, cy, blobRadius);
    blob.addColorStop(0, 'rgba(255,255,255,0.5)');
    blob.addColorStop(0.6, 'rgba(255,255,255,0.18)');
    blob.addColorStop(1, 'rgba(255,255,255,0)');
    ctx.fillStyle = blob;
    ctx.beginPath();
    ctx.arc(cx, cy, blobRadius, 0, Math.PI * 2);
    ctx.fill();
  }
  // 四周淡出，保证云片永远不会出现方形硬边
  ctx.globalCompositeOperation = 'destination-in';
  const edge = ctx.createRadialGradient(radius, radius, size * 0.08, radius, radius, size * 0.5);
  edge.addColorStop(0, 'rgba(255,255,255,1)');
  edge.addColorStop(0.62, 'rgba(255,255,255,0.72)');
  edge.addColorStop(1, 'rgba(255,255,255,0)');
  ctx.fillStyle = edge;
  ctx.fillRect(0, 0, size, size);
  // 底部比顶部略薄，云看起来更“平底”
  const base = ctx.createLinearGradient(0, size * 0.55, 0, size);
  base.addColorStop(0, 'rgba(255,255,255,1)');
  base.addColorStop(1, 'rgba(255,255,255,0.35)');
  ctx.fillStyle = base;
  ctx.fillRect(0, 0, size, size);
  ctx.globalCompositeOperation = 'source-over';
  return finishTexture(canvas);
}

interface CloudField {
  base: Float32Array;
  size: Float32Array;
  rot: Float32Array;
  phase: Float32Array;
  opacity: Float32Array;
  tint: Float32Array;
  velocity: Float32Array;
}

/** 撒云 */
function buildCloudField(half: number): CloudField {
  const puffs: Array<{ x: number; y: number; z: number; w: number; h: number; rot: number; opacity: number; tint: number; phase: [number, number, number, number]; vx: number; vz: number }> = [];
  for (let c = 0; c < CLOUD_CLUSTERS; c++) {
    const angle = Math.random() * Math.PI * 2;
    // 均匀铺满圆盘，近处不至于空，远处也不至于挤成一团
    const distance = Math.sqrt(Math.random()) * half * 0.94;
    const cx = Math.cos(angle) * distance;
    const cz = Math.sin(angle) * distance;
    const parallax = Math.random();
    const altitude = CLOUD_ALTITUDE_MIN + parallax * CLOUD_ALTITUDE_SPAN;
    const level = 2 + Math.floor(parallax * 3.999);
    const vx = CLOUD_WIND.x * CLOUD_WIND_SPEED * level;
    const vz = CLOUD_WIND.y * CLOUD_WIND_SPEED * level;
    const clusterRadius = 55 + Math.random() * 85;
    const clusterAlpha = 0.34 + Math.random() * 0.24;
    const clusterTint = 0.94 + Math.random() * 0.14;
    const phaseX = Math.random();
    const phaseY = Math.random();
    const phaseZ = Math.random();
    const puffCount = 4 + Math.floor(Math.random() * 3);
    for (let p = 0; p < puffCount; p++) {
      const a = Math.random() * Math.PI * 2;
      const r = Math.pow(Math.random(), 0.6) * clusterRadius;
      const dx = Math.cos(a) * r;
      const dz = Math.sin(a) * r;
      const dy = (Math.random() - 0.35) * clusterRadius * 0.5;
      const puffRadius = clusterRadius * (0.5 + Math.random() * 0.5);
      puffs.push({
        x: cx + dx,
        y: altitude + dy,
        z: cz + dz,
        w: puffRadius * (0.95 + Math.random() * 0.35),
        h: puffRadius * (0.72 + Math.random() * 0.4),
        rot: (Math.random() - 0.5) * 0.9,
        opacity: Math.min(0.86, clusterAlpha * (0.8 + Math.random() * 0.4)),
        // 云团上部略亮、下部略暗，叠出体积感
        tint: clusterTint + dy / Math.max(clusterRadius, 1) * 0.12 - Math.random() * 0.05,
        phase: [phaseX, phaseY, phaseZ, Math.random()],
        vx,
        vz,
      });
    }
  }
  const count = puffs.length;
  const field: CloudField = {
    base: new Float32Array(count * 3),
    size: new Float32Array(count * 2),
    rot: new Float32Array(count),
    phase: new Float32Array(count * 4),
    opacity: new Float32Array(count),
    tint: new Float32Array(count),
    velocity: new Float32Array(count * 2),
  };
  puffs.forEach((puff, i) => {
    field.base[i * 3] = puff.x;
    field.base[i * 3 + 1] = puff.y;
    field.base[i * 3 + 2] = puff.z;
    field.size[i * 2] = puff.w;
    field.size[i * 2 + 1] = puff.h;
    field.rot[i] = puff.rot;
    field.phase[i * 4] = puff.phase[0];
    field.phase[i * 4 + 1] = puff.phase[1];
    field.phase[i * 4 + 2] = puff.phase[2];
    field.phase[i * 4 + 3] = puff.phase[3];
    field.opacity[i] = puff.opacity;
    field.tint[i] = puff.tint;
    field.velocity[i * 2] = puff.vx;
    field.velocity[i * 2 + 1] = puff.vz;
  });
  return field;
}
