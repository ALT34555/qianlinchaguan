import * as THREE from 'three';
import type { WorldLighting } from './WorldLighting';

/** 生成器的 Uint8 RGB 为 sRGB */
export function decodeSurfaceColors(shader: { fragmentShader: string }): void {
  shader.fragmentShader = shader.fragmentShader.replace('#include <color_fragment>', `
    #if defined(USE_COLOR)
      diffuseColor.rgb *= sRGBTransferEOTF(vec4(vColor.rgb, 1.0)).rgb;
    #endif
  `);
}

/** 原创程序水面 */
export class WaterMaterial extends THREE.MeshStandardMaterial {
  private readonly waterUniforms = {
    uWaterTime: { value: 0 }, uWaterDaylight: { value: 1 }, uWaterNight: { value: 0 },
    uWaterHorizon: { value: new THREE.Color() }, uWaterZenith: { value: new THREE.Color() },
    uWaterSun: { value: new THREE.Vector3() }, uWaterMoon: { value: new THREE.Vector3() },
  };

  constructor() {
    super({ vertexColors: true, transparent: true, opacity: .78, depthWrite: false,
      side: THREE.DoubleSide, roughness: .24, metalness: .08 });
    this.onBeforeCompile = shader => {
      decodeSurfaceColors(shader);
      Object.assign(shader.uniforms, this.waterUniforms);
      shader.vertexShader = `
        attribute float waterDepth;
        attribute vec2 waterFlow;
        varying vec2 vWaterFlow;
        varying float vWaterDepth;
        varying vec3 vWaterWorld;
      ` + shader.vertexShader;
      shader.vertexShader = shader.vertexShader.replace('#include <worldpos_vertex>', `
        #include <worldpos_vertex>
        vWaterWorld = (modelMatrix * vec4(transformed, 1.0)).xyz;
        vWaterDepth = waterDepth;
        vWaterFlow = waterFlow;
      `);
      shader.fragmentShader = `
        uniform float uWaterTime;
        uniform float uWaterDaylight;
        uniform float uWaterNight;
        uniform vec3 uWaterHorizon;
        uniform vec3 uWaterZenith;
        uniform vec3 uWaterSun;
        uniform vec3 uWaterMoon;
        varying float vWaterDepth;
        varying vec2 vWaterFlow;
        varying vec3 vWaterWorld;
      ` + shader.fragmentShader;
      shader.fragmentShader = shader.fragmentShader.replace('#include <normal_fragment_maps>', `
        #include <normal_fragment_maps>
        vec3 waterBaseNormal = inverseTransformDirection(normal, viewMatrix);
        float waterTop = smoothstep(0.45, 0.9, abs(waterBaseNormal.y));
        // 法线波动而不位移几何，岸线、碰撞和相邻区块始终吻合。
        vec2 wp = vWaterWorld.xz - vWaterFlow * uWaterTime;
        float w1 = dot(wp, vec2(0.72, 0.43)) - uWaterTime * 0.85;
        float w2 = dot(wp, vec2(-0.38, 1.12)) + uWaterTime * 0.62;
        float w3 = dot(wp, vec2(1.63, -0.82)) - uWaterTime * 1.14;
        vec2 slope = cos(w1) * vec2(0.72, 0.43) * 0.045
          + cos(w2) * vec2(-0.38, 1.12) * 0.022
          + cos(w3) * vec2(1.63, -0.82) * 0.012;
        vec3 rippleNormal = normalize(vec3(-slope.x, 1.0, -slope.y)) * faceDirection;
        vec3 waterWorldNormal = normalize(mix(waterBaseNormal, rippleNormal, waterTop));
        normal = normalize(mat3(viewMatrix) * waterWorldNormal);
      `);
      shader.fragmentShader = shader.fragmentShader.replace('#include <opaque_fragment>', `
        vec3 waterView = normalize(cameraPosition - vWaterWorld);
        float waterFresnel = 0.025 + 0.65 * pow(1.0 - max(dot(waterView, waterWorldNormal), 0.0), 5.0);
        vec3 waterReflected = reflect(-waterView, waterWorldNormal);
        vec3 waterSky = mix(uWaterHorizon, uWaterZenith, pow(clamp(waterReflected.y, 0.0, 1.0), 0.7));
        float sunGlint = pow(max(dot(waterReflected, uWaterSun), 0.0), 280.0) * uWaterDaylight;
        float moonGlint = pow(max(dot(waterReflected, uWaterMoon), 0.0), 480.0) * uWaterNight;
        float reflectionWeight = gl_FrontFacing ? waterFresnel * waterTop : 0.0;
        outgoingLight = mix(outgoingLight, waterSky, reflectionWeight);
        outgoingLight += (vec3(1.0, 0.82, 0.55) * sunGlint * 0.8
          + vec3(0.65, 0.79, 1.0) * moonGlint * 0.35) * waterTop * float(gl_FrontFacing);
        float shore = (1.0 - smoothstep(0.08, 0.7, vWaterDepth)) * smoothstep(0.0, 0.08, vWaterDepth);
        float shoreWave = smoothstep(0.55, 0.95, sin(vWaterDepth * 18.0 - uWaterTime * 1.4 + sin(w1) * 0.6));
        outgoingLight += vec3(0.4, 0.48, 0.45) * shore * shoreWave * (0.08 + 0.32 * uWaterDaylight) * waterTop;
        // 真正陡落的面产生沿高度下落的水丝与白沫；缓坡水面不加竖向纹理。
        float falling=(1.0-waterTop)*smoothstep(1.2,4.0,length(vWaterFlow));
        float streak=smoothstep(0.1,0.85,sin(dot(vWaterWorld.xz,vec2(2.3,1.7))+vWaterWorld.y*.6+uWaterTime*7.0));
        outgoingLight=mix(outgoingLight,vec3(.63,.78,.8)*(0.25+uWaterDaylight*.65),falling*(.35+streak*.45));
        diffuseColor.a = clamp(0.18 + 0.58 * (1.0 - exp(-vWaterDepth * 0.32)) + reflectionWeight * 0.25, 0.18, 0.9);
        diffuseColor.a=max(diffuseColor.a,falling*.85);
        #include <opaque_fragment>
      `);
    };
  }

  override customProgramCacheKey(): string { return 'qianlin-water-v3-falls'; }

  update(time: number, lighting: WorldLighting, daylight: number, night: number): void {
    // 周期远大于一次游玩
    this.waterUniforms.uWaterTime.value = time;
    this.waterUniforms.uWaterDaylight.value = daylight;
    this.waterUniforms.uWaterNight.value = night;
    this.waterUniforms.uWaterHorizon.value.copy(lighting.fogColor);
    this.waterUniforms.uWaterZenith.value.copy(lighting.zenithColor);
    this.waterUniforms.uWaterSun.value.copy(lighting.sunDirection);
    this.waterUniforms.uWaterMoon.value.copy(lighting.moonDirection);
    this.color.copy(lighting.tint);
  }
}
