import * as THREE from 'three';
import type { WorldLighting } from './WorldLighting';

/** 水与浪：高级水面波浪着色块 */
export class WaterWaves {
  private enabled = false;
  private readonly sky = new THREE.Color();
  private readonly uniforms = {
    uWaveTime: { value: 0 },
    uWaveSky: { value: new THREE.Color() },
    uWaveHorizon: { value: new THREE.Color() },
    uWaveSun: { value: new THREE.Vector3(0, 1, 0) },
    uWaveDay: { value: 1 },
    uWaveNight: { value: 0 },
  };

  get active(): boolean { return this.enabled; }

  setEnabled(enabled: boolean): boolean {
    if (this.enabled === enabled) return false;
    this.enabled = enabled;
    return true;
  }

  /** 复用材质 uniform 对象 */
  attach(shader: { uniforms: Record<string, THREE.IUniform> }): void {
    Object.assign(shader.uniforms, this.uniforms);
  }

  update(time: number, lighting: WorldLighting, daylight: number, night: number): void {
    this.uniforms.uWaveTime.value = time;
    this.uniforms.uWaveSun.value.copy(lighting.sunDirection);
    this.uniforms.uWaveDay.value = daylight;
    this.uniforms.uWaveNight.value = night;
    this.uniforms.uWaveHorizon.value.copy(lighting.fogColor);
    this.sky.copy(lighting.fogColor).lerp(lighting.zenithColor, .62)
      .lerp(lighting.sun.color, Math.min(.3, daylight * .3 * Math.max(0, lighting.sunDirection.y)))
      .lerp(lighting.moon.color, night * .22);
    this.uniforms.uWaveSky.value.copy(this.sky);
  }

  /** 依赖水面varying与waterTop */
  readonly header = `
    uniform float uWaveTime;
    uniform vec3 uWaveSky;
    uniform vec3 uWaveHorizon;
    uniform vec3 uWaveSun;
    uniform float uWaveDay;
    uniform float uWaveNight;
    float waveFlowSpeed;
    float waveDeep;
    float waveAlong;
    float waveAcross;
    float waveSwell;
    vec3 waveWorldNormal;
    float waveHash(vec2 p) {
      p = mod(p, 4096.0);
      vec3 p3 = fract(vec3(p.xyx) * .1031);
      p3 += dot(p3, p3.yzx + 33.33);
      return fract((p3.x + p3.y) * p3.z);
    }
    float waveNoise(vec2 p) {
      vec2 i = floor(p), f = fract(p);
      vec2 u = f * f * (3.0 - 2.0 * f);
      float a = waveHash(i), b = waveHash(i + vec2(1.0, 0.0));
      float c = waveHash(i + vec2(0.0, 1.0)), d = waveHash(i + vec2(1.0, 1.0));
      return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
    }
  `;

  /** 接在水面法线之后 */
  readonly normalSnippet = `
    waveFlowSpeed = length(vWaterFlow);
    vec2 waveFlowDir = vWaterFlow / max(waveFlowSpeed, 1e-4);
    waveDeep = smoothstep(0.4, 2.6, vWaterDepth);
    vec2 wavePos = vWaterWorld.xz;
    waveAlong = dot(wavePos, waveFlowDir);
    waveAcross = dot(wavePos, vec2(-waveFlowDir.y, waveFlowDir.x));
    float waveAmp = mix(0.35, 1.0, waveDeep) * waterTop;
    vec2 waveSlope = vec2(0.0);
    float wavePhase = 0.0;
    float waveHeight = 0.0;
    wavePhase = dot(wavePos, vec2(0.78, 0.62)) * 0.22 - uWaveTime * 0.55;
    waveSlope += vec2(0.78, 0.62) * (cos(wavePhase) * 0.21);
    waveHeight += sin(wavePhase) * 0.62;
    wavePhase = dot(wavePos, vec2(-0.35, 0.94)) * 0.31 - uWaveTime * 0.78;
    waveSlope += vec2(-0.35, 0.94) * (cos(wavePhase) * 0.15);
    waveHeight += sin(wavePhase) * 0.36;
    wavePhase = dot(wavePos, vec2(0.94, -0.34)) * 0.63 - uWaveTime * 1.15;
    waveSlope += vec2(0.94, -0.34) * (cos(wavePhase) * 0.14);
    waveHeight += sin(wavePhase) * 0.18;
    wavePhase = dot(wavePos, vec2(-0.66, -0.75)) * 1.17 - uWaveTime * 1.65;
    waveSlope += vec2(-0.66, -0.75) * (cos(wavePhase) * 0.11);
    waveHeight += sin(wavePhase) * 0.08;
    wavePhase = dot(wavePos, vec2(0.42, -0.91)) * 2.35 - uWaveTime * 2.4;
    waveSlope += vec2(0.42, -0.91) * (cos(wavePhase) * 0.07);
    waveHeight += sin(wavePhase) * 0.03;
    float waveChannel = smoothstep(0.06, 0.5, waveFlowSpeed) * smoothstep(0.8, 3.2, vWaterDepth);
    float waveTrainPhase = waveAlong * 0.85 - uWaveTime * (0.9 + waveFlowSpeed * 0.8);
    float waveCrossPhase = waveAcross * 1.7 + uWaveTime * 0.7 + sin(waveTrainPhase) * 0.8;
    waveSlope += waveFlowDir * (cos(waveTrainPhase) * 0.09 * waveChannel);
    waveSlope += vec2(-waveFlowDir.y, waveFlowDir.x) * (cos(waveCrossPhase) * 0.11 * waveChannel);
    waveHeight += (sin(waveTrainPhase) * 0.55 + sin(waveCrossPhase) * 0.35) * waveChannel;
    waveSlope *= waveAmp;
    waveWorldNormal = normalize(vec3(-waveSlope.x, 1.0, -waveSlope.y)) * faceDirection;
    waveWorldNormal = normalize(mix(waterWorldNormal, waveWorldNormal, waterTop * 0.85));
    normal = normalize(mat3(viewMatrix) * waveWorldNormal);
    waveSwell = clamp(waveHeight * 0.8, -1.0, 1.0);
  `;

