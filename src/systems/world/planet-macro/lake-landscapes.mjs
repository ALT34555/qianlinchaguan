
const TAU=Math.PI*2;
const clamp=(v,a=0,b=1)=>Math.max(a,Math.min(b,v));
const smooth=v=>{const t=clamp(v);return t*t*(3-2*t);};
const mix=(a,b,t)=>a+(b-a)*t;
const delta=(w,x)=>x-Math.round(x/w.circumference)*w.circumference;
function variation(w,x,z,scale=1){
  const lon=x/w.circumference*TAU,lat=-z/w.circumference*TAU,r=w.radius,seed=(w.options.seed%997)*.013;
  const a=Math.cos(lat)*Math.cos(lon)*r,b=Math.sin(lat)*r,c=Math.cos(lat)*Math.sin(lon)*r;
  return .5+.25*Math.sin((a/93+b/131)/scale+seed)+.25*Math.sin((c/157-b/107)/scale-seed);
}
export {variation as lakeShoreVariation};
export function lakeShoreProfile(height,level,slope,salinity,patch){
  if(height<=level)return height;
  const h=height-level,band=1-smooth(Math.abs(h)/6),gentle=1-smooth((slope-.035)/.16);
  const strength=salinity==='salt'?.56:.32;
  const terrace=(salinity==='salt'?1.1:.65)+patch*.7;
  const terraceWeight=Math.exp(-(((Math.abs(h)-terrace)/.65)**2))*band*gentle;
  const magnitude=Math.abs(h)*(1-strength*band*gentle*(.65+.35*patch));
  const shaped=mix(magnitude,terrace*.65,terraceWeight*.3);
  return level+Math.sign(h)*shaped;
}
export function applyLakeOutlet(w,surface,x,z,outlet){
  if(!outlet)return surface;
  const {pond,cos,points}=outlet;
  if(Math.hypot(delta(w,x-pond.wx)*cos,z-pond.wz)>150)return surface;
  let best=null;
  for(let k=1;k<points.length;k++){
    const a=points[k-1],b=points[k],vx=delta(w,b.wx-a.wx)*cos,vz=b.wz-a.wz,px=delta(w,x-a.wx)*cos,pz=z-a.wz;
    const t=clamp((px*vx+pz*vz)/(vx*vx+vz*vz)),distance=Math.hypot(px-vx*t,pz-vz*t);
    if(!best||distance<best.distance)best={distance,t,a,b,vx,vz};
  }
  const natural=surface.height;
  if(best){
    const {a,b,t,distance,vx,vz}=best,width=mix(a.halfWidth,b.halfWidth,t),level=mix(a.level,b.level,t),bed=level-.45;
    const radius=width*2.5+3,weight=1-smooth((distance-width*.6)/(radius-width*.6));
    if(distance<radius){
      surface.height=Math.min(surface.height,mix(natural,bed,weight));
      if(surface.height<level){
        // 湖面交界保留同一水位
        surface.water=surface.kind===2?Math.max(surface.water,level):level;
        if(surface.kind!==2){surface.kind=3;surface.river={id:outlet.id,cell:outlet.cell,receiver:-1,source:'source',dischargeRole:'decorative-estimate',dischargeM3s:.4,widthM:width*2,directionEast:vx/Math.hypot(vx,vz),directionNorth:-vz/Math.hypot(vx,vz),waterfallDropM:outlet.fall&&Math.abs(b.level-a.level)>.02?outlet.drop:0};}
        surface.salinity=outlet.salinity;surface.saltLakeId=outlet.salinity==='salt'?outlet.lakeId:null;surface.lakeOutlet=outlet.lakeId;
      }
    }
  }
  const dx=delta(w,x-pond.wx)*cos,dz=z-pond.wz,r=Math.hypot(dx/pond.radiusX,dz/pond.radiusZ),angle=Math.atan2(dz,dx),edge=1+.08*Math.sin(angle*3+outlet.lakeId);
  const radius=r/edge;
  if(radius<1.4){
    const floor=pond.level-pond.depth,weight=1-smooth((radius-.35)/1.05);
    surface.height=Math.min(surface.height,mix(natural,floor,weight));
    const level=surface.lakeOutlet!==undefined&&Number.isFinite(surface.water)?mix(surface.water,pond.level,weight):pond.level;
    if(surface.height<level){surface.water=level;if(radius<.35||!surface.river){surface.kind=4;surface.river=null;}surface.salinity=outlet.salinity;surface.saltLakeId=outlet.salinity==='salt'?outlet.lakeId:null;surface.lakeOutlet=outlet.lakeId;}
  }
  return surface;
}
export function lakeLandscapeStats(){return {searches:0,probes:0,gradientNodes:0};}
