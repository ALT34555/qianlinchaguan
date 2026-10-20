import {sphereFrame,cellAt} from './sphere-frame.mjs';
import {sampleLakeShore} from './lake-shores.mjs';
import {sampleNaturalHeight} from './backend.mjs';
const caches=new WeakMap(),groups=new WeakMap(),LIMIT=128;
function random(seed){return()=>{let t=seed+=0x6d2b79f5;t=Math.imul(t^t>>>15,t|1);t^=t+Math.imul(t^t>>>7,t|61);return((t^t>>>14)>>>0)/4294967296;};}
function neighbor(w,cell,dx,dz){
  let x=cell%w.columns+dx,y=Math.floor(cell/w.columns)+dz;
  if(y<0){y=-y-1;x+=w.columns/2;}if(y>=w.rows){y=w.rows*2-y-1;x+=w.columns/2;}
  return y*w.columns+(x%w.columns+w.columns)%w.columns;
}
function lakeGroup(w,cell){
  let cache=groups.get(w);if(!cache){cache=new Map();groups.set(w,cache);}
  const id=w.lakeId[cell];if(cache.has(id))return cache.get(id);
  const members=new Set([cell]),queue=[cell],boundary=new Set();
  for(let k=0;k<queue.length;k++){
    const current=queue[k];
    for(let dz=-1;dz<=1;dz++)for(let dx=-1;dx<=1;dx++){
      if(!dx&&!dz)continue;const next=neighbor(w,current,dx,dz);
      if(w.water[next]===2&&w.lakeId[next]===id){if(!members.has(next)){members.add(next);queue.push(next);}}
      else boundary.add(current);
    }
  }
  const lake=w.lakes?.[id],level=lake?.level??w.spill[cell];
  const group={id,members,boundary,level,sink:0,salinity:lake?.salinity??'fresh',
    salinityInfo:{source:'macro-water-balance',closed:lake?.closed,hasOutlet:(lake?.outlet??-1)>=0,
      inflowM3s:lake?.inflowM3s,outflowM3s:lake?.outflowM3s}};
  cache.set(id,group);if(cache.size>32)cache.delete(cache.keys().next().value);return group;
}
export function getLakeRegion(w,cell,natural){
  let cache=caches.get(w);if(!cache){cache=new Map();caches.set(w,cache);}
  if(cache.has(cell)){const model=cache.get(cell);cache.delete(cell);cache.set(cell,model);return model;}
  const start=performance.now(),group=lakeGroup(w,cell),frame=sphereFrame(w,cell);
  const interior=!group.boundary.has(cell),nodes=[],rng=random(w.options.seed^Math.imul(cell+1,0x315acf73));
  const count=interior?0:Math.max(8,Math.min(12,Math.floor(w.rainfall[cell]/400)*3));
  for(let k=0;k<count;k++)for(let attempt=0;attempt<8;attempt++){
    const x=(rng()-.5)*frame.size*.84,z=(rng()-.5)*frame.size*.84,g=frame.coordinates(x,z);
    if(cellAt(w,g.wx,g.wz)!==cell||nodes.some(p=>Math.hypot(p.x-x,p.z-z)<frame.size*.07))continue;
    const s=natural(g.wx,g.wz);if(s.kind===1||s.height<0)continue;
    const shore=sampleLakeShore(w,g.wx,g.wz,s.height,null,null,sampleNaturalHeight),lakeMember=shore?.kind===2;
    nodes.push({id:'lake-source:'+cell+':'+k,x,z,naturalHeight:s.height,level:(shore?.height??s.height)-.8,
      weight:.5+rng(),chunkType:lakeMember?2:22,connected:false,lakeMember,attempts:0,dischargeM3s:0});break;
  }
  const model={world:w,cell,frame,nodes,sources:nodes,edges:[],group,interior,level:group.level,elapsed:performance.now()-start};
  cache.set(cell,model);if(cache.size>LIMIT)cache.delete(cache.keys().next().value);return model;
}
export function sampleLakeRegion(model,wx,wz,naturalHeight){
  return sampleLakeShore(model.world,wx,wz,naturalHeight,null,null,sampleNaturalHeight)??{height:naturalHeight,water:-Infinity,kind:0,salinity:null,saltLakeId:null};
}
