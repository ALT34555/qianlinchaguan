export function polygonArea(poly) {
  let area=0;
  for(let i=0;i<poly.length;i++){const a=poly[i],b=poly[(i+1)%poly.length];area+=a.x*b.z-b.x*a.z;}
  return Math.abs(area)*.5;
}
function clip(poly,key,edge,sign) {
  const out=[];
  for(let i=0;i<poly.length;i++) {
    const a=poly[i],b=poly[(i+1)%poly.length],da=(a[key]-edge)*sign,db=(b[key]-edge)*sign;
    if(da>=0)out.push(a);
    if((da>=0)!==(db>=0)){const t=da/(da-db);out.push({x:a.x+(b.x-a.x)*t,z:a.z+(b.z-a.z)*t});}
  }
  return out;
}
export function rasterPolygon(target,poly,bounds,side,scale=1) {
  if(poly.length<3)return;
  const sx=(bounds.maxX-bounds.minX)/side,sz=(bounds.maxZ-bounds.minZ)/side;
  const x0=Math.max(0,Math.floor((Math.min(...poly.map(p=>p.x))-bounds.minX)/sx));
  const x1=Math.min(side-1,Math.floor((Math.max(...poly.map(p=>p.x))-bounds.minX)/sx));
  const z0=Math.max(0,Math.floor((Math.min(...poly.map(p=>p.z))-bounds.minZ)/sz));
  const z1=Math.min(side-1,Math.floor((Math.max(...poly.map(p=>p.z))-bounds.minZ)/sz));
  for(let z=z0;z<=z1;z++)for(let x=x0;x<=x1;x++) {
    let piece=clip(poly,'x',bounds.minX+x*sx,1);piece=clip(piece,'x',bounds.minX+(x+1)*sx,-1);
    piece=clip(piece,'z',bounds.minZ+z*sz,1);piece=clip(piece,'z',bounds.minZ+(z+1)*sz,-1);
    target[z*side+x]+=polygonArea(piece)/(sx*sz)*scale;
  }
}
export function reduceAxisEdges(groups,emit) {
  for(const group of groups.values()) {
    const events=[];
    for(const e of group){const key=e.axis==='x'?'z':'x',a=e.a[key],b=e.c[key];e.key=key;e.sign=b>a?1:-1;
      events.push({at:Math.min(a,b),add:true,e},{at:Math.max(a,b),add:false,e});}
    events.sort((a,b)=>a.at-b.at);
    const active=new Set();let level=0,previous=events[0]?.at,index=0;
    while(index<events.length) {
      const at=events[index].at;
      if(level&&at>previous+1e-9) {
        const edge=[...active].find(e=>e.sign===Math.sign(level));
        if(edge){const point=value=>{const t=(value-edge.a[edge.key])/(edge.c[edge.key]-edge.a[edge.key]);
          return {x:edge.a.x+(edge.c.x-edge.a.x)*t,z:edge.a.z+(edge.c.z-edge.a.z)*t,depth:edge.a.depth+(edge.c.depth-edge.a.depth)*t};};
          const a=point(level>0?previous:at),c=point(level>0?at:previous);emit({...edge,a,c,isShore:a.depth===0&&c.depth===0});}
      }
      while(index<events.length&&Math.abs(events[index].at-at)<1e-9){const event=events[index++];if(event.add){active.add(event.e);level+=event.e.sign;}else{active.delete(event.e);level-=event.e.sign;}}
      previous=at;
    }
  }
}

export function intersectPolygons(subject,other) {
  let poly=subject;
  let area=0;for(let i=0;i<other.length;i++){const a=other[i],b=other[(i+1)%other.length];area+=a.x*b.z-b.x*a.z;}
  if(Math.abs(area)<1e-12||subject.length<3||other.length<3)return [];
  const sign=area>=0?1:-1;
  for(let i=0;i<other.length&&poly.length;i++) {
    const a=other[i],b=other[(i+1)%other.length],out=[];
    const distance=p=>((b.x-a.x)*(p.z-a.z)-(b.z-a.z)*(p.x-a.x))*sign;
    for(let j=0;j<poly.length;j++) {
      const p=poly[j],q=poly[(j+1)%poly.length],dp=distance(p),dq=distance(q);
      if(dp>=0)out.push(p);
      if((dp>=0)!==(dq>=0)){const t=dp/(dp-dq);out.push({x:p.x+(q.x-p.x)*t,z:p.z+(q.z-p.z)*t});}
    }
    poly=out;
  }
  return poly;
}
