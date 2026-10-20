// 唯一水量口径：降水与潜在蒸发都只在这里换算
export const YEAR=31557600;

// 径流深度 mm/yr，无零台阶
export function runoffMm(rainfall,evaporation){
  const p=Math.max(0,rainfall)||0,e=Math.max(0,evaporation)||0;
  if(!p)return 0;
  const share=p/(p+e+1e-9);
  return p*share*Math.sqrt(share);
}

export function runoffM3s(rainfall,evaporation,areaKm2){return runoffMm(rainfall,evaporation)*areaKm2*1000/YEAR;}

export function evaporationM3s(evaporation,areaKm2){return Math.max(0,evaporation)*areaKm2*1000/YEAR;}
