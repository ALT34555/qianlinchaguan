import {DECORATIVE_MAX_DEPTH} from './river-dimensions.mjs';
import {vectorChannelSection} from './vector-channel.mjs';
const clamp=v=>Math.max(0,Math.min(1,v));
// 沿支流前进，截到首个主干交点
export function firstMainIntersection(points,mainEdges){
  for(let j=1;j<points.length;j++){
    const a=points[j-1],b=points[j],vx=b.x-a.x,vz=b.z-a.z;let best=null;
    for(const edge of mainEdges)for(let k=1;k<edge.points.length;k++){
      const c=edge.points[k-1],d=edge.points[k];
      if(Math.max(a.x,b.x)+1e-5<Math.min(c.x,d.x)||Math.min(a.x,b.x)-1e-5>Math.max(c.x,d.x)||Math.max(a.z,b.z)+1e-5<Math.min(c.z,d.z)||Math.min(a.z,b.z)-1e-5>Math.max(c.z,d.z))continue;
      const ux=d.x-c.x,uz=d.z-c.z,den=vx*uz-vz*ux;if(Math.abs(den)<1e-9)continue;
      const px=c.x-a.x,pz=c.z-a.z,t=(px*uz-pz*ux)/den,u=(px*vz-pz*vx)/den;
      if(t< -1e-7||t>1+1e-7||u< -1e-7||u>1+1e-7||best&&t>=best.t)continue;
      const startDepth=c.depth??edge.depth,endDepth=d.depth??edge.depth;
      best={t:clamp(t),point:{x:a.x+vx*clamp(t),z:a.z+vz*clamp(t),level:c.level+(d.level-c.level)*clamp(u),depth:startDepth+(endDepth-startDepth)*clamp(u)},edgeId:edge.id};
    }
    if(best)return {points:[...points.slice(0,j),best.point],target:{...best.point,edgeId:best.edgeId}};
  }
  return null;
}
export function assignTributaryBed(points,startLevel,depth){
  const lengths=[0];for(let k=1;k<points.length;k++)lengths.push(lengths[k-1]+Math.hypot(points[k].x-points[k-1].x,points[k].z-points[k-1].z));
  const total=lengths.at(-1),endLevel=points.at(-1).level,waterDepth=Math.max(0,Math.min(DECORATIVE_MAX_DEPTH,depth));
  if(total<64||startLevel<=endLevel)return null;
  return points.map((point,k)=>{const t=lengths[k]/total,level=startLevel+(endLevel-startLevel)*t;return {...point,level,bedHeight:level-waterDepth};});
}
// 支流独立限深，主河覆盖汇流口
export function tributarySection(distance,radius,centerBed,level,natural){
  return vectorChannelSection(distance,radius,Math.max(centerBed,level-DECORATIVE_MAX_DEPTH),level,natural);
}
