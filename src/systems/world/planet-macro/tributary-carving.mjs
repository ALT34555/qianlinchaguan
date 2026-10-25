import {DECORATIVE_MAX_DEPTH} from './river-dimensions.mjs';
import {vectorChannelSection} from './vector-channel.mjs';
export {templateTributaryPath as decorativePath} from './tributary-templates.mjs';
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
      const startWidth=c.halfWidth??edge.halfWidth,endWidth=d.halfWidth??edge.halfWidth;
      best={t:clamp(t),point:{x:a.x+vx*clamp(t),z:a.z+vz*clamp(t),level:c.level+(d.level-c.level)*clamp(u),depth:startDepth+(endDepth-startDepth)*clamp(u),halfWidth:startWidth+(endWidth-startWidth)*clamp(u),sourceType:edge.sourceType},edgeId:edge.id};
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
// 装饰支流贴地浅切
export function tributaryProfile(_level,depth,natural,width=Infinity){
  const water=natural-.12;
  return {level:water,bed:water-Math.max(0,Math.min(DECORATIVE_MAX_DEPTH,depth,width*.22))};
}
export function tributarySection(distance,radius,centerBed,level,natural){
  const profile=tributaryProfile(level,level-centerBed,natural,radius*2);
  return vectorChannelSection(distance,radius,profile.bed,profile.level,natural);
}

// 浅谷承托窄槽，正式水位沿程固定
export function decorativeChannelProfile(distance,radius,depth,natural,level=null){
  const sink=level===null?Math.min(1.2,.2+radius*.25):Math.max(0,Math.min(3.08,natural-level-.12));
  const valleyRadius=Math.max(8,radius*3+4,sink*5),r=distance/valleyRadius;
  if(r>=1)return null;
  const ground=natural-sink*(1-r*r)**2;
  const profile=level===null?tributaryProfile(ground,depth,ground,radius*2):{level,bed:level-Math.max(0,Math.min(DECORATIVE_MAX_DEPTH,depth,radius*2*.22))};
  const core=distance/radius,raw=core<1?ground-Math.max(0,ground-profile.bed)*(1-core*core)**2:ground;
  const height=level===null?raw:Math.max(natural-3.2-(profile.level-profile.bed),raw);
  return {...profile,decorative:true,section:{height,wet:core<1&&height<profile.level}};
}
