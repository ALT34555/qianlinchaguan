const TAU=Math.PI*2;
export function cellAt(world,wx,wz){
  const x=Math.floor((wx/world.circumference+.5)*world.columns),y=Math.max(0,Math.min(world.rows-1,Math.floor((wz/world.circumference+.25)*world.columns)));
  return y*world.columns+((x%world.columns)+world.columns)%world.columns;
}
export function sphereFrame(world,cell){
  const lon=((cell%world.columns+.5)/world.columns-.5)*TAU,lat=(.25-(Math.floor(cell/world.columns)+.5)/world.rows*.5)*TAU;
  const anchor=[Math.cos(lat)*Math.cos(lon),Math.sin(lat),Math.cos(lat)*Math.sin(lon)],east=[-Math.sin(lon),0,Math.cos(lon)],south=[Math.sin(lat)*Math.cos(lon),-Math.cos(lat),Math.sin(lat)*Math.sin(lon)];
  const coordinates=(x,z)=>{const d=Math.hypot(x,z),a=d/world.radius,c=Math.cos(a),k=d?Math.sin(a)/d:0,p=anchor.map((v,i)=>v*c+(east[i]*x+south[i]*z)*k);
    return {wx:Math.atan2(p[2],p[0])/TAU*world.circumference,wz:-Math.asin(Math.max(-1,Math.min(1,p[1])))/TAU*world.circumference};};
  const local=(wx,wz)=>{const a=wx/world.circumference*TAU,b=-wz/world.circumference*TAU,p=[Math.cos(b)*Math.cos(a),Math.sin(b),Math.cos(b)*Math.sin(a)];
    const dot=v=>v.reduce((sum,c,i)=>sum+c*p[i],0),e=dot(east),s=dot(south),sin=Math.hypot(e,s),angle=Math.atan2(sin,dot(anchor)),k=sin?world.radius*angle/sin:world.radius;
    return {x:e*k,z:s*k};};
  return {cell,lon,lat,anchor,east,south,size:world.circumference/world.columns,coordinates,local};
}
