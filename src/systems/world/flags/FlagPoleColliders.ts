import { HARD_TRUNK } from '../DecorationCollisionShapes';
import type { ResolvedFlagpoleParams } from './types';
import { rotateX, rotateY, rotateZ, type V3 } from '../../../core/math/Geometry';

export function appendFlagRodCollider(out:number[],start:V3,end:V3,radius:number):void {
  const horizontal=Math.hypot(end[0]-start[0],end[2]-start[2]);
  if(horizontal<1e-6){out.push(start[0],Math.min(start[1],end[1]),start[2],radius,radius,Math.max(start[1],end[1]),HARD_TRUNK);return;}
  const steps=Math.max(1,Math.ceil(horizontal/(radius*1.5)));
  for(let i=0;i<=steps;i++){
    const t=i/steps,x=start[0]+(end[0]-start[0])*t,y=start[1]+(end[1]-start[1])*t,z=start[2]+(end[2]-start[2])*t;
    const halfY=radius+Math.abs(end[1]-start[1])/(2*steps);
    out.push(x,y-halfY,z,radius*1.25,radius*1.25,y+halfY,HARD_TRUNK);
  }
}

export function flagpoleColliders(params:ResolvedFlagpoleParams,scale=1,yaw=0):Float32Array {
  const out:number[]=[];
  const point=(p:V3):V3=>{
    let [x,y,z]=p;[x,y]=rotateZ(x,y,params.tiltZ);[y,z]=rotateX(y,z,params.tiltX);[x,z]=rotateY(x,z,yaw);
    return [x*scale,y*scale,z*scale];
  };
  appendFlagRodCollider(out,point([0,0,0]),point([0,params.height,0]),params.radius*scale);
  const arm=params.arm;
  if(arm){
    const x0=params.plate.style==='none'?0:params.plate.depth;
    appendFlagRodCollider(out,point([x0,arm.at,0]),point([x0+arm.length,arm.at+arm.rise,0]),arm.radius*scale);
    if(arm.brace>0)appendFlagRodCollider(out,point([0,arm.at-arm.brace,0]),point([x0+arm.length*.45,arm.at+arm.rise*.45,0]),arm.radius*.55*scale);
  }
  return new Float32Array(out);
}

export function offsetFlagColliders(data:Float32Array,x:number,y:number,z:number):Float32Array {
  const result=data.slice();
  for(let i=0;i<result.length;i+=7){result[i]+=x;result[i+1]+=y;result[i+2]+=z;result[i+5]+=y;}
  return result;
}
