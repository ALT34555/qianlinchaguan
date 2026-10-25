import {hash2} from '../../core/math/Random';

export interface DecorationEnvironment {moisture:number;activity:number}
export type DecorationEnvironmentSampler=(x:number,z:number)=>DecorationEnvironment;
const clamp=(v:number)=>Math.max(0,Math.min(1,v));
function smooth(a:number,b:number,v:number){const t=clamp((v-a)/(b-a));return t*t*(3-2*t);}
export function decorationPatch(x:number,z:number,seed:number,span:number):number {
  const u=x/span,v=z/span,ix=Math.floor(u),iz=Math.floor(v),tx=smooth(0,1,u-ix),tz=smooth(0,1,v-iz);
  return (hash2(ix,iz,seed)*(1-tx)+hash2(ix+1,iz,seed)*tx)*(1-tz)+(hash2(ix,iz+1,seed)*(1-tx)+hash2(ix+1,iz+1,seed)*tx)*tz;
}
// 共享节点，跨区块连续
export function decorationEnvironmentSampler(sample:DecorationEnvironmentSampler):DecorationEnvironmentSampler {
  const nodes=new Map<string,DecorationEnvironment>();
  const at=(x:number,z:number)=>{const key=x+','+z;let value=nodes.get(key);if(!value){value=sample(x*64,z*64);nodes.set(key,value);}return value;};
  return (x,z)=>{
    const u=x/64,v=z/64,ix=Math.floor(u),iz=Math.floor(v),tx=u-ix,tz=v-iz,a=at(ix,iz),b=at(ix+1,iz),c=at(ix,iz+1),d=at(ix+1,iz+1);
    const read=(key:keyof DecorationEnvironment)=>clamp((a[key]*(1-tx)+b[key]*tx)*(1-tz)+(c[key]*(1-tx)+d[key]*tx)*tz);
    return {moisture:read('moisture'),activity:read('activity')};
  };
}
export function decorationHabitat(x:number,z:number,seed:number,slope:number,environment:DecorationEnvironment){
  const canopy=smooth(.28,.72,decorationPatch(x,z,seed^0x4f726573,96));
  const patch=decorationPatch(x,z,seed^0x726f636b,32),moisture=clamp(environment.moisture);
  return {
    trees:canopy*(.35+.65*moisture)*(1-smooth(.12,.85,slope)),
    understory:canopy<.025?0:(.18+.82*canopy)*(.5+.5*moisture)*(1-smooth(.35,1.65,slope)),
    rocks:smooth(.42,.76,patch)*(.35+.65*smooth(.15,1.2,slope))*(.75+clamp(environment.activity)*.5),
    exposed:smooth(.25,1.1,slope),
  };
}
