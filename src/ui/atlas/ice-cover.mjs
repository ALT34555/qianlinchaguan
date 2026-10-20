export const ICE_RGB=[218,235,237];
export function iceCoverage(temperature){
  const t=Math.max(0,Math.min(1,-temperature/6));return t*t*(3-2*t);
}
export function iceColorValue(value,temperature){
  const amount=iceCoverage(temperature);if(!amount)return value;if(amount===1)return 0xdaebed;
  const r=value>>>16&255,g=value>>>8&255,b=value&255;
  return Math.round(r+(ICE_RGB[0]-r)*amount)<<16|Math.round(g+(ICE_RGB[1]-g)*amount)<<8|Math.round(b+(ICE_RGB[2]-b)*amount);
}
