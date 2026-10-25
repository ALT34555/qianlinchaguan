import {drainSteps,runSteps} from './generation-steps.mjs';
import {sampleSurface,macroAt} from './backend.mjs';
import {sphereFrame,cellAt} from './sphere-frame.mjs';
import {chunkMetadata,landChunkMetadata} from './chunk-metadata.mjs';
import {firstMainIntersection,decorativePath} from './tributary-carving.mjs';
import {templateTributaryProfile,templateTributarySection} from './tributary-templates.mjs';
import {getLakeRegion} from './lake-regions.mjs';
import {macroDepth,macroBandWidth,decorativeDimensions} from './river-dimensions.mjs';
import {runoffM3s} from './water-balance.mjs';
import {riverCenterline} from './river-tangents.mjs';
import {isRiverHead} from './river-headwaters.mjs';
import {indexVectorEdge,queryVectorSegments,carveVectorSegments} from './vector-channel.mjs';
import {configureSpring,applySpringSurface,springRockPattern} from './spring-geometry.mjs';
import {configureWetPoint,applyWetPoints,wetPointRocks} from './wet-point-geometry.mjs';
import {lakeOutflowGeometry,buildLakeOutflow} from './lake-outflow.mjs';
import {mainRiverWidth} from './river-continuity.mjs';
import {headwaterLandform,headwaterSource} from './headwater-landforms.mjs';
import {bankedChannelSection} from './lake-visual.mjs';
const caches=new WeakMap(),mainCaches=new WeakMap(),LIMIT=32,BIN=512;
const curves=new WeakMap();
const geometries=new WeakMap();
const channels=new WeakMap(),profiles=new WeakMap(),labels=new WeakMap(),falls=new WeakMap(),CHUNK=64,PROFILE_STEP=32;
const mouths=new WeakMap();
const waterProbes=new WeakMap();
const clamp=(v,a=0,b=1)=>Math.max(a,Math.min(b,v));
function random(seed){return()=>{let t=seed+=0x6d2b79f5;t=Math.imul(t^t>>>15,t|1);t^=t+Math.imul(t^t>>>7,t|61);return((t^t>>>14)>>>0)/4294967296;};}
function cache(w){let c=caches.get(w);if(!c){c={items:new Map(),hits:0,misses:0,evictions:0,generationMs:0};caches.set(w,c);}return c;}
export function clearTributaryCache(w){caches.delete(w);mainCaches.delete(w);}
export function tributaryCacheStats(w){const c=cache(w);return {limit:LIMIT,entries:c.items.size,hits:c.hits,misses:c.misses,evictions:c.evictions,generationMs:c.generationMs,edges:[...c.items.values()].reduce((s,n)=>s+n.edges.length,0),wetlands:[...c.items.values()].reduce((s,n)=>s+n.wetlands.length,0)};}
export function wetPointCount(rainfall){return rainfall<400?0:Math.min(12,Math.floor(rainfall/400)*3);}
function crossesMacroCell(w,frame,edge){
  const size=frame.size,centerX=frame.lon*w.circumference/(Math.PI*2),centerZ=-frame.lat*w.circumference/(Math.PI*2);
  const points=edge.points.map(p=>{const g=frame.coordinates(p.x,p.z),dx=g.wx-centerX;return {x:dx-Math.round(dx/w.circumference)*w.circumference,z:g.wz-centerZ};});
  for(let k=1;k<points.length;k++){
    const a=points[k-1],b=points[k],dx=b.x-a.x,dz=b.z-a.z;let lo=0,hi=1,valid=true;
    for(const [p,q] of [[-dx,a.x+size/2],[dx,size/2-a.x],[-dz,a.z+size/2],[dz,size/2-a.z]]){if(Math.abs(p)<1e-9){if(q<0)valid=false;}else{const t=q/p;if(p<0)lo=Math.max(lo,t);else hi=Math.min(hi,t);}}
    if(valid&&lo<=hi)return true;
  }
  return false;
}
// 包含被相邻主河弯道穿过的格
export function macroCellHasWater(w,cell){
  if(w.water[cell]===1||w.water[cell]===2||mainRiverWidth(w,cell)>0)return true;
  const col=cell%w.columns,row=Math.floor(cell/w.columns),frame=sphereFrame(w,cell);
  for(let dz=-1;dz<=1;dz++)for(let dx=-1;dx<=1;dx++){
    let y=row+dz,x=col+dx;if(y<0){y=-y-1;x+=w.columns/2;}if(y>=w.rows){y=w.rows*2-y-1;x+=w.columns/2;}
    const i=y*w.columns+(x%w.columns+w.columns)%w.columns;if(!mainRiverWidth(w,i))continue;
    const curve=macroCurveGeometry(w,i);if(!curve)continue;
    if(crossesMacroCell(w,frame,{points:curve.points.map(p=>frame.local(p.wx,p.wz))}))return true;
  }
  return false;
}
export function macroCurveGeometry(w,cell){return macroCurve(w,cell,true);}
export function macroCurve(w,cell,geometryOnly=false){
  const cache=geometryOnly?geometries:curves;
  let stored=cache.get(w);if(!stored){stored=new Map();cache.set(w,stored);}if(stored.has(cell))return stored.get(cell);
  const receiver=w.receiver[cell];if(receiver<0||!mainRiverWidth(w,cell))return null;
  const head=isRiverHead(w,cell);
  const startWidth=macroBandWidth(w.flow[cell])/2,endWidth=macroBandWidth(w.flow[receiver])/2;
  const frame=sphereFrame(w,cell),targetFrame=sphereFrame(w,receiver),target=frame.local(targetFrame.lon*w.circumference/(Math.PI*2),-targetFrame.lat*w.circumference/(Math.PI*2));
  const length=Math.hypot(target.x,target.z),rng=random(w.options.seed^Math.imul(cell+1,0x7f4a7c15)),chunks=frame.size/64;
  const gentle=1-clamp(Math.abs(w.spill[cell]-w.spill[receiver])/Math.max(1,length)/.012),centerline=riverCenterline(w,cell,frame,target);
  const count=chunks<10?0:Math.min(3,Math.max(1,Math.round((1+gentle*2)*clamp(Math.sqrt(chunks/625),.35,1))));
  const amplitude=Math.min(frame.size*.1,length/(count+1)*.3)*(.2+.8*gentle),knots=[{t:0,offset:0}],points=[],bends=[];
  const side=Math.sin(frame.lon*w.radius/260000+Math.sin(frame.lat*13)*2+(w.options.seed%997))<0?-1:1;
  for(let k=1;k<=count;k++)knots.push({t:k/(count+1),offset:side*(k%2?1:-1)*amplitude*(.65+rng()*.35)});
  knots.push({t:1,offset:0});
  const steps=(count+1)*Math.max(8,Math.ceil(32/(count+1)));
  for(let k=0;k<=steps;k++){const t=k/steps,section=Math.min(knots.length-2,Math.floor(t*(count+1))),a=knots[section],b=knots[section+1],u=clamp((t-a.t)/(b.t-a.t)),blend=u*u*(3-2*u),wave=a.offset+(b.offset-a.offset)*blend;
    const base=centerline(t),x=base.x-target.z/Math.max(1,length)*wave,z=base.z+target.x/Math.max(1,length)*wave,g=frame.coordinates(x,z);
    if(k===0){g.wx=frame.lon*w.circumference/(Math.PI*2);g.wz=-frame.lat*w.circumference/(Math.PI*2);}
    if(k===steps){g.wx=targetFrame.lon*w.circumference/(Math.PI*2);g.wz=-targetFrame.lat*w.circumference/(Math.PI*2);}
    const width=startWidth+(endWidth-startWidth)*t*t*(3-2*t);
    points.push({...g,level:w.spill[cell]+(w.spill[receiver]-w.spill[cell])*t,halfWidth:width});}
  for(const knot of knots.slice(1,-1)){
    const base=centerline(knot.t),x=base.x-target.z/Math.max(1,length)*knot.offset,z=base.z+target.x/Math.max(1,length)*knot.offset,g=frame.coordinates(x,z);
    bends.push({...g,level:w.spill[cell]+(w.spill[receiver]-w.spill[cell])*knot.t,t:knot.t});
  }
  if(geometryOnly){
    const curve={cell,receiver,points:[...lakeOutflowGeometry(w,cell,points[0]),...points],bends,dischargeM3s:w.flow[cell],halfWidth:Math.max(startWidth,endWidth)};
    stored.set(cell,curve);if(stored.size>1024)stored.delete(stored.keys().next().value);return curve;
  }
  // 红点与端点共用自然高度锚点
  const anchors=knots.map(knot=>{
    const point=points[Math.round(knot.t*steps)],surface=sampleSurface(w,point.wx,point.wz,true,false,true,false,false);
    return {t:knot.t,naturalHeight:surface.height,level:Number.isFinite(surface.water)?surface.water:surface.height-.8};
  });
  const startDepth=head ? .5 : macroDepth(w.flow[cell]),endDepth=macroDepth(w.flow[receiver]);
  anchors.forEach(a=>a.depth=startDepth+(endDepth-startDepth)*a.t);
  for(let k=0;k<points.length;k++){
    const t=k/steps,j=Math.min(anchors.length-2,Math.floor(t*(count+1))),a=anchors[j],b=anchors[j+1],u=clamp((t-a.t)/(b.t-a.t));
    points[k].level=a.level+(b.level-a.level)*u;
    points[k].depth=a.depth+(b.depth-a.depth)*u*u*(3-2*u);
    points[k].halfWidth=startWidth+(endWidth-startWidth)*t*t*(3-2*t);
  }
  bends.forEach((bend,k)=>{bend.level=anchors[k+1].level;bend.depth=anchors[k+1].depth;});
  const outflow=buildLakeOutflow(w,cell,points[0],(x,z)=>sampleSurface(w,x,z,true,false,true,false,false,false),anchors.at(-1).level);
  if(outflow||!w.riverWidth[cell]){
    const start=outflow?.endLevel??anchors[0].level,end=Math.min(start,anchors.at(-1).level);
    for(let k=0;k<points.length;k++)points[k].level=start+(end-start)*k/(points.length-1);
    for(const bend of bends)bend.level=start+(end-start)*bend.t;
    if(outflow)points.unshift(...outflow.points.slice(0,-1));
  }
  const curve={cell,receiver,points,bends,anchors,lakeOutflow:!!outflow,dischargeM3s:w.flow[cell],halfWidth:Math.max(startWidth,endWidth)};stored.set(cell,curve);if(stored.size>1024)stored.delete(stored.keys().next().value);return curve;
}
function index(network,edge){
  indexVectorEdge(network.bins,edge,BIN);
}
function nearMainChannel(n,x,z){
  for(const seg of n.bins.get(Math.floor(x/BIN)+','+Math.floor(z/BIN))??[]){
    if(seg.edge.sourceType!=='macro')continue;
    const t=clamp(((x-seg.a.x)*seg.vx+(z-seg.a.z)*seg.vz)/seg.length2),width=seg.a.halfWidth+(seg.b.halfWidth-seg.a.halfWidth)*t;
    if(Math.hypot(x-seg.a.x-seg.vx*t,z-seg.a.z-seg.vz*t)<width+30)return true;
  }
  return false;
}
// 预筛只读宏观量
export function tributaryCandidate(w,cell){
  if(w.water[cell]===2)return {cell,lake:true};
  if(w.circumference/w.columns<640||w.height[cell]<=0)return null;
  if(runoffM3s(w.rainfall[cell],w.evaporation[cell],w.areas[cell])<.03)return null;
  if(!wetPointCount(w.rainfall[cell])&&!isRiverHead(w,cell))return null;
  const col=cell%w.columns,row=Math.floor(cell/w.columns);
  for(let dz=-1;dz<=1;dz++)for(let dx=-1;dx<=1;dx++){
    let y=row+dz,x=col+dx;
    if(y<0){y=-y-1;x+=w.columns/2;}
    if(y>=w.rows){y=w.rows*2-y-1;x+=w.columns/2;}
    const neighbor=y*w.columns+(x%w.columns+w.columns)%w.columns;
    if(mainRiverWidth(w,neighbor))return {cell,lake:false};
  }
  return null;
}
function hotNetwork(c,cell){
  const n=c?.items.get(cell);if(!n)return null;
  c.hits++;if(c.lastCell!==cell){c.items.delete(cell);c.items.set(cell,n);c.lastCell=cell;}return n;
}
export function getMainRiverNetwork(w,cell){
  return hotNetwork(caches.get(w),cell)??hotNetwork(mainCaches.get(w),cell)??drainSteps(tributarySteps(w,cell,true));
}
export function getTributaries(w,cell){return hotNetwork(caches.get(w),cell)??drainSteps(tributarySteps(w,cell));}
export function getTributariesAsync(w,cell,options){return runSteps(tributarySteps(w,cell),options);}
function* tributarySteps(w,cell,mainOnly=false){
  let c;
  if(mainOnly){
    c=mainCaches.get(w);
    if(!c){c={items:new Map(),hits:0,misses:0,evictions:0,generationMs:0};mainCaches.set(w,c);}
  }else c=cache(w);
  if(c.items.has(cell)){const n=c.items.get(cell);c.items.delete(cell);c.items.set(cell,n);c.hits++;return n;}
  c.misses++;const start=performance.now(),frame=sphereFrame(w,cell),rng=random(w.options.seed^Math.imul(cell+1,0x45d9f3b));
  const runoff=runoffM3s(w.rainfall[cell],w.evaporation[cell],w.areas[cell]);
  const n={world:w,cell,type:'sources',routingProbes:0,frame,nodes:[],sources:[],edges:[],mainEdges:[],wetlands:[],bins:new Map(),chunkLabels:new Map(),localRunoffM3s:runoff,allocatedM3s:0,macroFlowM3s:w.flow[cell],macroReceiver:w.receiver[cell],rejectedPaths:0};
  const col=cell%w.columns,row=Math.floor(cell/w.columns),seen=new Set();
  for(let dz=-1;dz<=1;dz++)for(let dx=-1;dx<=1;dx++){
    let y=row+dz,x=col+dx;if(y<0){y=-y-1;x+=w.columns/2;}if(y>=w.rows){y=w.rows*2-y-1;x+=w.columns/2;}
    const i=y*w.columns+(x%w.columns+w.columns)%w.columns;if(seen.has(i))continue;seen.add(i);
    yield;
    const curve=macroCurve(w,i);if(!curve)continue;
    const edge={id:'macro:'+i,sourceType:'macro',lakeOutflow:curve.lakeOutflow,head:isRiverHead(w,i),cell:i,receiver:curve.receiver,dischargeM3s:curve.dischargeM3s,halfWidth:curve.halfWidth,depth:macroDepth(curve.dischargeM3s),
      points:curve.points.map(p=>({...frame.local(p.wx,p.wz),level:p.level,depth:p.depth,halfWidth:p.halfWidth})),bends:curve.bends.map(p=>({...frame.local(p.wx,p.wz),level:p.level,depth:p.depth,edgeId:'macro:'+i}))};n.mainEdges.push(edge);index(n,edge);
  }
  if(mainOnly){
    n.elapsed=performance.now()-start;
    c.items.set(cell,n);
    if(c.items.size>LIMIT)c.items.delete(c.items.keys().next().value);
    return n;
  }
  const chunks=frame.size/64;
  const head=isRiverHead(w,cell);
  const headForm=headwaterLandform(w,cell),headEdge=n.mainEdges.find(e=>e.cell===cell);
  if(w.water[cell]===2){
    const lake=getLakeRegion(w,cell,(x,z)=>sampleSurface(w,x,z,true,false,false,false,false));n.lake=lake;
    n.sources=lake.sources.filter(s=>!s.lakeMember).map(s=>({...s,lakePoint:true}));
    n.lakePoints=lake.nodes.map(s=>({...s,level:Math.max(s.level,lake.level)}));
  }
  n.bends=n.mainEdges.flatMap(e=>e.bends).filter(p=>{const g=frame.coordinates(p.x,p.z);return cellAt(w,g.wx,g.wz)===cell;});
  n.hasMacroRiver=n.mainEdges.some(e=>crossesMacroCell(w,frame,e));
  const count=chunks<10||w.height[cell]<=0||w.water[cell]===2||!n.hasMacroRiver||runoff<.03?0:Math.max(head?4:0,wetPointCount(w.rainfall[cell]));
  n.targetWetPoints=n.lake?n.sources.length:count;
  const riverTargets=n.mainEdges.flatMap(edge=>edge.points.filter((p,k)=>k%4===0).map(p=>({...p,edgeId:edge.id})));
  const spacing=Math.max(64,frame.size*.12);
  for(let k=0;k<count;k++){
    yield;
    let source=null;
    // 河源优先寻找雪线附近高地
    if(head&&k<4&&w.height[cell]>300&&w.temperature[cell]>-15&&w.temperature[cell]<12){
      let best=null;
      for(let attempt=0;attempt<8;attempt++){
        yield;
        const x=(rng()-.5)*frame.size*.9,z=(rng()-.5)*frame.size*.9,g=frame.coordinates(x,z);
        if(cellAt(w,g.wx,g.wz)!==cell||n.sources.some(p=>Math.hypot(p.x-x,p.z-z)<spacing))continue;
        const s=sampleSurface(w,g.wx,g.wz,false,false,false);
        if(s.kind||s.height<500||s.temperature< -8||s.temperature>0)continue;
        const score=Math.abs(s.temperature+1);
        if(!best||score<best.score)best={x,z,s,score};
      }
      if(best){
        const {x,z}=best,g=frame.coordinates(x,z),s=sampleSurface(w,g.wx,g.wz,true,false,true,false,false),dimensions=decorativeDimensions(macroAt(w,g.wx,g.wz,'rainfall'),macroAt(w,g.wx,g.wz,'evaporation'),w.columns,'snowmelt');
        if(!s.kind&&s.height>=500&&dimensions.dischargeM3s>0)source={id:'source:'+cell+':'+k,x,z,level:s.height-.3,weight:1,chunkType:2,connected:false,attempts:0,...dimensions,sourceKind:'snowmelt'};
      }
    }
    for(let attempt=0;!source&&attempt<6;attempt++){
      yield;
      const anchor=n.bends.length?n.bends[k%n.bends.length]:null,angle=rng()*Math.PI*2,radius=frame.size*(.12+rng()*.22);
      const candidate=headForm&&k<3?headwaterSource(headForm,k,attempt):null;
      const x=candidate?.x??(anchor?anchor.x+Math.cos(angle)*radius:(rng()-.5)*frame.size*.9),z=candidate?.z??(anchor?anchor.z+Math.sin(angle)*radius:(rng()-.5)*frame.size*.9),g=frame.coordinates(x,z);
      if(cellAt(w,g.wx,g.wz)!==cell||n.sources.some(p=>Math.hypot(p.x-x,p.z-z)<spacing))continue;
      const s=sampleSurface(w,g.wx,g.wz,true,false,true,false,false);if(s.kind||s.height<0)continue;
      const rainfall=macroAt(w,g.wx,g.wz,'rainfall'),evaporation=macroAt(w,g.wx,g.wz,'evaporation'),dimensions=decorativeDimensions(rainfall,evaporation,w.columns);
      source={id:'source:'+cell+':'+k,x,z,level:s.height-.8,weight:.5+rng(),chunkType:rainfall>=1000&&w.moisture[cell]>=.6?22:2,connected:false,attempts:0,...dimensions};break;
    }
    if(!source)continue;n.sources.push(source);if(source.dischargeM3s<=0)continue;
    const streamTargets=n.edges.flatMap(edge=>edge.points.filter((p,k)=>k>0&&k%4===0).map(p=>({...p,edgeId:edge.id})));
    const origin=headEdge?{...headEdge.points[0],edgeId:headEdge.id,headOrigin:true}:null;
    const targets=[...(origin?[origin]:[]),...n.bends,...riverTargets,...streamTargets].filter(p=>p.level<source.level-.8&&Math.hypot(p.x-source.x,p.z-source.z)>=64).sort((a,b)=>(!!b.headOrigin-!!a.headOrigin)||Math.hypot(a.x-source.x,a.z-source.z)-Math.hypot(b.x-source.x,b.z-source.z));
    const samples=new Map(),sample=(x,z)=>{
      const key=x+','+z;if(samples.has(key))return samples.get(key);
      n.routingProbes++;
      const g=frame.coordinates(x,z),s=cellAt(w,g.wx,g.wz)===cell?sampleSurface(w,g.wx,g.wz,true,false,true,false,false):null;samples.set(key,s);return s;
    };
    const connect=function*(candidate){
      yield;
      if(!candidate)return false;
      const hit=firstMainIntersection(candidate,[...n.mainEdges,...n.edges]);
      if(!hit)return false;
      if(hit.points.some(p=>{const g=frame.coordinates(p.x,p.z);return cellAt(w,g.wx,g.wz)!==cell;}))return false;
      const points=templateTributaryProfile(hit.points,source);if(!points)return false;
      const target=hit.target,floor=target.level-(target.depth??source.depth);
      source.connected=true;source.level=points[0].level;source.target={...target,bedHeight:floor};
      configureSpring(source,points,sample,w.options.seed^cell^k);
      n.edges.push({id:source.id,sourceId:source.id,sourceType:'source',sourceKind:source.sourceKind??'rainfall',cell,receiver:w.receiver[cell],points,confluenceBed:floor,confluence:source.target,outletKind:'river',halfWidth:points.at(-1).halfWidth,depth:source.depth,dischargeM3s:source.dischargeM3s,dischargeRole:source.dischargeRole});return true;
    };
    for(let attempt=0;attempt<3;attempt++){
      const target=targets[attempt];if(!target)break;
      source.attempts++;if(yield* connect(decorativePath(source,target,w.options.seed^cell^k,attempt)))break;
      n.rejectedPaths++;
    }
  }
  n.sources=n.sources.filter(s=>s.sourceKind!=='snowmelt'||s.connected);
  for(const source of n.sources)if(!source.connected&&!source.lakeMember){
    yield;
    const sample=(x,z)=>{if(nearMainChannel(n,x,z))return null;const g=frame.coordinates(x,z);return cellAt(w,g.wx,g.wz)===cell?sampleSurface(w,g.wx,g.wz,true,false,true,false,false):null;};
    configureWetPoint(source,sample,w.rainfall[cell],w.evaporation[cell],n.lake?.group.salinity??'fresh');
  }
  n.sources=n.sources.filter(s=>s.connected||s.pool||s.lakeMember);
  n.informationalFlowM3s=0;
  for(const edge of n.edges){
    index(n,edge);
    n.informationalFlowM3s+=edge.dischargeM3s;
  }
  n.retainedWetPoints=n.sources.filter(s=>!s.connected).length;n.unroutedRunoffM3s=runoff;
  n.nodes=n.sources;n.audit={ok:[...n.mainEdges,...n.edges].every(e=>e.points.every(p=>Number.isFinite(p.level))),downhill:[...n.mainEdges,...n.edges].every(e=>e.points.every((p,i)=>!i||p.level<=e.points[i-1].level+1e-6)),conservation:Math.abs(n.allocatedM3s+n.unroutedRunoffM3s-runoff)/Math.max(1e-9,runoff),unroutedRunoffM3s:n.unroutedRunoffM3s};
  n.elapsed=performance.now()-start;c.generationMs+=n.elapsed;c.items.set(cell,n);if(c.items.size>LIMIT){c.items.delete(c.items.keys().next().value);c.evictions++;}return n;
}
// 邻岸查询复用区块九点分类
function probeWater(n,cx,cz){
  let cache=waterProbes.get(n);if(!cache){cache=new Map();waterProbes.set(n,cache);}
  const key=cx+','+cz;if(cache.has(key))return cache.get(key);
  const samples=[],saltLakeIds=new Set(),half=n.frame.size/2;let sea=0,lake=0,saltLake=0,lakeLevel=Infinity;
  for(const dz of [0,32,64])for(const dx of [0,32,64]){
    const g=n.frame.coordinates(cx*64-half+dx,cz*64-half+dz),s=sampleSurface(n.world,g.wx,g.wz,true,false,true,false,dx===32&&dz===32);samples.push(s);
    if(!Number.isFinite(s.water)||s.water<=s.height)continue;
    if(s.kind===1)sea++;
    else if(s.kind===2){lake++;lakeLevel=Math.min(lakeLevel,s.water);if(s.salinity==='salt'){saltLake++;saltLakeIds.add(s.saltLakeId);}}
  }
  const result={samples,sea,lake,saltLake,lakeLevel,saltLakeIds:[...saltLakeIds]};cache.set(key,result);return result;
}
// 基础标签独立于装饰赋值
function rawChunkLabel(network,cx,cz){
  const key=cx+','+cz;if(network.chunkLabels.has(key))return network.chunkLabels.get(key);
  const size=network.frame.size,x0=cx*64-size/2,z0=cz*64-size/2,edges=new Set(),decorativeEdges=new Set();
  for(let z=Math.floor(z0/BIN);z<=Math.floor((z0+64)/BIN);z++)for(let x=Math.floor(x0/BIN);x<=Math.floor((x0+64)/BIN);x++)for(const segment of network.bins.get(x+','+z)??[]){
    const {edge,a,b}=segment,pad=edge.sourceType==='macro'?Math.max(a.halfWidth,b.halfWidth):edge.halfWidth;if(Math.max(a.x,b.x)+pad<x0||Math.min(a.x,b.x)-pad>x0+64||Math.max(a.z,b.z)+pad<z0||Math.min(a.z,b.z)-pad>z0+64)continue;
    const vx=b.x-a.x,vz=b.z-a.z;let lo=0,hi=1,valid=true;
    for(const [p,q] of [[-vx,a.x-x0+pad],[vx,x0+64+pad-a.x],[-vz,a.z-z0+pad],[vz,z0+64+pad-a.z]]){if(Math.abs(p)<1e-9){if(q<0)valid=false;}else{const t=q/p;if(p<0)lo=Math.max(lo,t);else hi=Math.min(hi,t);}}
    if(valid&&lo<=hi)(edge.sourceType==='macro'?edges:decorativeEdges).add(edge.id);
  }
  const {sea,lake,saltLake,lakeLevel,samples}=probeWater(network,cx,cz);
  const sources=network.sources.filter(s=>Math.floor((s.x+size/2)/64)===cx&&Math.floor((s.z+size/2)/64)===cz);
  const naturalWater=sea+lake>0,saline=network.world.lakeSalinityEnabled!==false&&(sea>0||saltLake>0),id=naturalWater?saline?32:2:sources.some(s=>s.chunkType===22)?22:sources.length?2:edges.size?5:null;
  const center=id===null?network.frame.coordinates(x0+32,z0+32):null;
  const type=id===null?landChunkMetadata(network.world,samples,center.wx,center.wz):chunkMetadata(id);
  const label={...type,hasRiver:edges.size>0,hasMacroRiver:edges.size>0,hasDecorativeStream:decorativeEdges.size>0,decorativeEdges:[...decorativeEdges],salinity:naturalWater?saline?'salt':'fresh':null,waterBody:sea?'ocean':lake?'lake':sources.length?'source':null,staticLevel:sea?0:lake?lakeLevel:null,edges:[...edges],sources:sources.map(s=>s.id)};network.chunkLabels.set(key,label);return label;
}
// 缓存周围静水格，河侧64米接平
function mouthLevel(n,cx,cz,x,z,level){
  let cache=mouths.get(n);if(!cache){cache=new Map();mouths.set(n,cache);}
  const key=cx+','+cz;let cells=cache.get(key);
  if(!cells){
    cells=[];const half=n.frame.size/2;
    for(let dz=-1;dz<=1;dz++)for(let dx=-1;dx<=1;dx++){
      const label=rawChunkLabel(n,cx+dx,cz+dz);
      if(label.staticLevel===null)continue;
      const x0=(cx+dx)*CHUNK-half,z0=(cz+dz)*CHUNK-half;
      cells.push({x0,z0,level:label.staticLevel});
    }
    cache.set(key,cells);
  }
  let distance=CHUNK,target=level;
  for(const cell of cells){
    const d=Math.hypot(Math.max(cell.x0-x,0,x-cell.x0-CHUNK),Math.max(cell.z0-z,0,z-cell.z0-CHUNK));
    if(d<distance){distance=d;target=cell.level;}
  }
  const t=clamp(distance/CHUNK),blend=t*t*(3-2*t);
  return target+(level-target)*blend;
}
function channelChunk(n,cx,cz){
  let cache=channels.get(n);if(!cache){cache=new Map();channels.set(n,cache);}
  const key=cx+','+cz;if(cache.has(key))return cache.get(key);
  const x0=cx*CHUNK-n.frame.size/2,z0=cz*CHUNK-n.frame.size/2;
  const value={segments:queryVectorSegments(n.bins,x0,z0,CHUNK,BIN)};cache.set(key,value);return value;
}
// 沿法线寻找区块带外缘
function channelExtent(n,edgeId,x,z,dx,dz){
  const ox=n.frame.size/2,oz=ox;let distance=0;
  for(let k=0;k<128;k++){
    const px=x+dx*(distance+1e-5),pz=z+dz*(distance+1e-5),cx=Math.floor((px+ox)/CHUNK),cz=Math.floor((pz+oz)/CHUNK),label=rawChunkLabel(n,cx,cz);
    if(!label.edges.includes(edgeId))return distance;
    if(!label.isStaticWater&&!label.isRiver&&!(label.hasRiver&&label.waterBody==='source'))return distance;
    const tx=Math.abs(dx)<1e-9?Infinity:((cx+(dx>0?1:0))*CHUNK-ox-x)/dx;
    const tz=Math.abs(dz)<1e-9?Infinity:((cz+(dz>0?1:0))*CHUNK-oz-z)/dz;
    distance=Math.min(tx,tz);if(!Number.isFinite(distance))return CHUNK;
  }
  return distance;
}
function edgeProfile(n,edge){
  let cache=profiles.get(n);if(!cache){cache=new Map();profiles.set(n,cache);}if(cache.has(edge))return cache.get(edge);
  const lengths=[0];for(let k=1;k<edge.points.length;k++)lengths.push(lengths[k-1]+Math.hypot(edge.points[k].x-edge.points[k-1].x,edge.points[k].z-edge.points[k-1].z));
  const total=lengths.at(-1),startWidth=edge.points[0].halfWidth,endWidth=edge.points.at(-1).halfWidth;
  const value={edge,lengths,total,startWidth,endWidth,startRange:Math.min(total/2,Math.max(CHUNK*2,startWidth*2)),endRange:Math.min(total/2,Math.max(CHUNK*2,endWidth*2)),raw:new Map(),smooth:new Map()};cache.set(edge,value);return value;
}
function profilePoint(profile,distance){
  const {edge,lengths,total}=profile,d=clamp(distance,0,total);let k=1;
  while(k<lengths.length-1&&lengths[k]<d)k++;
  const a=edge.points[k-1],b=edge.points[k],length=Math.max(1e-9,lengths[k]-lengths[k-1]),t=(d-lengths[k-1])/length;
  return {x:a.x+(b.x-a.x)*t,z:a.z+(b.z-a.z)*t,level:a.level+(b.level-a.level)*t,nx:-(b.z-a.z)/length,nz:(b.x-a.x)/length};
}
function rawWidth(n,profile,k){
  const key=clamp(k,0,Math.ceil(profile.total/PROFILE_STEP));if(profile.raw.has(key))return profile.raw.get(key);
  const p=profilePoint(profile,key*PROFILE_STEP),value={left:Math.max(8,channelExtent(n,profile.edge.id,p.x,p.z,p.nx,p.nz)-3),right:Math.max(8,channelExtent(n,profile.edge.id,p.x,p.z,-p.nx,-p.nz)-3)};
  profile.raw.set(key,value);return value;
}
function smoothWidth(n,profile,k){
  if(profile.smooth.has(k))return profile.smooth.get(k);
  let left=0,right=0;for(let j=-2;j<=2;j++){const weight=3-Math.abs(j),p=rawWidth(n,profile,k+j);left+=p.left*weight;right+=p.right*weight;}
  const value={left:left/9,right:right/9};profile.smooth.set(k,value);return value;
}
// 区块宽度沿河线连续插值
function channelWidth(n,seg,t){
  if(seg.edge.sourceType!=='macro'||seg.edge.lakeOutflow){const a=seg.a.halfWidth??seg.edge.halfWidth,b=seg.b.halfWidth??seg.edge.halfWidth,width=a+(b-a)*t;return {left:width,right:width};}
  const profile=edgeProfile(n,seg.edge),distance=profile.lengths[seg.pointIndex]+seg.length*t,k=Math.floor(distance/PROFILE_STEP),u=distance/PROFILE_STEP-k;
  const a=smoothWidth(n,profile,k),b=smoothWidth(n,profile,k+1),blend=u*u*(3-2*u);
  let left=a.left+(b.left-a.left)*blend,right=a.right+(b.right-a.right)*blend;
  const start=1-clamp(distance/profile.startRange),end=1-clamp((profile.total-distance)/profile.endRange),head=start*start*(3-2*start),tail=end*end*(3-2*end);
  left+=(profile.startWidth-left)*head+(profile.endWidth-left)*tail;
  right+=(profile.startWidth-right)*head+(profile.endWidth-right)*tail;
  return {left,right};
}
// 沿河线切分64米区块，不采地表
function edgeChunks(n,edge){
  const runs=[],half=n.frame.size/2;
  for(let k=1;k<edge.points.length;k++){
    const a=edge.points[k-1],b=edge.points[k],dx=b.x-a.x,dz=b.z-a.z,cuts=[0,1];
    for(const [origin,delta] of [[a.x,dx],[a.z,dz]]){
      if(Math.abs(delta)<1e-9)continue;
      const low=Math.min(origin,origin+delta),high=Math.max(origin,origin+delta);
      for(let grid=Math.floor((low+half)/CHUNK)+1;grid*CHUNK-half<high;grid++)cuts.push((grid*CHUNK-half-origin)/delta);
    }
    cuts.sort((a,b)=>a-b);
    for(let j=1;j<cuts.length;j++){
      const enter=cuts[j-1],exit=cuts[j];if(exit-enter<1e-9)continue;
      const t=(enter+exit)/2,cx=Math.floor((a.x+dx*t+half)/CHUNK),cz=Math.floor((a.z+dz*t+half)/CHUNK),start=a.level+(b.level-a.level)*enter,end=a.level+(b.level-a.level)*exit,last=runs.at(-1);
      if(last&&last.cx===cx&&last.cz===cz)last.end=end;
      else runs.push({cx,cz,start,end});
    }
  }
  return runs.map(r=>({...r,level:(r.start+r.end)/2}));
}
function waterfallLabels(n){
  if(falls.has(n))return falls.get(n);
  const all=n.mainEdges,routes=new Map(all.map(edge=>[edge.id,edgeChunks(n,edge)])),result=new Map();
  for(const edge of all){
    const runs=routes.get(edge.id);
    for(let k=0;k<runs.length;k++){
      const current=runs[k];let next=runs[k+1];
      if(!next){
        const targetId=edge.sourceType==='macro'?'macro:'+edge.receiver:n.sources.find(s=>s.id===edge.sourceId)?.target?.edgeId,target=routes.get(targetId);
        const same=target?.findIndex(r=>r.cx===current.cx&&r.cz===current.cz)??-1;
        if(same>=0)next=target[same+1];
      }
      if(!next)continue;const drop=current.level-next.level;if(drop<=16)continue;
      const key=current.cx+','+current.cz,previous=result.get(key);
      if(!previous||drop>previous.dropM)result.set(key,{dropM:drop,edgeId:edge.id,downstream:{cx:next.cx,cz:next.cz},upstreamLevel:current.level,downstreamLevel:next.level});
    }
  }
  falls.set(n,result);return result;
}
export function riverChunkLabel(n,cx,cz){
  let cache=labels.get(n);if(!cache){cache=new Map();labels.set(n,cache);}const key=cx+','+cz;if(cache.has(key))return cache.get(key);
  const raw=rawChunkLabel(n,cx,cz);if(raw.isStaticWater||raw.isWetland){cache.set(key,raw);return raw;}
  const x0=cx*CHUNK-n.frame.size/2,z0=cz*CHUNK-n.frame.size/2,edges=new Set();
  for(const seg of channelChunk(n,cx,cz).segments){
    if(seg.edge.sourceType!=='macro')continue;
    const parts=Math.max(1,Math.ceil(seg.length/PROFILE_STEP));
    for(let j=0;j<parts;j++){
      const t0=j/parts,t1=(j+1)/parts,w0=channelWidth(n,seg,t0),w1=channelWidth(n,seg,t1),pad=Math.max(w0.left,w0.right,w1.left,w1.right);
      const ax=seg.a.x+seg.vx*t0,az=seg.a.z+seg.vz*t0,vx=seg.vx/parts,vz=seg.vz/parts;let lo=0,hi=1,valid=true;
      for(const [p,q] of [[-vx,ax-x0+pad],[vx,x0+CHUNK+pad-ax],[-vz,az-z0+pad],[vz,z0+CHUNK+pad-az]]){if(Math.abs(p)<1e-9){if(q<0)valid=false;}else{const t=q/p;if(p<0)lo=Math.max(lo,t);else hi=Math.min(hi,t);}}
      if(valid&&lo<=hi){edges.add(seg.edge.id);break;}
    }
  }
  const fall=edges.size?waterfallLabels(n).get(key):null,decorative=raw.hasDecorativeStream,hasRiver=edges.size>0||decorative;
  let type=fall?chunkMetadata(4):edges.size?chunkMetadata(5):null;
  if(decorative&&!fall){
    const center=n.frame.coordinates(x0+32,z0+32),land=raw.id>=100?raw:landChunkMetadata(n.world,probeWater(n,cx,cz).samples,center.wx,center.wz);
    type=chunkMetadata(land.material*10+5);
  }
  const label={...raw,...(type??{}),saltShore:hasRiver?false:!!raw.saltShore,isRiver:hasRiver,hasRiver,hasMacroRiver:edges.size>0,isWaterfall:!!fall,waterfallDropM:fall?.dropM??0,waterfall:fall??null,waterBody:null,edges:[...edges],sources:[]};cache.set(key,label);return label;
}
function finishSources(s,p,n,natural){
  return applyWetPoints(applySpringSurface(s,p,n,natural),p,n);
}
export function applyTributaries(w,wx,wz,s,n,chunkLabels=true,includeDecorative=true){
  if(chunkLabels!==true&&!n.mainEdges.length&&(!includeDecorative||!n.sources.length))return s;
  const p=n.frame.local(wx,wz),cx=Math.floor((p.x+n.frame.size/2)/64),cz=Math.floor((p.z+n.frame.size/2)/64);
  let label=chunkLabels===true?riverChunkLabel(n,cx,cz):null;if(label)s.chunkCode=label.code;
  if(s.lakeOutlet!==undefined)return s;
  const staticKind=s.kind===1||s.kind===2?s.kind:0,staticWater=s.water;
  const original=s.height;
  const natural=s.height,channel=channelChunk(n,cx,cz);
  if(!channel.segments.length)return includeDecorative?finishSources(s,p,n,original):s;
  const segments=includeDecorative?channel.segments:channel.segments.filter(seg=>seg.edge.sourceType==='macro');
  const {winner:nearest}=carveVectorSegments(s,p,segments,({seg,t,d})=>{
    const width=channelWidth(n,seg,t),riverLevel=seg.a.level+(seg.b.level-seg.a.level)*t;
    const macro=seg.edge.sourceType==='macro',level=macro?mouthLevel(n,cx,cz,p.x,p.z,riverLevel):riverLevel;
    const profile=macro?{level,bed:level-Math.max(0,seg.a.depth+(seg.b.depth-seg.a.depth)*t)}
      :(!staticKind?templateTributarySection(d,width.left,seg.a.depth+(seg.b.depth-seg.a.depth)*t,natural,riverLevel):null)
        ??{level:natural-.12,bed:natural,decorative:true,section:{height:natural,wet:false}};
    const section=seg.edge.lakeOutflow&&!staticKind?bankedChannelSection(d,width.left,profile.bed,level,natural):null;
    return {left:width.left,right:width.right,...profile,...(section?{section,waterAllowed:section.waterAllowed}:{}),protectBed:macro,priority:macro?1:0};
  });
  if(!nearest)return includeDecorative?finishSources(s,p,n,original):s;
  if(chunkLabels)label??=riverChunkLabel(n,cx,cz);
  {
    const {seg,t,shape}=nearest,{a,b,vx,vz,length,edge}=seg;
    const localLevel=shape.level;
    const level=staticKind?Math.max(staticWater,localLevel):localLevel;
    s.water=level;s.kind=staticKind||3;if(!staticKind){s.salinity='fresh';s.saltLakeId=null;}
    const lon=wx/w.circumference*Math.PI*2,lat=-wz/w.circumference*Math.PI*2,east=[-Math.sin(lon),0,Math.cos(lon)],north=[-Math.sin(lat)*Math.cos(lon),Math.cos(lat),-Math.sin(lat)*Math.sin(lon)],v=n.frame.east.map((c,i)=>c*vx/length+n.frame.south[i]*vz/length),dot=a=>a.reduce((sum,c,i)=>sum+c*v[i],0);
    s.river={id:edge.id,source:edge.sourceType,sourceKind:edge.sourceKind,cell:edge.cell,receiver:edge.receiver,dischargeM3s:edge.dischargeM3s,dischargeRole:edge.dischargeRole??'macro',widthM:shape.left+shape.right,widthSource:edge.lakeOutflow?'lake-curve-width':edge.sourceType==='macro'?'smooth-chunk-band':'decorative-rainfall',targetDepthM:edge.sourceType==='macro'?a.depth+(b.depth-a.depth)*t:edge.depth,centerSinkM:Math.max(0,natural-nearest.height),confluenceBed:edge.confluenceBed,waterfallDropM:label?.waterfallDropM??0,directionEast:dot(east),directionNorth:dot(north)};
  }
  return includeDecorative?finishSources(s,p,n,original):s;
}

export function springRockSites(w,cx,cz){
  const cells=new Set(),sites=new Map(),C=w.circumference,center=(cx+.5)*64;
  for(const dx of [-20,84])for(const dz of [-20,84]){
    const x=((cx*64+dx+C/2)%C+C)%C-C/2,z=Math.max(-C/4+.5,Math.min(C/4-.5,cz*64+dz));cells.add(cellAt(w,x,z));
  }
  for(const cell of cells){const network=getTributaries(w,cell);
    for(const source of network.sources)for(const [k,site] of [...springRockPattern(source),...wetPointRocks(source)].entries()){
      const g=network.frame.coordinates(site.x,site.z),x=center+((g.wx-center+C/2)%C+C)%C-C/2,z=g.wz;
      if(Math.floor(x/64)!==cx||Math.floor(z/64)!==cz)continue;
      sites.set(source.id+':'+k,{...site,x,z});
    }
  }
  return [...sites.values()];
}
