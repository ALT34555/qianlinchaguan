const clamp=v=>Math.max(0,Math.min(1,v)),smooth=v=>{const t=clamp(v);return t*t*(3-2*t);};

// 低岸处形成缓坡，外缘回接原地面
export function bankedChannelSection(distance,radius,bed,level,natural){
  const r=distance/radius;if(r>=1.8)return null;
  if(r>=1){
    const height=natural+Math.max(0,level+.18-natural)*(1-smooth((r-1)/.8));
    return {height,wet:false,bank:true,waterAllowed:false};
  }
  const carved=natural-Math.max(0,natural-bed)*(1-r*r)**2;
  const height=Math.max(carved,bed+(level+.18-bed)*smooth((r-.6)/.4));
  return {height,wet:height<level,bank:height>natural,waterAllowed:true};
}
