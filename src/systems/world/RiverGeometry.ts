/** 几何导线以区块为单位 */
export interface RiverGuideGraph {
  upstream(x:number,z:number):readonly [number,number]|null;
  downstream(x:number,z:number):readonly [number,number]|null;
}
export interface RiverGuide {x:number;z:number;tx:number;tz:number}
/** 节点仅在所属区块中央微调。 */
export const MAX_RIVER_BEND=16;
export const RIVER_NODE_NOISE=3;
export const RIVER_BANK_BLEND=8;

/** 直角转弯平滑过渡，锚点最多内移12格。 */
export function riverGuide(x:number,z:number,graph:RiverGuideGraph):RiverGuide{
  const a=graph.upstream(x,z),b=graph.downstream(x,z);
  const incoming=a?Math.hypot(x-a[0],z-a[1]):1,outgoing=b?Math.hypot(b[0]-x,b[1]-z):1;
  const ix=a?(x-a[0])/incoming:0,iz=a?(z-a[1])/incoming:0;
  const ox=b?(b[0]-x)/outgoing:0,oz=b?(b[1]-z)/outgoing:0;
  const tx=ix+ox,tz=iz+oz;
  const length=Math.hypot(tx,tz)||1;
  const bend=a&&b?Math.hypot(ox-ix,oz-iz):0,scale=bend?Math.min(12,12*bend)/bend/64:0;
  return {x:x+(ox-ix)*scale,z:z+(oz-iz)*scale,tx:tx/length,tz:tz/length};
}

interface Point {x:number;z:number}
/** 分段保留在所属方格，穿过共享角点。 */
export function riverCurve(start:Point,end:Point,ta:Point,tb:Point,t:number,size:number):Point{
  const ax=Math.floor(start.x/size),az=Math.floor(start.z/size),bx=Math.floor(end.x/size),bz=Math.floor(end.z/size);
  const dx=Math.sign(bx-ax),dz=Math.sign(bz-az),length=Math.hypot(dx,dz)||1;
  const boundary={x:dx?(dx>0?ax+1:ax)*size:(start.x+end.x)/2,
    z:dz?(dz>0?az+1:az)*size:(start.z+end.z)/2};
  const tangent={x:dx/length,z:dz/length};
  const first=t<=.5,a=first?start:boundary,b=first?boundary:end;
  const at=first?ta:tangent,bt=first?tangent:tb,u=first?t*2:(t-.5)*2,v=1-u;
  const control=Math.min(size*.23,Math.hypot(b.x-a.x,b.z-a.z)*.42);
  return {x:v**3*a.x+3*v*v*u*(a.x+at.x*control)+3*v*u*u*(b.x-bt.x*control)+u**3*b.x,
    z:v**3*a.z+3*v*v*u*(a.z+at.z*control)+3*v*u*u*(b.z-bt.z*control)+u**3*b.z};
}
