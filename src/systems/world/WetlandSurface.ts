import {hash2} from '../../core/math/Random';
export interface WetlandPond {x:number;z:number;ratio:number}
/** 世界坐标浅潭，边缘留出独立河滩。 */
export function wetlandPond(x:number,z:number,seed:number,period=0):WetlandPond{
  const size=32,tx=Math.floor(x/size),tz=Math.floor(z/size);let best:WetlandPond={x,z,ratio:Infinity};
  for(let j=tz-1;j<=tz+1;j++)for(let i=tx-1;i<=tx+1;i++){
    const h=period?((i%(period/size))+(period/size))%(period/size):i;
    const random=(salt:number)=>hash2(h,j,seed+salt);
    const cx=(i+.5)*size+(random(71)-.5)*8,cz=(j+.5)*size+(random(97)-.5)*8;
    const ratio=Math.hypot((x-cx)/(5+random(113)*4),(z-cz)/(5+random(137)*5));
    if(ratio<best.ratio)best={x:cx,z:cz,ratio};
  }
  return best;
}
