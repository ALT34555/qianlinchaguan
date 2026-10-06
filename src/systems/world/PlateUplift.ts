/** 第三层 */
export const PLATE_BASES = [0,512,1024,2048] as const;
export type PlateTier = 0 | 1 | 2 | 3;
export interface PlateSample {tier: PlateTier;base: number;edge: number}
const smooth=(v:number)=>{const t=Math.max(0,Math.min(1,v));return t*t*(3-2*t);};
export function plateUplift(signal:number,activity=0):PlateSample{
  // 同一低频场形成大片同级底座
  // 常规最常见
  const thresholds=[.14,.40,.70],width=.035;
  const value=signal+activity/9*.055;
  let tier:PlateTier=0,base=0,edge=0;
  for(let i=0;i<3;i++){
    const t=smooth((value-thresholds[i])/width);
    if(value>=thresholds[i])tier=(i+1) as PlateTier;
    base+=(PLATE_BASES[i+1]-PLATE_BASES[i])*t;
    edge=Math.max(edge,4*t*(1-t));
  }
  return {tier,base,edge};
}
