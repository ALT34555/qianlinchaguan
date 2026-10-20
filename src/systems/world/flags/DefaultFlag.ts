import { FlagBuilder, type FlagGeometry } from './geometry';
import { buildFlagById } from './Flags';
import { appendFlagRodCollider } from './FlagPoleColliders';
import type { V3 } from '../../../core/math/Geometry';

export const DEFAULT_FLAG_SIZE=Object.freeze({width:3.6,drop:4.1,top:5.9,poleHeight:6.5,radius:.11});
export interface DefaultFlagModel {geometry:FlagGeometry;wind:Float32Array;colliders:Float32Array}

export function buildDefaultFlag(mainColor:string,trimColor:string):DefaultFlagModel {
  const size=DEFAULT_FLAG_SIZE,wood:[number,number,number]=[144,133,121],out=new FlagBuilder(),colliders:number[]=[];
  const rod=(start:V3,end:V3,radius:number)=>{
    out.rod(start,end,radius,wood,{sides:6,mat:'pole'});
    appendFlagRodCollider(colliders,start,end,radius);
  };
  rod([0,0,0],[0,size.poleHeight,0],size.radius);
  rod([-2.02,6,.1],[2.02,6,.1],.105);
  rod([-2.02,6,.1],[-2.34,6.37,.1],.095);
  rod([2.02,6,.1],[2.34,6.37,.1],.095);
  const cloth=buildFlagById('flag.rect.suqi',{fly:size.drop,hoist:size.width,sleeve:false,trimWidth:.13,
    charge:{kind:'none'},palette:{field:mainColor,trim:trimColor},
    wave:{amplitude:0,droop:0,twist:0,columns:10,bands:9,doubleSided:false}});
  out.bake(cloth,{rotZ:-Math.PI/2,offset:[-size.width/2,size.top,.19]});
  const geometry=out.build(),wind=new Float32Array(geometry.vertexCount*4);
  for(const group of geometry.groups){
    if(!['cloth','trim','charge','emblem'].includes(group.mat))continue;
    for(let j=group.start;j<group.start+group.count;j++){
      const i=geometry.indices[j];if(wind[i*4+3])continue;
      const t=Math.max(0,Math.min(1,(size.top-geometry.positions[i*3+1])/size.drop));
      const u=(geometry.positions[i*3]+size.width/2)/size.width;
      wind.set([t,u,size.drop,1],i*4);geometry.positions[i*3+2]+=.1*t*t;
    }
  }
  geometry.colliders=new Float32Array(colliders);
  return {geometry,wind,colliders:geometry.colliders};
}
