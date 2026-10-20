import * as THREE from 'three';
import { decodeSurfaceColors } from '../WaterMaterial';
import { naturalPhysicsLevel } from '../PlantWind';

export class FlagWind {
  readonly material:THREE.MeshStandardMaterial;
  private readonly time={value:0};
  private readonly enabled={value:0};
  private readonly flags=new Map<THREE.Mesh,{cx:number;cz:number;revision:string;level:number;swing:number;velocity:number}>();
  constructor(){
    this.material=new THREE.MeshStandardMaterial({vertexColors:true,flatShading:true,roughness:.95,side:THREE.DoubleSide});
    this.material.onBeforeCompile=shader=>{
      decodeSurfaceColors(shader);shader.uniforms.flagTime=this.time;shader.uniforms.flagEnabled=this.enabled;
      shader.vertexShader=`uniform float flagTime;
uniform float flagEnabled;
attribute vec4 flagWind;
attribute float flagWindLevel;
attribute float flagSwing;
`+shader.vertexShader;
      shader.vertexShader=shader.vertexShader.replace('#include <begin_vertex>',`#include <begin_vertex>
if(flagEnabled>.5 && flagWindLevel>.5 && flagWind.w>.5) {
  float t=flagWind.x;
  float phase=modelMatrix[3].x*.17+modelMatrix[3].z*.11;
  float amplitude=.009+.008*flagWindLevel;
  float ripple=sin(flagTime*(2.0+flagWindLevel*.3)-t*6.0+flagWind.y*4.0+phase);
  transformed.z+=flagWind.z*t*t*amplitude*(flagSwing+.4*ripple);
  transformed.x+=flagWind.z*t*t*amplitude*.18*sin(flagTime*1.1+phase);
}
`);
    };
    this.material.customProgramCacheKey=()=> 'hanging-flag-wind';
  }
  add(mesh:THREE.Mesh,cx:number,cz:number):void {
    mesh.geometry.setAttribute('flagWindLevel',new THREE.BufferAttribute(new Uint8Array(mesh.geometry.getAttribute('position').count),1).setUsage(THREE.DynamicDrawUsage));
    mesh.geometry.setAttribute('flagSwing',new THREE.BufferAttribute(new Float32Array(mesh.geometry.getAttribute('position').count),1).setUsage(THREE.DynamicDrawUsage));
    this.flags.set(mesh,{cx,cz,revision:'',level:0,swing:0,velocity:0});
  }
  remove(mesh:THREE.Mesh):void {this.flags.delete(mesh);}
  update(dt:number,on:boolean,weatherAt:(cx:number,cz:number)=>number,revision:string,tint:THREE.Color):void {
    this.enabled.value=on?1:0;this.material.color.copy(tint);
    if(!on)return;
    const delta=Math.min(.1,Math.max(0,dt));this.time.value+=delta;
    for(const [mesh,entry] of this.flags){
      if(!mesh.visible)continue;
      const attribute=mesh.geometry.getAttribute('flagWindLevel') as THREE.BufferAttribute;
      if(entry.revision!==revision){
        const level=naturalPhysicsLevel(weatherAt(entry.cx,entry.cz));
        if(attribute.getX(0)!==level){(attribute.array as Uint8Array).fill(level);attribute.needsUpdate=true;}
        entry.level=level;entry.revision=revision;
      }
      if(delta>0&&entry.level>0){
        const phase=mesh.position.x*.17+mesh.position.z*.11;
        const target=Math.sin(this.time.value*(.8+entry.level*.12)+phase),steps=Math.ceil(delta*120),step=delta/steps;
        for(let i=0;i<steps;i++){entry.velocity+=(18*(target-entry.swing)-5*entry.velocity)*step;entry.swing+=entry.velocity*step;}
        const swing=mesh.geometry.getAttribute('flagSwing') as THREE.BufferAttribute;
        (swing.array as Float32Array).fill(entry.swing);swing.needsUpdate=true;
      }
    }
  }
  dispose():void {this.flags.clear();this.material.dispose();}
}
