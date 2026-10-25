import * as THREE from 'three';
import { decodeSurfaceColors } from './WaterMaterial';

export type NaturalPhysicsLevel = 0 | 1 | 2 | 3 | 4 | 5;
const levels = new Map<number, NaturalPhysicsLevel>([
  ...[0,1,2,3,100,200,201,401].map(id => [id,1] as const),
  ...[4,101,102,202].map(id => [id,2] as const),
  ...[104,204,300].map(id => [id,3] as const),
  ...[103,203,301].map(id => [id,4] as const),
  ...[302,303,304].map(id => [id,5] as const),
]);

export function naturalPhysicsLevel(weather: number): NaturalPhysicsLevel {
  return levels.get(weather) ?? 0;
}

export function plantWindAttribute(positions: ArrayLike<number>, parts: Uint8Array | undefined, shrub: boolean): Float32Array {
  let height = .01;
  for (let i=1;i<positions.length;i+=3) height=Math.max(height,positions[i]);
  const data=new Float32Array(positions.length/3*4);
  for(let i=0;i<data.length/4;i++)data.set([parts?.[i]??3,Math.max(0,Math.min(1,positions[i*3+1]/height)),height,shrub?1:0],i*4);
  return data;
}

export function plantWindMaterial(base: THREE.Material, time: {value:number}, enabled: {value:number}, phase={value:0}): THREE.Material {
  const material=base.clone();
  material.onBeforeCompile=shader=>{
    decodeSurfaceColors(shader);
    shader.uniforms.plantWindTime=time;shader.uniforms.plantWindEnabled=enabled;
    shader.uniforms.plantWindPhase=phase;
    shader.vertexShader=`uniform float plantWindTime;
uniform float plantWindEnabled;
uniform float plantWindPhase;
attribute vec4 plantWind;
attribute float plantWindLevel;
vec4 plantWindAmplitudes(float level) {
  if(level<0.5)return vec4(0.0);
  if(level<1.5)return vec4(.008,0.0,0.0,0.0);
  if(level<2.5)return vec4(.015,.015,0.0,0.0);
  if(level<3.5)return vec4(.040,.015,.012,0.0);
  if(level<4.5)return vec4(.070,.055,.045,0.0);
  return vec4(.100,.080,.065,.070);
}
`+shader.vertexShader;
    shader.vertexShader=shader.vertexShader.replace('#include <begin_vertex>',`#include <begin_vertex>
if(plantWindEnabled>0.5 && plantWindLevel>0.5) {
  vec4 amplitudes=plantWindAmplitudes(plantWindLevel);
  float h=plantWind.y;
  float leaf=1.0-step(0.5,abs(plantWind.x-1.0));
  float other=step(2.5,plantWind.x);
  float phase=instanceMatrix[3].x*.17+instanceMatrix[3].z*.11+plantWindPhase;
  float crownWave=sin(plantWindTime*1.35+phase);
  float leafWave=sin(plantWindTime*4.3+phase+position.x*2.1+position.y*2.7+position.z*1.9);
  float trunk=step(4.5,plantWindLevel)*.006;
  float sway=(trunk+amplitudes.z*plantWind.w+amplitudes.y*leaf+amplitudes.w*other)*h*h*crownWave;
  sway+=leaf*amplitudes.x*h*leafWave*.25;
  vec2 direction=vec2(.92,.38);
  float yaw=atan(instanceMatrix[0].z,instanceMatrix[0].x);
  direction=mat2(cos(yaw),-sin(yaw),sin(yaw),cos(yaw))*direction;
  transformed.xz+=direction*plantWind.z*sway;
}
`);
  };
  material.customProgramCacheKey=()=> 'plant-wind';
  return material;
}
