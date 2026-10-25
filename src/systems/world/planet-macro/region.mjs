import {retrySample,runSync,runPreview} from './preview-scheduler.mjs';
import {sampleSurface,macroAt,regionalRelief,dataBytes} from './backend.mjs';
import {buildPatch,buildPatchSteps,gridBuffers,finishGrid} from './patch.mjs';
import {getTributaries,applyTributaries,riverChunkLabel} from './source-rivers.mjs';
import {cellAt} from './sphere-frame.mjs';
import {sphereFrame} from './sphere-frame.mjs';
import {finishCover} from './surface-cover.mjs';

const TAU=Math.PI*2;
const tileCaches=new WeakMap(),TILE_LIMIT=192,TILE_BUDGET=8*2**20;
function tileCache(world){let c=tileCaches.get(world);if(!c){c={items:new Map(),bytes:0,hits:0,misses:0,evictions:0};tileCaches.set(world,c);}return c;}
export function clearChunkCache(world){tileCaches.delete(world);}
export function chunkCacheStats(world){const c=tileCache(world);return {entries:c.items.size,bytes:c.bytes,hits:c.hits,misses:c.misses,evictions:c.evictions,limit:TILE_LIMIT,budget:TILE_BUDGET};}
export function createRegion(world,cell,tributaries=true,options={}) {
  if(!Number.isInteger(cell)||cell<0||cell>=world.columns*world.rows)throw Error('区域编号无效');
  const lon=((cell%world.columns+.5)/world.columns-.5)*TAU;
  const lat=(.25-(Math.floor(cell/world.columns)+.5)/world.rows*.5)*TAU;
  const anchor=[Math.cos(lat)*Math.cos(lon),Math.sin(lat),Math.cos(lat)*Math.sin(lon)];
  const east=[-Math.sin(lon),0,Math.cos(lon)],south=[Math.sin(lat)*Math.cos(lon),-Math.cos(lat),Math.sin(lat)*Math.sin(lon)];
  const size=world.circumference/world.columns;
  const coordinates=(x,z)=>{
    const distance=Math.hypot(x,z),angle=distance/world.radius,c=Math.cos(angle),k=distance?Math.sin(angle)/distance:0;
    const p=anchor.map((v,i)=>v*c+(east[i]*x+south[i]*z)*k);
    return {wx:Math.atan2(p[2],p[0])/TAU*world.circumference,wz:-Math.asin(Math.max(-1,Math.min(1,p[1])))/TAU*world.circumference};
  };
  const networks=new Map();
  const sample=(x,z,details=true)=>{const {wx,wz}=coordinates(x,z);let s=sampleSurface(world,wx,wz,details);
    s.wetland??=0;
    if(tributaries&&details){const owner=cellAt(world,wx,wz);let n=networks.get(owner);if(!n){n=getTributaries(world,owner);networks.set(owner,n);}s=applyTributaries(world,wx,wz,s,n,options.chunkLabels!==false);}
    if(s.river){const atLon=wx/world.circumference*TAU,atLat=-wz/world.circumference*TAU;
      const e=[-Math.sin(atLon),0,Math.cos(atLon)],n=[-Math.sin(atLat)*Math.cos(atLon),Math.cos(atLat),-Math.sin(atLat)*Math.sin(atLon)];
      const v=e.map((a,i)=>a*s.river.directionEast+n[i]*s.river.directionNorth),dot=(a,b)=>a.reduce((sum,c,i)=>sum+c*b[i],0);
      const dx=dot(v,east),dz=dot(v,south),length=Math.hypot(dx,dz)||1;
      s.river={...s.river,directionX:dx/length,directionZ:dz/length};
    }return details?finishCover(s):s;};
  return {cell,size,lon,lat,coordinates,sample};
}

