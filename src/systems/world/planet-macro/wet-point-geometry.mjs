const clamp=(v,a=0,b=1)=>Math.max(a,Math.min(b,v));
const smooth=v=>{v=clamp(v);return v*v*(3-2*v);};
const mix=(a,b,t)=>a+(b-a)*t;
export function configureWetPoint(source,sample,rainfall,evaporation,salinity='fresh'){
  let best=null;
  for(let z=-16;z<=16;z+=8)for(let x=-16;x<=16;x+=8){
    const s=sample(source.x+x,source.z+z);if(!s||s.kind)continue;
    const score=s.height+Math.hypot(x,z)*.025;
    if(!best||score<best.score)best={x:source.x+x,z:source.z+z,height:s.height,score};
  }
  if(!best)return;
  source.x=best.x;source.z=best.z;
  const seed=source.x*.013+source.z*.017,style=rainfall/Math.max(150,evaporation)<.55?'oasis':'woodland';
  const yaw=Math.sin(seed)*Math.PI,rx=style==='oasis'?17:12,rz=rx*(.7+.08*Math.sin(seed+1));
  let rim=best.height,rimWet=false;
  for(let k=0;k<12;k++){
    const a=k*Math.PI/6,x=source.x+Math.cos(a)*rx*1.45,z=source.z+Math.sin(a)*rx*1.45,s=sample(x,z);
    if(!s)return;if(s.kind){rimWet=true;continue;}rim=Math.min(rim,s.height);
  }
  const level=Math.fround(Math.min(best.height-.2,rim-.12)),depth=style==='oasis'?1.35:1.65;
  source.pool={style,rx,rz,yaw,level,depth,salinity,dampOnly:rimWet||salinity==='salt'||best.height-level>2.5,phase:seed};
  source.level=level;source.chunkType=22;
}
export function applyWetPoints(surface,p,network){
  if(surface.kind===1||surface.kind===2||surface.river?.source==='macro')return surface;
  for(const source of network.sources){
    const pool=source.pool;if(source.connected||!pool)continue;
    const dx=p.x-source.x,dz=p.z-source.z,c=Math.cos(pool.yaw),s=Math.sin(pool.yaw),x=dx*c+dz*s,z=-dx*s+dz*c;
    const angle=Math.atan2(z,x),r=Math.hypot(x/pool.rx,z/pool.rz)/(1+.11*Math.sin(angle*3+pool.phase)+.05*Math.cos(angle*5));
    if(r>=1.65)continue;
    const damp=1-smooth((r-.75)/.9);
    surface.wetPoint={style:pool.style,damp,salinity:pool.salinity,underwater:false};
    surface.springDamp=Math.max(surface.springDamp??0,damp);
    if(pool.dampOnly||r>=1.3)continue;
    const natural=surface.height,bed=pool.level-pool.depth*(1-smooth(r/.98)),weight=1-smooth((r-.7)/.6);
    surface.height=Math.min(natural,mix(natural,bed,weight));
    if(surface.height<pool.level){
      surface.water=Math.max(surface.water,pool.level);surface.kind=4;surface.salinity=pool.salinity;
      surface.wetPoint.underwater=true;surface.wetland=Math.max(surface.wetland??0,pool.style==='woodland'?.3:.1);
    }
  }
  return surface;
}
export function wetPointRocks(source){
  const p=source.pool;if(source.connected||!p||p.dampOnly)return [];
  const out=[],count=p.style==='oasis'?3:5;
  for(let k=0;k<count;k++){
    const a=p.phase+k*2.4,r=.85+(k%2)*.3,x=Math.cos(a)*p.rx*r,z=Math.sin(a)*p.rz*r,c=Math.cos(p.yaw),s=Math.sin(p.yaw);
    out.push({x:source.x+x*c-z*s,z:source.z+x*s+z*c,id:k%2?'rock.slab':'rock.boulder',scale:.65+(k%3)*.4,yaw:a+p.yaw,form:k%2?'buried':'outcrop'});
  }
  return out;
}
