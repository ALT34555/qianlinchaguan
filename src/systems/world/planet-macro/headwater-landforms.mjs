import {cellAt,sphereFrame} from './sphere-frame.mjs';
import {isRiverHead} from './river-headwaters.mjs';
const caches=new WeakMap();
const clamp=v=>Math.max(0,Math.min(1,v)),smooth=v=>{const t=clamp(v);return t*t*(3-2*t);};

export function headwaterLandform(world,cell){
  let cache=caches.get(world);if(!cache){cache=new Map();caches.set(world,cache);}
  if(cache.has(cell))return cache.get(cell);
  let form=null;
  if(world.water[cell]!==1&&world.water[cell]!==2&&isRiverHead(world,cell)){
    const frame=sphereFrame(world,cell),target=sphereFrame(world,world.receiver[cell]);
    const direction=frame.local(target.lon*world.radius,-target.lat*world.radius),length=Math.hypot(direction.x,direction.z);
    const radius=Math.min(1800,frame.size*.32),angle=(world.options.seed^Math.imul(cell,374761393))>>>0;
    form={frame,dx:direction.x/length,dz:direction.z/length,radius,amplitude:8+angle%9,phase:angle%628/100};
  }
  cache.set(cell,form);if(cache.size>4096)cache.delete(cache.keys().next().value);return form;
}

// 上游低丘环抱，中心和下游留白
export function headwaterRelief(world,wx,wz){
  const cell=cellAt(world,wx,wz),form=headwaterLandform(world,cell);if(!form)return 0;
  const p=form.frame.local(wx,wz),u=-(p.x*form.dx+p.z*form.dz),v=-p.x*form.dz+p.z*form.dx;
  const r=Math.hypot(u,v)/form.radius;if(r>=1)return 0;
  const col=cell%world.columns,row=Math.floor(cell/world.columns);
  const tx=(wx/world.circumference+.5)*world.columns-col,tz=(wz/world.circumference+.25)*world.columns-row;
  const border=smooth(Math.min(tx,1-tx,tz,1-tz)/.12);
  const upstream=smooth((u/form.radius+.12)/.4),outer=1-smooth((r-.65)/.35);
  const rise=smooth(r/.65),fold=.72+.28*Math.cos(v/form.radius*9+form.phase)*Math.sin(u/form.radius*5);
  return form.amplitude*rise*outer*upstream*fold*border;
}

// 上游模板源点
export function headwaterSource(form,k,attempt=0){
  const angle=[-.75,.7,0][k%3]+(attempt-2.5)*.08;
  const distance=form.radius*(.28+.055*attempt),u=Math.cos(angle)*distance,v=Math.sin(angle)*distance;
  return {x:-form.dx*u-form.dz*v,z:-form.dz*u+form.dx*v};
}