export function buildRegion(world,cell,step=256,tributaries=true) {
  const region=createRegion(world,cell,tributaries);
  if(!Number.isFinite(step)||step<1||Math.ceil(region.size/step)>256)throw Error('区域预览最多每轴256段');
  const mesh=buildPatch(world,0,0,region.size,step,(x,z)=>region.sample(x-region.size/2,z-region.size/2),true);
  return {...mesh,cell,bytes:dataBytes(mesh),chunkColumns:Math.ceil(region.size/64),projection:'sphere exponential map; metric local axes'};
}

function* neighborhoodSteps(world,cell,step=256){
  const start=performance.now(),timings={};
  const region=createRegion(world,cell,true,{chunkLabels:false}),frame=sphereFrame(world,cell),size=region.size*3,col=cell%world.columns,row=Math.floor(cell/world.columns),cells=new Set();
  for(let dz=-1;dz<=1;dz++)for(let dx=-1;dx<=1;dx++){
    let x=col+dx,y=row+dz;if(y<0){y=-y-1;x+=world.columns/2;}if(y>=world.rows){y=world.rows*2-y-1;x+=world.columns/2;}
    cells.add(y*world.columns+(x%world.columns+world.columns)%world.columns);
  }
  const mesh=yield* buildPatchSteps(world,0,0,size,step,(x,z)=>region.sample(x-size/2,z-size/2),true),indices=[],waterIndices=[];
  timings.surfaceAndGridMs=performance.now()-start;const clipStart=performance.now();
  const inside=(x,z)=>{const g=region.coordinates(x-size/2,z-size/2);return cells.has(cellAt(world,g.wx,g.wz));};
  for(let k=0;k<mesh.indices.length;k+=3){if(k%1536===0)yield {stage:'区域裁剪'};const ids=Array.from(mesh.indices.subarray(k,k+3));if(inside(ids.reduce((s,i)=>s+mesh.positions[i*3],0)/3,ids.reduce((s,i)=>s+mesh.positions[i*3+2],0)/3))indices.push(...ids);}
  for(let k=0;k<mesh.waterIndices.length;k+=3){if(k%1536===0)yield {stage:'水面裁剪'};const ids=Array.from(mesh.waterIndices.subarray(k,k+3));if(inside(ids.reduce((s,i)=>s+mesh.waterPositions[i*3],0)/3,ids.reduce((s,i)=>s+mesh.waterPositions[i*3+2],0)/3))waterIndices.push(...ids);}
  mesh.indices=new Uint32Array(indices);mesh.waterIndices=new Uint32Array(waterIndices);
  timings.clipMs=performance.now()-clipStart;const overlayStart=performance.now();
  yield {stage:'河网与边界'};
  const current=yield* retrySample(()=>getTributaries(world,cell)),network={...current,frame:{...frame,size},mainEdges:[],edges:[],sources:[],bends:[],lakePoints:[],chunkLabels:new Map()},seen=new Set(),borders=[];
  for(const owner of cells){
    const n=yield* retrySample(()=>getTributaries(world,owner)),convert=p=>{const g=n.frame.coordinates(p.x,p.z);return {...p,...frame.local(g.wx,g.wz)};};
    for(const edge of [...n.mainEdges,...n.edges]){if(seen.has(edge.id))continue;seen.add(edge.id);const e={...edge,points:edge.points.map(convert)};(edge.sourceType==='macro'?network.mainEdges:network.edges).push(e);}
    network.sources.push(...n.sources.map(convert));
    network.bends.push(...n.bends.map(convert));
    network.lakePoints.push(...(n.lakePoints??[]).map(convert));
    const x=owner%world.columns,y=Math.floor(owner/world.columns),border=[];
    for(const [ax,ay,bx,by] of [[x,y,x+1,y],[x+1,y,x+1,y+1],[x+1,y+1,x,y+1],[x,y+1,x,y]])for(let k=0;k<=32;k++){
      const t=k/32,wx=((ax+(bx-ax)*t)/world.columns-.5)*world.circumference,wz=((ay+(by-ay)*t)/world.rows*.5-.25)*world.circumference,p=frame.local(wx,wz),s=yield* retrySample(()=>region.sample(p.x,p.z));
      border.push(p.x+size/2,Math.max(s.height,Number.isFinite(s.water)?s.water:-Infinity)+.5,p.z+size/2);
    }
    yield {stage:'河网与边界'};
    borders.push({cell:owner,positions:new Float32Array(border),selected:owner===cell});
  }
  timings.networkAndBordersMs=performance.now()-overlayStart;
  return {...mesh,cell,macroCells:[...cells],borders,network,contains:inside,timings,elapsed:performance.now()-start};
}

