import {runoffMm,YEAR} from './water-balance.mjs';
const clamp=(v,a,b)=>Math.max(a,Math.min(b,v));
const smooth=t=>t*t*(3-2*t);
export const SMALL_MAIN_MIN_DEPTH=2,LARGE_MAIN_MAX_DEPTH=16;
export const DECORATIVE_MAX_DEPTH=2;
export const DECORATIVE_CATCHMENT_KM2=1;
export const DECORATIVE_MIN_WIDTH=2,DECORATIVE_MAX_WIDTH=64;
const WIDTH_STAGES=[[0,1],[10,2],[1000,3],[10000,5],[100000,8]];

// 河门槛：∝格宽并随基准降水抬升，小流量交给装饰支流
export function macroRiverThreshold(scale,precipitation=1){return .96*scale*Math.pow(Math.max(.2,precipitation),.6);}

export function macroBandWidth(discharge){
  const q=Math.max(0,discharge);
  for(let k=1;k<WIDTH_STAGES.length;k++){
    const [start,low]=WIDTH_STAGES[k-1],[end,high]=WIDTH_STAGES[k];
    if(q<=end){const t=clamp(Math.log1p(q-start)/Math.log1p(end-start),0,1);return (low+(high-low)*smooth(t))*64;}
  }
  return WIDTH_STAGES.at(-1)[1]*64;
}

export function macroDepth(discharge){
  const q=Math.max(0,discharge);
  for(const [start,end,low,high] of [[0,10,SMALL_MAIN_MIN_DEPTH,4],[10,1000,4,8],[1000,10000,8,LARGE_MAIN_MAX_DEPTH]]){
    if(q<=end){const t=clamp(Math.log1p(Math.max(0,q-start))/Math.log1p(end-start),0,1);return low+(high-low)*smooth(t);}
  }
  return LARGE_MAIN_MAX_DEPTH;
}

// 固定参考汇水面积的年均估算
export function decorativeDimensions(rainfall,evaporation){
  const rainfallMmYear=Math.max(0,rainfall),evaporationMmYear=Math.max(0,evaporation),effectiveCatchmentKm2=DECORATIVE_CATCHMENT_KM2;
  const runoffMmYear=runoffMm(rainfallMmYear,evaporationMmYear),dischargeM3s=runoffMmYear*effectiveCatchmentKm2*1000/YEAR;
  const strength=-Math.expm1(-dischargeM3s/.04),halfWidth=(DECORATIVE_MIN_WIDTH+(DECORATIVE_MAX_WIDTH-DECORATIVE_MIN_WIDTH)*Math.sqrt(strength))/2,depth=DECORATIVE_MAX_DEPTH*strength;
  return {rainfallMmYear,evaporationMmYear,effectiveCatchmentKm2,runoffMmYear,dischargeM3s,dischargeRole:'decorative-estimate',halfWidth,depth};
}
