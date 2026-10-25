const caches=new WeakMap();

// 仅查询附近绿洲，不改变降水
export function oasisMoisture(w,wx,wz){
  if(!w.oases?.length)return 0;
  let cache=caches.get(w);
  if(!cache){
    cache=new Map();
    for(const oasis of w.oases){
      const x=oasis.cell%w.columns,y=Math.floor(oasis.cell/w.columns);
      const entry={...oasis,wx:((x+.5)/w.columns-.5)*w.circumference,wz:((y+.5)/w.rows*.5-.25)*w.circumference,radius:Math.sqrt(oasis.areaKm2/Math.PI)*1000};
      for(let dy=-1;dy<=1;dy++)for(let dx=-1;dx<=1;dx++){
        const row=y+dy;if(row<0||row>=w.rows)continue;
        const key=row*w.columns+(x+dx+w.columns)%w.columns;
        if(!cache.has(key))cache.set(key,[]);cache.get(key).push(entry);
      }
    }
    caches.set(w,cache);
  }
  const x=Math.floor((wx/w.circumference+.5)*w.columns),y=Math.floor((wz/w.circumference+.25)*w.columns);
  const items=cache.get(Math.max(0,Math.min(w.rows-1,y))*w.columns+(x%w.columns+w.columns)%w.columns);
  let value=0;
  for(const oasis of items??[]){
    let dx=wx-oasis.wx;dx-=Math.round(dx/w.circumference)*w.circumference;
    const distance=Math.hypot(dx*Math.cos(wz/w.circumference*Math.PI*2),wz-oasis.wz),t=Math.max(0,1-distance/Math.max(1,oasis.radius));
    value=Math.max(value,t*t*(3-2*t));
  }
  return value;
}