function* chunkSteps(world,cell,cx,cz,size=512,step=4,tributaries=true,options={}) {
  const start=performance.now();
  const region=createRegion(world,cell,tributaries);
  if(!Number.isInteger(cx)||!Number.isInteger(cz))throw Error('区块坐标无效');
  if(!Number.isInteger(size)||!Number.isInteger(step)||size<1||step<1||size%step)throw Error('地块尺寸与步长不匹配');
  if(options.cache===false||size%64||64%step)return yield* buildPatchSteps(world,cx,cz,size,step,(x,z)=>region.sample(x-region.size/2,z-region.size/2),false,options);
  const count=size/64,tiles=[];let missing=0;
  for(let z=0;z<count;z++)for(let x=0;x<count;x++){const tile=yield* retrySample(()=>getTile(world,region,cx+x,cz+z,step,tributaries));tiles.push(tile.patch);if(tile.miss)missing++;yield {stage:'区块采样',completed:tiles.length,total:count*count};}
  const side=size/step+1,buffers=gridBuffers(side,options.reuse),{positions,colors,levels,kinds,wetlands}=buffers,interval=64/step;let min=Infinity,max=-Infinity;
  for(const tile of tiles){min=Math.min(min,tile.min);max=Math.max(max,tile.max);}
  for(let z=0;z<side;z++)for(let x=0;x<side;x++){
    const tx=Math.min(count-1,Math.floor(x/interval)),tz=Math.min(count-1,Math.floor(z/interval)),tile=tiles[tz*count+tx],j=(z-tz*interval)*tile.side+x-tx*interval,i=z*side+x;
    positions[i*3]=x*step;positions[i*3+1]=tile.positions[j*3+1];positions[i*3+2]=z*step;
    colors[i*3]=tile.colors[j*3];colors[i*3+1]=tile.colors[j*3+1];colors[i*3+2]=tile.colors[j*3+2];levels[i]=tile.levels[j];kinds[i]=tile.kinds[j];wetlands[i]=tile.wetlands[j];
  }
  const patch=finishGrid(cx,cz,size,step,buffers,min,max,start);return {...patch,newChunks:missing,reusedChunks:count*count-missing};
}

function getTile(world,region,cx,cz,step,tributaries=true){
  const c=tileCache(world),key=`${region.cell}:${cx}:${cz}:${step}:${tributaries}`;
  if(c.items.has(key)){const value=c.items.get(key);c.items.delete(key);c.items.set(key,value);c.hits++;return {...value,miss:false};}
  c.misses++;const acc=annotationAccumulator(),patch=buildPatch(world,cx,cz,64,step,(x,z)=>region.sample(x-region.size/2,z-region.size/2),false,{onSample:acc.observe});
  const bytes=dataBytes(patch),value={patch,annotation:acc.finish(region.cell,cx,cz),bytes};
  if(bytes<=TILE_BUDGET){while(c.items.size&&(c.items.size>=TILE_LIMIT||c.bytes+bytes>TILE_BUDGET)){const k=c.items.keys().next().value;c.bytes-=c.items.get(k).bytes;c.items.delete(k);c.evictions++;}c.items.set(key,value);c.bytes+=bytes;}
  return {...value,miss:true};
}

