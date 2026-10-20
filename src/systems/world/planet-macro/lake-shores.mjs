import {Heap} from './heap.mjs';
const states=new WeakMap(),asyncModes=new WeakMap(),SIDE=32,NODES=SIDE*SIDE;
const PROBE_AFTER=500000,PROBE_LIMIT=65536,PROBE_BIAS=.001;
class ShoreHeap extends Heap{
  constructor(rank){super();this.rank=rank;}
  before(a,va,b,vb){
    if(va!==vb)return va<vb;
    const ra=this.rank(a),rb=this.rank(b);
    return ra<rb||ra===rb&&a<b;
  }
  push(id,value){
    if(this.size===this.ids.length){
      const ids=new Uint32Array(this.size*2),values=new Float64Array(this.size*2);
      ids.set(this.ids);values.set(this.values);this.ids=ids;this.values=values;
    }
    const {ids,values}=this;let i=this.size++;
    while(i){const p=(i-1)>>1;if(!this.before(id,value,ids[p],values[p]))break;ids[i]=ids[p];values[i]=values[p];i=p;}
    ids[i]=id;values[i]=value;
  }
  pop(){
    const {ids,values}=this,id=ids[0];this.last=values[0];const n=--this.size;if(!n)return id;
    const nid=ids[n],nv=values[n];let i=0;
    while(i*2+1<n){
      let child=i*2+1;if(child+1<n&&this.before(ids[child+1],values[child+1],ids[child],values[child]))child++;
      if(!this.before(ids[child],values[child],nid,nv))break;
      ids[i]=ids[child];values[i]=values[child];i=child;
    }
    ids[i]=nid;values[i]=nv;return id;
  }
}
function escapeDistances(w,s){
  if(s.escapeDistances)return s.escapeDistances;
  const distance=new Uint16Array(w.height.length).fill(65535),queue=new Uint32Array(distance.length);
  let head=0,tail=0;
  for(let i=0;i<distance.length;i++)if(w.water[i]===1){distance[i]=0;queue[tail++]=i;}
  while(head<tail){const i=queue[head++],x=i%w.columns,y=Math.floor(i/w.columns),next=distance[i]+1;
    for(const [dx,dy] of [[-1,0],[1,0],[0,-1],[0,1]]){const j=index(w,x+dx,y+dy);if(distance[j]<=next)continue;distance[j]=next;queue[tail++]=j;}}
  s.escapeDistances=distance;return distance;
}
function oceanGates(w,s){
  if(s.escapeGates)return s.escapeGates;
  const gates=new Float64Array(w.height.length).fill(Infinity),heap=new Heap();
  for(let i=0;i<gates.length;i++)if(w.water[i]===1&&s.upper[i]<0){gates[i]=Math.fround(s.upper[i]);heap.push(i,gates[i]);}
  while(heap.size){const i=heap.pop(),cost=heap.last;if(cost!==gates[i])continue;
    const x=i%w.columns,y=Math.floor(i/w.columns);
    for(const [dx,dy] of [[-1,0],[1,0],[0,-1],[0,1]]){const j=index(w,x+dx,y+dy),gate=Math.max(cost,Math.fround(s.upper[j]));
      if(gate>=gates[j])continue;gates[j]=gate;heap.push(j,gate);}
  }
  s.escapeGates=gates;return gates;
}
function escapeRank(w,s,distance,x,y){
  const cx=Math.floor(x/s.div-.5),cy=Math.floor(y/s.div-.5);let best=Infinity;
  for(let dy=0;dy<=1;dy++)for(let dx=0;dx<=1;dx++){
    const col=cx+dx,row=cy+dy,d=distance[index(w,col,row)];
    best=Math.min(best,d*s.div+Math.abs(x-(col+.5)*s.div)+Math.abs(y-(row+.5)*s.div));
  }
  return best;
}
export class LakeShorePending extends Error{
  constructor(promise){super('湖岸连通计算中');this.promise=promise;}
}
export function cooperativeLakeShore(w,options){asyncModes.set(w,options);}
const smooth=v=>{const t=Math.max(0,Math.min(1,v));return t*t*(3-2*t);};
function index(w,x,y){
  y=((y%(w.rows*2))+w.rows*2)%(w.rows*2);
  if(y>=w.rows){y=w.rows*2-y-1;x+=w.columns/2;}
  return y*w.columns+((x%w.columns)+w.columns)%w.columns;
}
export function shoreContext(w,wx,wz){
  const x=(wx/w.circumference+.5)*w.columns-.5,y=(wz/w.circumference+.25)*w.columns-.5;
  const ix=Math.floor(x),iy=Math.floor(y),span=w.circumference/w.columns;
  const distance=Math.min(Math.abs(wz+w.circumference/4),Math.abs(wz-w.circumference/4));
  return {a:index(w,ix,iy),b:index(w,ix+1,iy),c:index(w,ix,iy+1),d:index(w,ix+1,iy+1),tx:x-ix,ty:y-iy,
    pole:distance<span?smooth(distance/span):-1,poleId:wz<0?0:1};
}
function stateFor(w,sampler){
  if(states.has(w))return states.get(w);
  if(!sampler)throw Error('细化湖岸缺少自然地形采样器');
  const span=w.circumference/w.columns,div=Math.max(8,Math.min(128,2**Math.ceil(Math.log2(span/256))));
  const s={sampler,div,width:w.columns*div,height:w.rows*div,step:span/div,catalog:[],models:new Map(),candidates:new Map(),sampled:0,buildMs:0};
  const byId=new Map();
  for(let i=0;i<w.water.length;i++)if(w.water[i]===2){
    const id=w.lakeId[i];let lake=byId.get(id);
    if(!lake){const source=w.lakes?.[id];lake={id,level:source?.level??w.spill[i],salinity:source?.salinity??'fresh',cells:[]};byId.set(id,lake);s.catalog.push(lake);}
    lake.cells.push(i);
  }
  s.lower=new Float64Array(w.height.length);
  s.upper=new Float64Array(w.height.length);
  for(let i=0;i<w.height.length;i++){
    const x=i%w.columns,y=Math.floor(i/w.columns);let low=Infinity,high=-Infinity,uplift=0;
    for(let dz=-1;dz<=1;dz++)for(let dx=-1;dx<=1;dx++){const j=index(w,x+dx,y+dz);low=Math.min(low,w.height[j]);high=Math.max(high,w.height[j]);uplift=Math.max(uplift,w.uplift?.[j]??0);}
    if(y===0||y===w.rows-1){const pole=w.poles?.height?.[y===0?0:1];low=Math.min(low,pole??low);high=Math.max(high,pole??high);}
    s.lower[i]=low-19.5-150*Math.min(1,Math.max(0,uplift));
    s.upper[i]=high+19.5+150*Math.min(1,Math.max(0,uplift));
  }
  const saved=w.lakeTopology;
  if(saved?.version===3&&saved.div===div&&saved.source===w.fingerprint){
    const maximum=Math.ceil(s.width/SIDE)*(Math.floor(s.height/SIDE)+1);
    for(const lake of s.catalog){const m=saved.models?.[lake.id];
      if(!m||!Number.isFinite(m.level)||m.level>lake.level||!Array.isArray(m.tiles))continue;
      if(!m.tiles.every(([key,bits])=>Number.isInteger(key)&&key>=0&&key<maximum&&(bits===1||Array.isArray(bits)&&bits.length===NODES/32&&bits.every(v=>Number.isInteger(v)&&v>=0&&v<=0xffffffff))))continue;
      s.models.set(lake.id,{...m,tiles:new Map(m.tiles.map(([key,bits])=>[key,bits===1?new Uint32Array(NODES/32).fill(0xffffffff):new Uint32Array(bits)])),elapsed:0});
    }
  }
  states.set(w,s);return s;
}
function potential(w,s,lake){
  if(lake.potential)return lake.potential;
  const bits=new Uint32Array(Math.ceil(w.height.length/32)),queue=new Uint32Array(w.height.length);
  let head=0,tail=0;
  const add=i=>{const bit=1<<(i&31);if(bits[i>>>5]&bit)return;if(s.lower[i]>=lake.level)return;bits[i>>>5]|=bit;queue[tail++]=i;};
  for(const cell of lake.cells)add(cell);
  while(head<tail){const i=queue[head++];if(w.water[i]===1)continue;const x=i%w.columns,y=Math.floor(i/w.columns);
    for(let dz=-1;dz<=1;dz++)for(let dx=-1;dx<=1;dx++)if(dx||dz)add(index(w,x+dx,y+dz));}
  lake.potential=bits;return bits;
}
function normalize(s,x,y){
  y=((y%(s.height*2))+s.height*2)%(s.height*2);
  if(y>s.height){y=s.height*2-y;x+=s.width/2;}
  x=((x%s.width)+s.width)%s.width;
  if(y===0||y===s.height)x=0;
  return {x,y};
}
function position(w,s,x,y){const p=normalize(s,x,y);return {wx:p.x/s.width*w.circumference-w.circumference/2,wz:p.y/s.width*w.circumference-w.circumference/4};}
function natural(w,s,x,y){s.sampled++;const p=position(w,s,x,y);return s.sampler(w,p.wx,p.wz);}
function drainsBelowSeeds(w,s,lake,wx,wz){
  lake.seedFloor??=lake.cells.reduce((low,cell)=>Math.min(low,Math.fround(natural(w,s,(cell%w.columns+.5)*s.div,(Math.floor(cell/w.columns)+.5)*s.div))),Infinity);
  const limit=lake.seedFloor;if(!(limit<lake.level))return true;
  const cell=index(w,Math.floor((wx/w.circumference+.5)*w.columns),Math.floor((wz/w.circumference+.25)*w.columns));
  if(oceanGates(w,s)[cell]<limit)return true;
  const x=Math.floor((wx/w.circumference+.5)*s.width),y=Math.floor((wz/w.circumference+.25)*s.width),quad=`${x},${y}`;
  s.oceanQuads??=new Map();if((s.oceanQuads.get(quad)??Infinity)<=limit)return true;
  s.oceanGates??=new Map();s.coastHeights??=new Map();
  const node=(x,y)=>{const p=normalize(s,x,y),key=p.y*s.width+p.x;
    let h=s.coastHeights.get(key);if(h===undefined){h=natural(w,s,p.x,p.y);s.coastHeights.set(key,h);if(s.coastHeights.size>32768)s.coastHeights.delete(s.coastHeights.keys().next().value);}
    return {...p,key,h};};
  const edge=(x,y,dx,dy)=>{const steps=Math.max(2,Math.ceil(s.step/128));
    for(let k=1;k<steps;k++)if(natural(w,s,x+dx*k/steps,y+dy*k/steps)>limit)return false;return true;};
  const corners=[node(x,y),node(x+1,y),node(x,y+1),node(x+1,y+1)];
  if(corners.every(p=>(s.oceanGates.get(p.key)??Infinity)<=limit)){
    s.oceanQuads.set(quad,limit);if(s.oceanQuads.size>32768)s.oceanQuads.delete(s.oceanQuads.keys().next().value);return true;
  }
  if(corners.some(p=>p.h>limit))return false;
  if(!edge(x,y,1,0)||!edge(x,y,0,1)||!edge(x+1,y,0,1))return false;
  const seen=new Set(),points=[],heap=new Heap(),distance=escapeDistances(w,s);
  const add=p=>{if(seen.has(p.key)||p.h>limit)return;seen.add(p.key);points.push(p);heap.push(points.length-1,escapeRank(w,s,distance,p.x,p.y));};
  add(corners[0]);let ocean=false;
  while(heap.size&&points.length<=2048){
    const p=points[heap.pop()],cell=index(w,Math.floor(p.x/s.div),Math.floor(p.y/s.div));
    if((w.water[cell]===1&&Math.fround(p.h)<0)||(s.oceanGates.get(p.key)??Infinity)<=limit){ocean=true;break;}
    for(const [dx,dy] of [[-1,0],[1,0],[0,-1],[0,1]]){
      const q=node(p.x+dx,p.y+dy);if(!seen.has(q.key)&&q.h<=limit&&edge(p.x,p.y,dx,dy))add(q);
    }
  }
  if(!ocean)return false;
  for(const p of [...points,...corners])s.oceanGates.set(p.key,Math.min(limit,s.oceanGates.get(p.key)??Infinity));
  while(s.oceanGates.size>32768)s.oceanGates.delete(s.oceanGates.keys().next().value);
  s.oceanQuads.set(quad,limit);if(s.oceanQuads.size>32768)s.oceanQuads.delete(s.oceanQuads.keys().next().value);
  return true;
}
function buildModel(w,s,lake){
  const task=buildModelSteps(w,s,lake);let step;
  do{step=task.next();}while(!step.done);return step.value;
}
function* buildModelSteps(w,s,lake,limit=lake.level,bulk=true,inheritedEscape=null,preview=false,bias=0){
  if(s.models.has(lake.id))return s.models.get(lake.id);
  const start=performance.now(),tiles=[],lookup=new Map(),tileColumns=Math.ceil(s.width/SIDE),distance=escapeDistances(w,s);
  const heap=new ShoreHeap(id=>tiles[Math.floor(id/NODES)].rank[id%NODES]);
  let cap=limit,visited=0,escape=inheritedEscape,bulkUsed=false,probeUsed=false;
  const keyFor=(cost,rank)=>bias?cost+bias*rank:cost;
  function node(x,y){
    const p=normalize(s,x,y),tx=Math.floor(p.x/SIDE),ty=Math.floor(p.y/SIDE),key=ty*tileColumns+tx;
    let tile=lookup.get(key);
    if(!tile){tile={id:tiles.length,key,x:tx*SIDE,y:ty*SIDE,h:new Float32Array(NODES).fill(NaN),cost:new Float32Array(NODES).fill(Infinity),done:new Uint8Array(NODES),rank:new Float64Array(NODES)};tiles.push(tile);lookup.set(key,tile);}
    const i=(p.y-tile.y)*SIDE+p.x-tile.x;
    if(Number.isNaN(tile.h[i])){tile.h[i]=natural(w,s,p.x,p.y);tile.rank[i]=escapeRank(w,s,distance,p.x,p.y);}
    return {tile,i,id:tile.id*NODES+i,x:p.x,y:p.y};
  }
  for(const cell of lake.cells){
    const p=node((cell%w.columns+.5)*s.div,(Math.floor(cell/w.columns)+.5)*s.div);
    if(p.tile.h[p.i]<cap){p.tile.cost[p.i]=p.tile.h[p.i];heap.push(p.id,keyFor(p.tile.cost[p.i],p.tile.rank[p.i]));}
  }
  while(heap.size){
    const id=heap.pop(),priority=heap.last,tile=tiles[Math.floor(id/NODES)],i=id%NODES,cost=tile.cost[i];
    if(tile.done[i]||priority!==keyFor(cost,tile.rank[i]))continue;if(!preview&&cost>=cap)break;
    tile.done[i]=1;visited++;
    if(visited%256===0)yield {lake:lake.id,visited,sampled:s.sampled};
    if(preview&&visited>=PROBE_LIMIT)break;
    if(bulk&&!preview&&!probeUsed&&visited>=PROBE_AFTER){
      probeUsed=true;
      const bound=yield* buildModelSteps(w,s,lake,cap,true,null,true,PROBE_BIAS);
      if(bound.cap<cap){tiles.length=0;lookup.clear();return yield* buildModelSteps(w,s,lake,bound.cap,false,bound.escape);}
    }
    const x=tile.x+i%SIDE,y=tile.y+Math.floor(i/SIDE),cell=index(w,Math.floor(x/s.div),Math.floor(y/s.div));
    if(tile.h[i]<0&&w.water[cell]===1){cap=cost;escape='ocean';break;}
    if(x%s.div===s.div/2&&y%s.div===s.div/2&&w.water[cell]===2&&w.lakeId[cell]!==lake.id){
      const other=w.lakes?.[w.lakeId[cell]]?.level??w.spill[cell];
      if(other<cap){cap=Math.min(cap,Math.max(cost,other));escape='lake';if(cost>=cap)break;}
    }
    // 深水整片连通，岸边仍逐点求解
    if(bulk&&s.div>=SIDE&&!tile.bulk&&w.water[cell]===2&&w.lakeId[cell]===lake.id&&tile.y+SIDE<=s.height&&s.upper[cell]<cap){
      const gate=Math.fround(Math.max(cost,s.upper[cell]));
      if(gate<cap){
        tile.bulk=true;bulkUsed=true;
        for(let k=0;k<NODES;k++)tile.rank[k]=escapeRank(w,s,distance,tile.x+k%SIDE,tile.y+Math.floor(k/SIDE));
        tile.h.fill(Math.fround(s.upper[cell]));tile.cost.fill(gate);tile.done.fill(1);visited--;
        for(let k=0;k<NODES;k++){const px=k%SIDE,py=Math.floor(k/SIDE);
          if(px===0||py===0||px===SIDE-1||py===SIDE-1){tile.done[k]=0;heap.push(tile.id*NODES+k,keyFor(gate,tile.rank[k]));}else visited++;}
        continue;
      }
    }
    for(const [dx,dy] of [[-1,0],[1,0],[0,-1],[0,1]]){
      const next=node(x+dx,y+dy),t=next.tile,j=next.i;
      if(t.done[j])continue;
      let gate=Math.max(cost,t.h[j]);if(gate>=cap||gate>=t.cost[j])continue;
      const steps=Math.max(2,Math.ceil(s.step/128));
      for(let k=1;k<steps;k++)gate=Math.max(gate,natural(w,s,x+dx*k/steps,y+dy*k/steps));
      gate=Math.fround(gate);if(gate>=cap||gate>=t.cost[j])continue;
      t.cost[j]=gate;heap.push(next.id,keyFor(gate,t.rank[j]));
    }
  }
  if(preview)return {cap,escape,visited};
  if(bulkUsed&&cap<limit){tiles.length=0;lookup.clear();return yield* buildModelSteps(w,s,lake,cap,false,escape);}
  const wetTiles=new Map();let wetNodes=0;
  for(const tile of tiles){const bits=new Uint32Array(NODES/32);let count=0;
    for(let i=0;i<NODES;i++)if(tile.done[i]&&tile.cost[i]<cap&&tile.h[i]<cap){bits[i>>>5]|=1<<(i&31);count++;}
    if(count){wetTiles.set(tile.key,bits);wetNodes+=count;}}
  const model={id:lake.id,level:cap,originalLevel:lake.level,salinity:lake.salinity,tiles:wetTiles,wetNodes,visited,escape,elapsed:performance.now()-start};
  if(w.lakeTopology?.version!==3||w.lakeTopology.source!==w.fingerprint)w.lakeTopology={version:3,div:s.div,source:w.fingerprint,models:{}};
  const saved={...model,tiles:[...wetTiles].map(([key,bits])=>[key,bits.every(v=>v===0xffffffff)?1:Array.from(bits)])};
  if(JSON.stringify(saved).length<8*2**20){
    const cache=w.lakeTopology;cache.models[lake.id]=saved;
    while(JSON.stringify(cache.models).length>8*2**20){const key=Object.keys(cache.models).find(key=>Number(key)!==lake.id);if(key===undefined)break;delete cache.models[key];}
  }
  s.models.set(lake.id,model);s.buildMs+=model.elapsed;return model;
}
function requestModel(w,s,lake,mode){
  s.jobs??=new Map();if(s.jobs.has(lake.id))return s.jobs.get(lake.id);
  s.tasks??=new Map();
  let task=s.tasks.get(lake.id);
  if(!task){task=buildModelSteps(w,s,lake);s.tasks.set(lake.id,task);}
  while(s.tasks.size>2){const oldest=s.tasks.keys().next().value;s.tasks.delete(oldest);}
  const cancelled=mode.cancelled?.()??(()=>false);
  const promise=(async()=>{
    let deadline=performance.now()+8;
    while(true){
      if(cancelled())return null;
      const step=task.next();if(step.done){s.tasks.delete(lake.id);return step.value;}
      if(performance.now()>=deadline){mode.progress?.(step.value);await mode.pause();deadline=performance.now()+8;}
    }
  })();
  s.jobs.set(lake.id,promise);promise.then(()=>s.jobs.delete(lake.id),()=>s.jobs.delete(lake.id));return promise;
}
function connectedNode(s,model,x,y){
  const p=normalize(s,x,y),tileColumns=Math.ceil(s.width/SIDE),key=Math.floor(p.y/SIDE)*tileColumns+Math.floor(p.x/SIDE),bits=model.tiles.get(key);
  const i=(p.y%SIDE)*SIDE+p.x%SIDE;return !!bits&&(bits[i>>>5]&(1<<(i&31)))!==0;
}
function connectedPoint(w,s,model,wx,wz){
  const x=(wx/w.circumference+.5)*s.width,y=(wz/w.circumference+.25)*s.width,ix=Math.floor(x),iy=Math.floor(y);
  const corners=[[ix,iy],[ix+1,iy],[ix,iy+1],[ix+1,iy+1]].sort((a,b)=>(a[0]-x)**2+(a[1]-y)**2-(b[0]-x)**2-(b[1]-y)**2);
  for(const [nx,ny] of corners){
    if(!connectedNode(s,model,nx,ny))continue;
    const steps=Math.max(2,Math.ceil(Math.hypot(nx-x,ny-y)*s.step/64));let valid=true;
    for(let k=1;k<=steps;k++)if(Math.fround(natural(w,s,x+(nx-x)*k/steps,y+(ny-y)*k/steps))>=Math.fround(model.level)){valid=false;break;}
    if(valid)return true;
  }
  return false;
}
export function sampleLakeShore(w,wx,wz,naturalHeight,base=null,context=null,sampler=null,details=true){
  const scoped=asyncModes.get(w)?.sample;
  if(scoped)return scoped(wx,wz,details?naturalHeight:sampler(w,wx,wz));
  const s=stateFor(w,sampler),cell=index(w,Math.floor((wx/w.circumference+.5)*w.columns),Math.floor((wz/w.circumference+.25)*w.columns));
  if(!s.catalog.length)return null;
  let candidates=s.candidates.get(cell);
  if(!candidates){
    candidates=s.catalog.filter(lake=>{const bits=potential(w,s,lake);return (bits[cell>>>5]&(1<<(cell&31)))!==0;});
    const cx=cell%w.columns,cy=Math.floor(cell/w.columns),distance=lake=>{const i=lake.cells[0],dx=Math.abs(i%w.columns-cx);return Math.min(dx,w.columns-dx)**2+(Math.floor(i/w.columns)-cy)**2;};
    candidates.sort((a,b)=>distance(a)-distance(b)||a.id-b.id);s.candidates.set(cell,candidates);
    if(s.candidates.size>4096)s.candidates.delete(s.candidates.keys().next().value);
  }
  if(!candidates.length)return null;
  if(!details)naturalHeight=s.sampler(w,wx,wz);
  for(const lake of candidates){
    if(Math.fround(naturalHeight)>=Math.fround(lake.level))continue;
    if(!s.models.has(lake.id)){
      if(drainsBelowSeeds(w,s,lake,wx,wz))continue;
    }
    const mode=asyncModes.get(w);
    if(mode&&!s.models.has(lake.id))throw new LakeShorePending(requestModel(w,s,lake,mode));
    const model=s.models.get(lake.id)??buildModel(w,s,lake);
    if(Math.fround(naturalHeight)>=Math.fround(model.level)||!connectedPoint(w,s,model,wx,wz))continue;
    return {height:naturalHeight,water:model.level,kind:2,salinity:model.salinity,saltLakeId:model.salinity==='salt'?model.id:null};
  }
  return {height:naturalHeight,water:-Infinity,kind:0,salinity:null,saltLakeId:null};
}
export function lakeShoreStats(w){const s=states.get(w);return s?{step:s.step,sampled:s.sampled,buildMs:s.buildMs,models:[...s.models.values()].map(({tiles,...m})=>({...m,tileCount:tiles.size,bytes:tiles.size*NODES/8}))}:null;}
