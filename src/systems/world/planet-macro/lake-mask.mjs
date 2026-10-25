const SIZE=32,HALO=2,SIDE=SIZE+HALO*2+1,LIMIT=128,DIAGONAL=Math.SQRT2;

// 共享湖群坐标网格，边缘留重叠
function bake(group,tx,tz,spacing){
  const start=performance.now(),ox=(tx*SIZE-HALO)*spacing,oz=(tz*SIZE-HALO)*spacing,inside=new Uint8Array(SIDE*SIDE);
  for(let z=0;z<SIDE;z++){
    const y=oz+z*spacing,crossings=[];
    for(const ring of group.rings)for(let j=0;j<ring.length;j++){
      const a=ring[j],b=ring[(j+1)%ring.length];
      if((a.z>y)!==(b.z>y))crossings.push(a.x+(b.x-a.x)*(y-a.z)/(b.z-a.z));
    }
    crossings.sort((a,b)=>a-b);let cursor=0;
    for(let x=0;x<SIDE;x++){const px=ox+x*spacing;while(cursor<crossings.length&&crossings[cursor]<=px)cursor++;inside[z*SIDE+x]=cursor%2;}
  }
  const distance=new Float32Array(inside.length).fill(Infinity);
  for(let z=0;z<SIDE;z++)for(let x=0;x<SIDE;x++){
    const i=z*SIDE+x,value=inside[i];
    if(x&&inside[i-1]!==value||x<SIDE-1&&inside[i+1]!==value||z&&inside[i-SIDE]!==value||z<SIDE-1&&inside[i+SIDE]!==value)distance[i]=.5;
  }
  for(let z=0;z<SIDE;z++)for(let x=0;x<SIDE;x++){
    const i=z*SIDE+x;let d=distance[i];
    if(x)d=Math.min(d,distance[i-1]+1);
    if(z){d=Math.min(d,distance[i-SIDE]+1);if(x)d=Math.min(d,distance[i-SIDE-1]+DIAGONAL);if(x<SIDE-1)d=Math.min(d,distance[i-SIDE+1]+DIAGONAL);}
    distance[i]=d;
  }
  for(let z=SIDE-1;z>=0;z--)for(let x=SIDE-1;x>=0;x--){
    const i=z*SIDE+x;let d=distance[i];
    if(x<SIDE-1)d=Math.min(d,distance[i+1]+1);
    if(z<SIDE-1){d=Math.min(d,distance[i+SIDE]+1);if(x)d=Math.min(d,distance[i+SIDE-1]+DIAGONAL);if(x<SIDE-1)d=Math.min(d,distance[i+SIDE+1]+DIAGONAL);}
    distance[i]=d;
  }
  for(let i=0;i<distance.length;i++)distance[i]=(inside[i]?1:-1)*Math.min(3,distance[i])*spacing;
  group.maskStats.builds++;group.maskStats.bakeMs+=performance.now()-start;
  return {ox,oz,distance};
}
export function lakeMaskDistance(group,wx,wz){
  const p=group.frame.local(wx,wz),bounds=group.bounds;
  if(p.x<bounds.minX||p.x>bounds.maxX||p.z<bounds.minZ||p.z>bounds.maxZ)return -Infinity;
  group.masks??=new Map();group.maskStats??={builds:0,hits:0,bakeMs:0,bytes:0,limit:LIMIT,divisions:SIZE};
  const size=group.frame.size,spacing=size/SIZE,tx=Math.floor(p.x/size),tz=Math.floor(p.z/size),key=tx+','+tz;
  let tile=group.masks.get(key);
  if(tile){group.maskStats.hits++;group.masks.delete(key);group.masks.set(key,tile);}
  else{
    tile=bake(group,tx,tz,spacing);group.masks.set(key,tile);
    if(group.masks.size>LIMIT)group.masks.delete(group.masks.keys().next().value);
    group.maskStats.bytes=group.masks.size*SIDE*SIDE*4;
  }
  const gx=(p.x-tile.ox)/spacing,gz=(p.z-tile.oz)/spacing,x=Math.floor(gx),z=Math.floor(gz),u=gx-x,v=gz-z,i=z*SIDE+x,d=tile.distance;
  return (d[i]*(1-u)+d[i+1]*u)*(1-v)+(d[i+SIDE]*(1-u)+d[i+SIDE+1]*u)*v;
}