export function inspectRegion(world,cell,cx,cz) {
  const region=createRegion(world,cell),x=cx*64+32-region.size/2,z=cz*64+32-region.size/2;
  const p=region.coordinates(x,z),surface=region.sample(x,z);
  return {cell,cx,cz,size:region.size,macroHeight:macroAt(world,p.wx,p.wz),regionalRelief:regionalRelief(world,p.wx,p.wz),surfaceHeight:surface.height,water:surface.water,kind:surface.kind};
}

function annotationAccumulator(){
  const rivers=new Map();let wet=0,total=0,lake=0,sea=0,marsh=0,strength=0;
  const observe=(x,z,s)=>{
    total++;if(s.kind===1)sea++;if(s.kind===2)lake++;if(s.wetland>.2){marsh++;strength+=s.wetland;}if(!s.river)return;wet++;
    const r=s.river,key=r.id??r.cell,entry=rivers.get(key)??{...r,samples:0,x:0,z:0,height:0,dx:0,dz:0};
    entry.samples++;entry.x+=x;entry.z+=z;entry.height+=s.water;entry.dx+=r.directionX;entry.dz+=r.directionZ;rivers.set(key,entry);
  };
  const finish=(cell,cx,cz)=>{const segments=[...rivers.values()].map(r=>{const length=Math.hypot(r.dx,r.dz)||1;return {...r,x:r.x/r.samples,z:r.z/r.samples,height:r.height/r.samples,directionX:r.dx/length,directionZ:r.dz/length};}).sort((a,b)=>b.samples-a.samples||a.cell-b.cell);
    return {cell,cx,cz,isRiverChunk:segments.length>0,isWetlandChunk:marsh>=4,wetlandSamples:marsh,wetlandCoverage:marsh/total,wetlandStrength:strength/total,classification:segments.length?'river':marsh>=4?'wetland':lake?'lake':sea?'ocean':'land',samples:total,riverSamples:wet,riverCoverage:wet/total,segments};};
  return {observe,finish};
}
export function describeRiverChunk(world,cell,cx,cz,step=4,options={}) {
  if(!Number.isInteger(cx)||!Number.isInteger(cz)||!Number.isInteger(step)||step<1||64%step)throw Error('河区块采样参数无效');
  const start=performance.now(),region=createRegion(world,cell);let result;
  if(options.cache!==false)result=getTile(world,region,cx,cz,step,true).annotation;
  else{const acc=annotationAccumulator();for(let z=0;z<=64;z+=step)for(let x=0;x<=64;x+=step){const s=region.sample(cx*64+x-region.size/2,cz*64+z-region.size/2);acc.observe(x,z,s);}result=acc.finish(cell,cx,cz);
  }
  const label=riverChunkLabel(getTributaries(world,cell),cx,cz);
  const classification=label.isWetland?'wetland':label.isStaticWater?label.waterBody==='ocean'?'ocean':label.waterBody==='lake'?'lake':'static':label.isWaterfall?'waterfall':label.isRiver?'river':'land';
  return {...result,isRiverChunk:label.isRiver,hasDecorativeStream:!!label.hasDecorativeStream,decorativeEdges:label.decorativeEdges??[],isWaterfallChunk:!!label.isWaterfall,waterfallDropM:label.waterfallDropM??0,waterfall:label.waterfall??null,chunkId:label.id,chunkCode:label.code,chunkName:label.name,landClass:label.landClass,material:label.material,terrain:label.terrain,salinity:label.salinity,saltShore:!!label.saltShore,classification,elapsed:performance.now()-start,cached:options.cache!==false,scope:'Natural water and continuous river footprints classified before carving.'};
}

export function buildRegionNeighborhood(...args){return runSync(neighborhoodSteps(...args));}
export function buildRegionNeighborhoodAsync(world,cell,step,options={}){return runPreview(world,neighborhoodSteps(world,cell,step),options);}

export function buildRegionChunk(...args){return runSync(chunkSteps(...args));}
export function buildRegionChunkAsync(world,cell,cx,cz,size,step,tributaries,options={}){return runPreview(world,chunkSteps(world,cell,cx,cz,size,step,tributaries,options),options);}
