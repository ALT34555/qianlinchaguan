import {drainSteps} from './generation-steps.mjs';
const clamp=v=>Math.max(0,Math.min(1,v));
const DIRS=[[-1,-1],[0,-1],[1,-1],[-1,0],[1,0],[-1,1],[0,1],[1,1]];
class Queue {
  items=[];
  push(value){const a=this.items;let i=a.length;a.push(value);while(i){const p=(i-1)>>1;if(a[p].rank<=value.rank)break;a[i]=a[p];i=p;}a[i]=value;}
  pop(){const a=this.items,first=a[0],last=a.pop();if(a.length){let i=0;while(i*2+1<a.length){let c=i*2+1;if(c+1<a.length&&a[c+1].rank<a[c].rank)c++;if(a[c].rank>=last.rank)break;a[i]=a[c];i=c;}a[i]=last;}return first;}
}
// 地形选路，限制累计切坡
export function terrainTributaryPath(...args){return drainSteps(terrainTributaryPathSteps(...args));}
export function* terrainTributaryPathSteps(source,target,sample,seed,attempt=0){
  const length=Math.hypot(target.x-source.x,target.z-source.z),step=length>6000?(attempt===2?16:32):attempt===2?8:16;
  if(length<64)return null;
  const corridor=Math.min(640,Math.max(128,length*(.1+attempt*.06))),cutLimit=3;
  const originX=Math.round(source.x/step)*step,originZ=Math.round(source.z/step)*step;
  const dx=target.x-source.x,dz=target.z-source.z,start=sample(source.x,source.z),floor=target.level;
  if(!start||start.kind||start.height-.12<=floor)return null;
  if(!sample(target.x,target.z))return null;
  const queue=new Queue(),nodes=new Map(),samples=new Map(),closed=new Set(),startNode={ix:0,iz:0,x:source.x,z:source.z,level:start.height-.12,cost:0,parent:null,rank:length};
  nodes.set('0,0',startNode);queue.push(startNode);let visits=0;
  const terrain=(x,z)=>{const key=x+','+z;if(!samples.has(key))samples.set(key,sample(x,z));return samples.get(key);};
  while(queue.items.length&&visits<6000){
    yield;
    const node=queue.pop(),nodeKey=node.ix+','+node.iz;if(nodes.get(nodeKey)!==node||closed.has(nodeKey))continue;closed.add(nodeKey);visits++;
    if(Math.hypot(node.x-target.x,node.z-target.z)<=step*1.5){
      const path=[{x:target.x,z:target.z,level:target.level,depth:target.depth}];
      for(let p=node;p;p=p.parent)path.push({x:p.x,z:p.z});return path.reverse();
    }
    for(const [ox,oz] of DIRS){
      const ix=node.ix+ox,iz=node.iz+oz,x=originX+ix*step,z=originZ+iz*step;
      if(closed.has(ix+','+iz))continue;
      const t=((x-source.x)*dx+(z-source.z)*dz)/(length*length),side=Math.abs((x-source.x)*dz-(z-source.z)*dx)/length;
      if(t<-.1||t>1.1||side>corridor)continue;
      const s=terrain(x,z);if(!s||s.kind||s.height-.12<floor)continue;
      const level=Math.min(node.level,s.height-.12),cut=s.height-.12-level;
      if(cut>cutLimit)continue;
      const distance=Math.hypot(x-node.x,z-node.z),wave=Math.sin(x/83+seed%997)*Math.sin(z/107+seed%613);
      const cost=node.cost+distance*(1+cut*4+side/corridor*.15+wave*.06)+(node.level-level)*24;
      const key=ix+','+iz,previous=nodes.get(key);if(previous&&previous.cost<=cost)continue;
      const next={ix,iz,x,z,level,cost,parent:node,rank:cost+Math.hypot(x-target.x,z-target.z)*2.5};nodes.set(key,next);queue.push(next);
    }
  }
  return null;
}
export function smoothTributaryPath(points){
  const out=[points[0]];
  for(let k=1;k<points.length-1;k++){
    const a=points[k-1],b=points[k],c=points[k+1];out.push({x:(a.x+b.x*2+c.x)/4,z:(a.z+b.z*2+c.z)/4});
  }
  out.push(points.at(-1));return out;
}
// 水位沿程不升，深度仍受河宽约束
export function terrainTributaryProfile(...args){return drainSteps(terrainTributaryProfileSteps(...args));}
export function* terrainTributaryProfileSteps(points,source,sample){
  if(!points||points.length<2)return null;
  const lengths=[0];for(let k=1;k<points.length;k++)lengths.push(lengths[k-1]+Math.hypot(points[k].x-points[k-1].x,points[k].z-points[k-1].z));
  const total=lengths.at(-1),out=[],end=points.at(-1),start=sample(points[0].x,points[0].z);let level=start?.height-.12,j=1;
  if(!Number.isFinite(level)||level<=end.level||total<64||total>32768)return null;
  for(let distance=0;distance<total+4;distance+=4){
    yield;
    const d=Math.min(total,distance);while(j<points.length-1&&lengths[j]<d)j++;
    const a=points[j-1],b=points[j],t=clamp((d-lengths[j-1])/(lengths[j]-lengths[j-1])),x=a.x+(b.x-a.x)*t,z=a.z+(b.z-a.z)*t,s=sample(x,z);
    if(!s||s.kind||s.height-.08<end.level)return null;
    level=Math.max(end.level,Math.min(level,s.height-.12));
    if(s.height-level>3.2)return null;
    const halfWidth=Math.max(source.minHalfWidth,source.halfWidth*(.3+.7*d/total)),depth=Math.min(source.depth,halfWidth*2*.22,2.5);
    out.push({x,z,level:d===total?end.level:level,halfWidth,bedHeight:(d===total?end.level:level)-depth,depth});
    if(d===total)break;
  }
  return out;
}