  /** 接在水面着色之后 */
  readonly surfaceSnippet = `
    float waveStill = 1.0 - smoothstep(0.04, 0.42, waveFlowSpeed);
    float waveGust = sin(vWaterWorld.x * 0.23 + vWaterWorld.z * 0.19) * 1.3
      + sin(vWaterWorld.x * 0.57 - vWaterWorld.z * 0.43 + 1.7) * 0.6;
    float flowMix = smoothstep(0.02, 0.35, waveFlowSpeed);
    vec2 waveFoamPos = mix(vWaterWorld.xz * 0.14 - vec2(uWaveTime * 0.03, -uWaveTime * 0.02),
      vec2(waveAcross * 0.36, waveAlong * 0.1 - uWaveTime * (0.25 + waveFlowSpeed * 0.22)), flowMix);
    float wavePatch = waveNoise(waveFoamPos) * 0.65 + waveNoise(waveFoamPos * 1.9 + 7.0) * 0.35;
    float waveShore = 1.0 - smoothstep(0.15, 1.45, vWaterDepth);
    float waveLap = smoothstep(0.5, 0.92, sin(vWaterDepth * 8.5 + uWaveTime * 1.35 + waveGust) * 0.5
      + sin(vWaterDepth * 4.7 + uWaveTime * 0.85 + waveGust * 1.4) * 0.32 + wavePatch * 0.28);
    float waveFilm = (1.0 - smoothstep(0.0, 0.5, vWaterDepth)) * (0.3 + 0.45 * wavePatch);
    float waveRun = 0.5 + 0.5 * sin(waveAlong * 0.22 - uWaveTime * (0.5 + waveFlowSpeed * 0.4) + wavePatch * 2.5);
    float waveCrest = smoothstep(0.48, 0.86, waveSwell * 0.45 + wavePatch * 0.55) * smoothstep(2.5, 6.0, vWaterDepth);
    float waveBreak = smoothstep(0.4, 0.76, wavePatch * 0.8 + waveSwell * 0.2) * waveChannel * (0.55 + 0.45 * waveRun);
    float waveFoam = clamp(waveCrest * 0.45 + waveBreak * 0.8
      + waveLap * waveShore * (0.62 + 0.35 * waveStill) + waveFilm * 0.34, 0.0, 1.0);
    waveFoam *= waterTop * (1.0 - vWaterFall) * float(gl_FrontFacing);
    vec3 waveFoamColor = sRGBTransferEOTF(vec4(vWaterFoam, 1.0)).rgb;
    outgoingLight += waveFoamColor * (waveFoam * (0.16 + 0.84 * uWaveDay));
    vec3 waveView = normalize(cameraPosition - vWaterWorld);
    float waveFresnel = 0.02 + 0.6 * pow(1.0 - max(dot(waveView, waveWorldNormal), 0.0), 5.0);
    vec3 waveReflected = reflect(-waveView, waveWorldNormal);
    float waveSunSheen = pow(max(dot(waveReflected, uWaveSun), 0.0), 420.0) * 0.45;
    float waveGlitter = pow(max(dot(waveReflected, uWaveSun), 0.0), 1400.0)
      * (0.5 + 0.5 * sin(dot(vWaterWorld.xz, vec2(11.3, -7.9)) + uWaveTime * 6.0));
    float waveMoonSheen = pow(max(dot(waveReflected, -uWaveSun), 0.0), 90.0) * 0.3 * uWaveNight;
    vec3 waveSkyTint = mix(uWaveHorizon, uWaveSky, clamp(waveWorldNormal.y * 0.5 + 0.5, 0.0, 1.0));
    float waveBands = 0.5 + 0.5 * sin(dot(vWaterWorld.xz, vec2(0.61, -0.79)) * 0.75
      + uWaveTime * 0.55 + waveSwell * 2.0);
    float waveSheen = (waveBands * 0.05 + waveSunSheen + waveGlitter * 0.6 + waveMoonSheen)
      * (0.35 + 0.65 * uWaveDay) * waterTop * float(gl_FrontFacing);
    outgoingLight += waveSkyTint * waveSheen;
    outgoingLight *= 1.0 + waveSwell * 0.05 * waveDeep * waterTop;
    diffuseColor.a = clamp(diffuseColor.a + waveFoam * 0.2 + waveFresnel * 0.04, 0.18, 0.94);
  `;
}
