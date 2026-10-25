const clamp=value=>Math.max(0,Math.min(1,value));
const lerp=(a,b,t)=>a+(b-a)*t;

export function vectorChannelSection(distance,radius,bed,level,natural){
  const width=Math.max(1e-6,radius),r=distance/width;
  if(r>=1)return null;
  const weight=(1-r*r)**2,height=natural-Math.max(0,natural-bed)*weight;
  return {height,wet:height<level};
}

function defaultProfile({seg,t}){
  const {edge,a,b}=seg;
  const left=lerp(a.leftWidth??a.halfWidth??edge.halfWidth,b.leftWidth??b.halfWidth??edge.halfWidth,t);
  const right=lerp(a.rightWidth??a.halfWidth??edge.halfWidth,b.rightWidth??b.halfWidth??edge.halfWidth,t);
  return {left,right,level:lerp(a.level,b.level,t),bed:lerp(a.bedHeight,b.bedHeight,t),priority:edge.priority??0};
}

export function indexVectorEdge(bins,edge,binSize=512){
  if(!edge?.id||!Array.isArray(edge.points)||edge.points.length<2)throw Error('矢量河道缺少线段');
  for(let k=1;k<edge.points.length;k++){
    const a=edge.points[k-1],b=edge.points[k],vx=b.x-a.x,vz=b.z-a.z,length=Math.hypot(vx,vz);
    if(!Number.isFinite(length)||!length)continue;
    const halfWidth=Math.max(a.leftWidth??a.halfWidth??edge.halfWidth,a.rightWidth??a.halfWidth??edge.halfWidth,b.leftWidth??b.halfWidth??edge.halfWidth,b.rightWidth??b.halfWidth??edge.halfWidth);
    if(!Number.isFinite(halfWidth)||halfWidth<=0)throw Error('矢量河道宽度无效');
    const pad=edge.sourceType==='source'?Math.max(32,halfWidth*3+20):Math.max(160,halfWidth*2+74),segment={edge,a,b,vx,vz,length,length2:length*length,depth:edge.depth,pointIndex:k-1,pad};
    for(let z=Math.floor((Math.min(a.z,b.z)-pad)/binSize);z<=Math.floor((Math.max(a.z,b.z)+pad)/binSize);z++)for(let x=Math.floor((Math.min(a.x,b.x)-pad)/binSize);x<=Math.floor((Math.max(a.x,b.x)+pad)/binSize);x++){
      const key=x+','+z;if(!bins.has(key))bins.set(key,[]);bins.get(key).push(segment);
    }
  }
}

export function queryVectorSegments(bins,x0,z0,chunkSize=64,binSize=512){
  const segments=new Set();
  for(let z=Math.floor((z0-chunkSize)/binSize);z<=Math.floor((z0+chunkSize*2)/binSize);z++)for(let x=Math.floor((x0-chunkSize)/binSize);x<=Math.floor((x0+chunkSize*2)/binSize);x++)for(const seg of bins.get(x+','+z)??[]){
    if(Math.max(seg.a.x,seg.b.x)+seg.pad<x0||Math.min(seg.a.x,seg.b.x)-seg.pad>x0+chunkSize||Math.max(seg.a.z,seg.b.z)+seg.pad<z0||Math.min(seg.a.z,seg.b.z)-seg.pad>z0+chunkSize)continue;
    segments.add(seg);
  }
  return [...segments];
}

export function carveVectorSegments(surface,point,segments,profile=defaultProfile){
  const reaches=new Map(),candidates=[],natural=surface.height;
  for(const seg of segments){
    const t=clamp(((point.x-seg.a.x)*seg.vx+(point.z-seg.a.z)*seg.vz)/seg.length2);
    const x=seg.a.x+seg.vx*t,z=seg.a.z+seg.vz*t,d=Math.hypot(point.x-x,point.z-z);
    const previous=reaches.get(seg.edge.id);
    if(!previous||d<previous.d)reaches.set(seg.edge.id,{seg,t,x,z,d});
  }
  let bed=natural,bank=-Infinity,wetCut=false,template=null,protectedBed=null;
  for(const reach of reaches.values()){
    const {seg,t,x,z,d}=reach,shape=profile(reach);
    if(![shape.left,shape.right,shape.level,shape.bed,shape.priority].every(Number.isFinite)||shape.left<=0||shape.right<=0)throw Error('矢量河道剖面无效');
    const offset=(point.x-x)*(-seg.vz/seg.length)+(point.z-z)*seg.vx/seg.length;
    const radius=offset>=0?shape.left:shape.right,section=shape.section??vectorChannelSection(d,radius,shape.bed,shape.level,natural);
    if(!section)continue;
    if(shape.templateTerrain){
      const distance=d/shape.valleyRadius;
      if(!template||distance<template.distance||distance===template.distance&&seg.edge.id<template.id)template={distance,id:seg.edge.id,height:section.height};
    }
    if(shape.protectBed&&section.wet&&d<radius)protectedBed=protectedBed===null?Math.min(natural,section.height):Math.min(protectedBed,section.height);
    if(section.bank)bank=Math.max(bank,section.height);else bed=Math.min(bed,section.height);
    if(section.wet){bed=Math.min(bed,section.height);wetCut=true;}
    if(shape.waterAllowed!==false&&(!shape.decorative||section.wet))candidates.push({...reach,shape,height:section.height});
  }
  bed=protectedBed??template?.height??(wetCut?bed:Math.max(bed,bank));surface.height=bed;
  let winner=null;
  for(const candidate of candidates){
    if(protectedBed!==null&&candidate.shape.templateTerrain)continue;
    if(!(bed<candidate.shape.level))continue;
    if(!winner||candidate.shape.priority>winner.shape.priority||candidate.shape.priority===winner.shape.priority&&candidate.d<winner.d)winner=candidate;
  }
  return {surface,winner};
}
