import {terrainHeightAt} from './terrain-geometry.mjs';
export function waterVertexLevel(x,z,height,water,seed,seaFallback=true,offset=.12) {
  let total=0,count=0;
  for(const [dx,dz] of [[-1,-1],[0,-1],[-1,0],[0,0]]) {
    const px=x-.5+dx,pz=z-.5+dz,level=water(px,pz);
    if(Number.isFinite(level)&&height(px,pz)<level){total+=level;count++;}
  }
  if(count)return Math.fround(total/count-offset);
  return seaFallback&&terrainHeightAt(x,z,seed,height)<-offset?Math.fround(-offset):-Infinity;
}
export function clipShore(vertices,upper=Infinity) {
  const out=[];
  for(let i=0;i<vertices.length;i++) {
    const a=vertices[i],b=vertices[(i+1)%vertices.length];
    const av=Number.isFinite(upper)?upper-a.depth:a.depth,bv=Number.isFinite(upper)?upper-b.depth:b.depth;
    if(av>=0)out.push(a);
    if((av>=0)!==(bv>=0)) {
      const t=av/(av-bv);
      out.push({x:a.x+(b.x-a.x)*t,y:a.y+(b.y-a.y)*t,z:a.z+(b.z-a.z)*t,depth:Number.isFinite(upper)?upper:0});
    }
  }
  return out;
}

export function iceVertexLevel(x,z,ice) {
  let sum=0,count=0;
  for(const [dx,dz] of [[-1,-1],[0,-1],[-1,0],[0,0]]) {
    const value=ice(x-.5+dx,z-.5+dz);
    if(Number.isFinite(value)){sum+=value;count++;}
  }
  return count?sum/count:-Infinity;
}
export function iceVertexCoverage(x,z,coverage) {
  let sum=0;
  for(const [dx,dz] of [[-1,-1],[0,-1],[-1,0],[0,0]])sum+=coverage(x-.5+dx,z-.5+dz);
  return sum*.25;
}
export function clipIce(vertices,field) {
  const out=[],threshold=field==='coverage'?.5:0;
  for(let i=0;i<vertices.length;i++) {
    const a=vertices[i],b=vertices[(i+1)%vertices.length],av=a[field]-threshold,bv=b[field]-threshold;
    if(av>=0)out.push(a);
    if((av>=0)!==(bv>=0)) {
      const t=av/(av-bv),point={};
      for(const key of ['x','y','z','depth','coverage','bottom'])point[key]=a[key]+(b[key]-a[key])*t;
      out.push(point);
    }
  }
  return out;
}
