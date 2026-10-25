import {getTributaries} from './source-rivers.mjs';
import {cellAt} from './sphere-frame.mjs';
import {rasterPolygon,reduceAxisEdges,intersectPolygons} from './shoreline-raster.mjs';
import {samplePlanetSurface} from './surface.mjs';
import {terrainHeightAt,terrainDiagonal} from './terrain-geometry.mjs';
import {waterVertexLevel,clipShore,iceVertexLevel,iceVertexCoverage,clipIce} from './water-geometry.mjs';

export const SHORELINE_MODEL_REVISION='centred-triangles-v2';
export function createPlanetShorelineSource(world,options={}) {
  const C=world.circumference;
  const wrap=x=>((x+C/2)%C+C)%C-C/2;
  const source={worldRevision:options.worldRevision??world.fingerprint,hydrologyRevision:options.hydrologyRevision??'natural',
    seed:world.options.seed,circumference:C,waterOffset:-.02,column:options.column??((x,z)=>{
      let wx=wrap(x+.5),wz=wrap(z+.5);
      if(wz>C/4){wz=C/2-wz;wx=wrap(wx+C/2);}
      if(wz<-C/4){wz=-C/2-wz;wx=wrap(wx+C/2);}
      const s=samplePlanetSurface(world,wx,wz,true,false,false,options.includeDecorative!==false);
      const kind=s.kind===1?'sea':s.kind===2?'lake':s.river?'river':'unknown';
      return {bed:Math.fround(s.height),water:Math.fround(s.water)>Math.fround(s.height)?Math.fround(s.water):-Infinity,
        waterBodyKind:kind,waterBodyId:kind==='sea'?'sea':kind==='lake'?`lake:${s.lakeTemplateId??world.lakeId[s.cell]}`:s.river?.id??null};
    })};
  const indices=new Map();let indexBytes=0;
  // 排除装饰水域时保守返回可能相交
  source.mayCross=options.includeDecorative===false?()=>true:b=>{
    const wx=wrap((b.minX+b.maxX)/2),wz=Math.max(-C/4+.5,Math.min(C/4-.5,(b.minZ+b.maxZ)/2));
    const network=getTributaries(world,cellAt(world,wx,wz));let entry=indices.get(network);
    if(!entry) {
      const values=[];
      for(const edge of options.includeDecorative===false?network.mainEdges:[...network.mainEdges,...network.edges])for(let i=1;i<edge.points.length;i++) {
        const a=edge.points[i-1],c=edge.points[i],pad=Math.max(4,(edge.halfWidth??1)*2+4);
        values.push(Math.min(a.x,c.x)-pad,Math.min(a.z,c.z)-pad,Math.max(a.x,c.x)+pad,Math.max(a.z,c.z)+pad);
      }
      const boxes=Float64Array.from(values);entry={boxes,frame:network.frame};
      if(boxes.byteLength<=2*1024*1024) {
        while(indices.size&&(indexBytes+boxes.byteLength>2*1024*1024||indices.size>=8)){const key=indices.keys().next().value;indexBytes-=indices.get(key).boxes.byteLength;indices.delete(key);}
        indices.set(network,entry);indexBytes+=boxes.byteLength;
      }
    }
    const f=entry.frame,p0=f.local(b.minX,b.minZ),p1=f.local(b.minX,b.maxZ),p2=f.local(b.maxX,b.minZ),p3=f.local(b.maxX,b.maxZ);
    const pad=((b.maxX-b.minX)**2+(b.maxZ-b.minZ)**2)/Math.max(1,world.radius)+2;
    const x0=Math.min(p0.x,p1.x,p2.x,p3.x)-pad,z0=Math.min(p0.z,p1.z,p2.z,p3.z)-pad,x1=Math.max(p0.x,p1.x,p2.x,p3.x)+pad,z1=Math.max(p0.z,p1.z,p2.z,p3.z)+pad;
    for(let i=0;i<entry.boxes.length;i+=4)if(entry.boxes[i]<=x1&&entry.boxes[i+2]>=x0&&entry.boxes[i+1]<=z1&&entry.boxes[i+3]>=z0)return true;
    return false;
  };
  source.hintStats=()=>({bytes:indexBytes,entries:indices.size});source.clearHints=()=>{indices.clear();indexBytes=0;};
  return source;
}
function abort() {const e=new Error('Shoreline query cancelled');e.name='AbortError';throw e;}
function clipBox(poly,b) {
  for(const [key,limit,sign] of [['x',b.minX,1],['x',b.maxX,-1],['z',b.minZ,1],['z',b.maxZ,-1]]) {
    const out=[];
    for(let i=0;i<poly.length;i++) {
      const a=poly[i],c=poly[(i+1)%poly.length],da=(a[key]-limit)*sign,dc=(c[key]-limit)*sign;
      if(da>=0)out.push(a);
      if((da>=0)!==(dc>=0)){const t=da/(da-dc);out.push({x:a.x+(c.x-a.x)*t,z:a.z+(c.z-a.z)*t,depth:a.depth+(c.depth-a.depth)*t});}
    }
    poly=out;
  }
  return poly;
}
export function createShorelineQuery(source,{maxCacheBytes=2*1024*1024,maxSourceBytes=2*1024*1024,maxSourceEntries=65536}={}) {
  const cache=new Map(),sourceCache=new Map(),originalColumn=source.column;
  let bytes=0,hits=0,misses=0,sourceBytes=0,sourceMisses=0,sourceHits=0;
  source={...source,column(x,z){
    const key=`${x},${z}`,hit=sourceCache.get(key);
    if(hit){sourceHits++;sourceCache.delete(key);sourceCache.set(key,hit);return hit.value;}
    sourceMisses++;const value=originalColumn(x,z),size=96;
    while(sourceCache.size&&(sourceBytes+size>maxSourceBytes||sourceCache.size>=maxSourceEntries)){const k=sourceCache.keys().next().value;sourceBytes-=sourceCache.get(k).size;sourceCache.delete(k);}
    if(size<=maxSourceBytes){sourceCache.set(key,{value,size});sourceBytes+=size;}return value;
  }};
  const clone=value=>structuredClone(value);
  function* steps(request) {
    const start=performance.now(),sourceStart=sourceMisses,sourceHitStart=sourceHits,check=()=>{if(request.cancelled?.())abort();};check();
    if(request.worldRevision!==source.worldRevision||request.hydrologyRevision!==source.hydrologyRevision)
      throw Object.assign(new Error('Stale shoreline revision'),{name:'StaleShorelineRevision'});
    const b=request.bounds;
    if(request.coordinateSystem!=='world-xz-metres'||![b.minX,b.minZ,b.maxX,b.maxZ,request.targetErrorM,request.maxVertices].every(Number.isFinite)
      ||b.minX>=b.maxX||b.minZ>=b.maxZ||request.targetErrorM<=0||request.maxVertices<2
      ||Math.max(...[b.minX,b.minZ,b.maxX,b.maxZ].map(Math.abs))>1e12)throw Error('Invalid shoreline request');
    const maxCells=request.maxCells??16384;
    if(!Number.isInteger(maxCells)||maxCells<1||!Number.isInteger(request.maxVertices))throw Error('Invalid shoreline budget');
    const minStep=Math.max(1,2**Math.floor(Math.log2(Math.max(1,request.minStepM??1))));
    const maxStep=Math.max(minStep,2**Math.floor(Math.log2(Math.min(2048,request.maxStepM??1))));
    const rasterSide=request.rasterSide??0,skipBoundaries=!!request.skipBoundaries;
    if(!Number.isFinite(maxStep)||!Number.isInteger(rasterSide)||rasterSide<0||rasterSide>512)throw Error('Invalid shoreline resolution');
    const key=JSON.stringify([SHORELINE_MODEL_REVISION,b,request.targetErrorM,request.maxVertices,maxCells,minStep,maxStep,rasterSide,request.phase,!!request.disableHints,skipBoundaries]);
    if(cache.has(key)) {
      hits++;const hit=cache.get(key);cache.delete(key);cache.set(key,hit);const result=clone(hit.value);
      result.stats={...result.stats,cacheHit:true,samples:0,columnProbes:0,sourceCacheHits:0,sourceCacheBytes:sourceBytes,cells:0,elapsedMs:performance.now()-start,cacheBytes:bytes};return result;
    }
    misses++;
    const origin={x:Math.floor(b.minX),z:Math.floor(b.minZ)},localBounds={minX:b.minX-origin.x,maxX:b.maxX-origin.x,minZ:b.minZ-origin.z,maxZ:b.maxZ-origin.z},columns=new Map(),vertexCache=new Map(),edges=new Map();let cells=0,samples=0,degraded=false,modelDegraded=false;
    const area=rasterSide?new Float64Array(rasterSide*rasterSide):null;
    const iceArea=area&&request.phase==='surface'?new Float64Array(area.length):null,axisEdges=new Map();
    const iceTop=(x,z)=>column(x,z).iceLevel??-Infinity,iceCoverage=(x,z)=>column(x,z).iceCoverage??0;
    const column=(x,z)=>{
      x=Math.floor(x);z=Math.floor(z);const k=`${x},${z}`;
      if(!columns.has(k)){check();columns.set(k,source.column(x,z));samples++;}
      return columns.get(k);
    };
    const bed=(x,z)=>column(x,z).bed,water=(x,z)=>column(x,z).water;
    const pointKey=p=>`${Math.round(p.x*1e8)},${Math.round(p.z*1e8)}`;
    const onBox=(a,c)=>['minX','maxX'].some(k=>Math.abs(a.x-localBounds[k])<1e-9&&Math.abs(c.x-localBounds[k])<1e-9)
      ||['minZ','maxZ'].some(k=>Math.abs(a.z-localBounds[k])<1e-9&&Math.abs(c.z-localBounds[k])<1e-9);
    const vertex=(px,pz)=>{
      const vk=`${px},${pz}`;let cached=vertexCache.get(vk);if(cached)return cached;
      if(minStep>=16) {
        const c=column(px-.5,pz-.5),ground=c.bed,offset=source.waterOffset??-.02;
        const y=Number.isFinite(c.water)&&ground<c.water?Math.fround(c.water-offset):-Infinity;
        const top=request.phase==='surface'&&Number.isFinite(c.iceLevel)?c.iceLevel:-Infinity;
        cached={x:px-origin.x,z:pz-origin.z,y,depth:y-ground,ground,iceTop:top,iceCoverage:request.phase==='surface'?(c.iceCoverage??0):0};
      } else {
        const y=waterVertexLevel(px,pz,bed,water,source.seed,false,source.waterOffset??-.02);
        const ground=terrainHeightAt(px,pz,source.seed,bed);
        const top=request.phase==='surface'?iceVertexLevel(px,pz,iceTop):-Infinity;
        cached={x:px-origin.x,z:pz-origin.z,y,depth:y-ground,ground,iceTop:top,
          iceCoverage:request.phase==='surface'?iceVertexCoverage(px,pz,iceCoverage):0};
      }
      vertexCache.set(vk,cached);return cached;
    };
    const baseMin=Math.max(minStep,2**Math.floor(Math.log2(Math.max(1,request.targetErrorM/2))));
    function* visit(ix,iz,step) {
      check();if(cells>=maxCells){degraded=true;return;}cells++;
      const x=ix+.5,z=iz+.5,coords=[[x,z],[x,z+step],[x+step,z+step],[x+step,z]];
      const verts=coords.map(([px,pz])=>vertex(px,pz));
      if(step>minStep) {
        const wet4=verts.map(v=>(Number.isFinite(v.y)&&v.depth>0)||(v.iceCoverage>=.5&&v.iceTop>v.ground));
        let mixed=wet4.some(Boolean)&&!wet4.every(Boolean);
        const needHint=!mixed||step<=baseMin;
        const hint=needHint&&!request.disableHints&&(source.mayCross?.({minX:x,minZ:z,maxX:x+step,maxZ:z+step})??false);
        if(!mixed&&!hint) {
          const skipMid=minStep>=16&&(wet4[0]?verts.every(v=>v.depth>25):verts.every(v=>v.ground>25));
          if(!skipMid) {
            const mids=[[x+step/2,z],[x,z+step/2],[x+step/2,z+step/2],[x+step,z+step/2],[x+step/2,z+step]].map(([px,pz])=>vertex(px,pz));
            const mWet=mids.map(v=>(Number.isFinite(v.y)&&v.depth>0)||(v.iceCoverage>=.5&&v.iceTop>v.ground));
            mixed=mWet.some(w=>w!==wet4[0]);
          }
        }
        const minimum=hint?minStep:baseMin;
        if((mixed||hint)&&step>minimum) {
          for(const [dx,dz] of [[0,0],[step/2,0],[0,step/2],[step/2,step/2]]) {
            if(degraded)break;yield* visit(ix+dx,iz+dz,step/2);
          }
          return;
        }
        modelDegraded=true;
      } else if(step>1) {
        modelDegraded=true;
      }
      const level=water(ix,iz),finite=verts.map(v=>v.y).filter(Number.isFinite);
      if(!finite.length&&!verts.some(v=>Number.isFinite(v.iceTop)&&v.iceCoverage>=.5)){if(cells%64===0)yield;return;}
      const fallback=Number.isFinite(level)&&bed(ix,iz)<level?level-(source.waterOffset??-.02):Math.min(...finite);
      for(let i=0;i<verts.length;i++)if(!Number.isFinite(verts[i].y)) {
        const g=verts[i].ground??terrainHeightAt(coords[i][0],coords[i][1],source.seed,bed);
        verts[i]={...verts[i],y:fallback,depth:fallback-g};
      }
      const metadata=skipBoundaries?null:([column(ix,iz),column(ix-1,iz),column(ix,iz-1),column(ix-1,iz-1)].find(s=>Number.isFinite(s.water))??column(ix,iz));
      for(const tri of terrainDiagonal(x,z,source.seed)?[[0,1,3],[1,2,3]]:[[0,1,2],[0,2,3]]) {
        const sourcePoly=clipShore(tri.map(i=>verts[i]));
        const liquid=sourcePoly.every(v=>!Number.isFinite(v.y)||v.depth===0)?[]:clipBox(sourcePoly,localBounds);
        const iceVerts=tri.map(i=>{const v=verts[i],top=Number.isFinite(v.iceTop)?v.iceTop:Math.min(...verts.map(p=>p.iceTop).filter(Number.isFinite));
          return {x:v.x,z:v.z,y:top,bottom:top,depth:top-v.ground,coverage:v.iceCoverage};});
        const frozen=request.phase==='surface'?clipBox(clipIce(clipIce(iceVerts,'coverage'),'depth'),localBounds):[];
        if(area){rasterPolygon(area,liquid,localBounds,rasterSide);rasterPolygon(area,frozen,localBounds,rasterSide);
          if(liquid.length&&frozen.length)rasterPolygon(area,intersectPolygons(liquid,frozen),localBounds,rasterSide,-1);}
        if(iceArea)rasterPolygon(iceArea,frozen,localBounds,rasterSide);
        if(skipBoundaries)continue;
        for(const [phase,poly] of [['liquid',liquid],['ice',frozen]]) {
        for(let i=0;i<poly.length;i++) {
          const a=poly[i],c=poly[(i+1)%poly.length],ak=pointKey(a),ck=pointKey(c);
          if(ak===ck||onBox(a,c))continue;
          const edge={a,c,ak:`${phase}:${ak}`,ck:`${phase}:${ck}`,phase,metadata,isShore:a.depth===0&&c.depth===0};
          if(maxStep>1&&(Math.abs(a.x-c.x)<1e-9||Math.abs(a.z-c.z)<1e-9)) {
            edge.axis=Math.abs(a.x-c.x)<1e-9?'x':'z';
            const axisKey=`${phase}:${edge.axis}:${Math.round(a[edge.axis]*1e8)}`;
            if(!axisEdges.has(axisKey))axisEdges.set(axisKey,[]);axisEdges.get(axisKey).push(edge);
          }else {
            const forward=`${edge.ak}/${edge.ck}`,reverse=`${edge.ck}/${edge.ak}`;
            if(edges.has(reverse))edges.delete(reverse);else edges.set(forward,edge);
          }
        }
      }
      }
      if(!skipBoundaries&&edges.size>request.maxVertices*4){degraded=true;return;}
      if(cells%64===0)yield;
    }
    outer:for(let iz=Math.floor((b.minZ-.5)/maxStep)*maxStep;iz+.5<b.maxZ;iz+=maxStep)
      for(let ix=Math.floor((b.minX-.5)/maxStep)*maxStep;ix+.5<b.maxX;ix+=maxStep) {
        yield* visit(ix,iz,maxStep);if(degraded)break outer;
      }
    const boundaries=[];let vertices=0;
    if(!skipBoundaries) {
      reduceAxisEdges(axisEdges,e=>{e.ak=`${e.phase}:${pointKey(e.a)}`;e.ck=`${e.phase}:${pointKey(e.c)}`;edges.set(`${e.ak}/${e.ck}`,e);});
      if(degraded)for(const [key,edge] of edges)if(!edge.isShore)edges.delete(key);
      const outgoing=new Map();for(const [key,e] of edges){if(!outgoing.has(e.ak))outgoing.set(e.ak,[]);outgoing.get(e.ak).push(key);}
      const incoming=new Set([...edges.values()].map(e=>e.ck)),starts=[...edges.values()].filter(e=>!incoming.has(e.ak));
      while(edges.size) {
        check();let first;while(starts.length&&!first){const candidate=starts.pop();if(edges.has(`${candidate.ak}/${candidate.ck}`))first=candidate;}
        first??=edges.values().next().value;const points=[first.a];let edge=first,closed=false;
        while(edge) {
          edges.delete(`${edge.ak}/${edge.ck}`);points.push(edge.c);
          if(edge.ck===first.ak){closed=true;break;}
          const direction={x:edge.c.x-edge.a.x,z:edge.c.z-edge.a.z};
          const turn=candidate=>{const dx=candidate.c.x-candidate.a.x,dz=candidate.c.z-candidate.a.z;
            return Math.atan2(direction.x*dz-direction.z*dx,direction.x*dx+direction.z*dz);};
          const next=(outgoing.get(edge.ck)??[]).filter(k=>{
            const candidate=edges.get(k);return candidate&&candidate.metadata.waterBodyId===first.metadata.waterBodyId&&candidate.metadata.waterBodyKind===first.metadata.waterBodyKind;
          }).sort((a,b)=>turn(edges.get(a))-turn(edges.get(b)))[0];edge=next?edges.get(next):null;
        }
        if(vertices+points.length>request.maxVertices){degraded=true;break;}
        const coordinates=new Float64Array(points.length*2);
        for(let i=0;i<points.length;i++){coordinates[i*2]=points[i].x;coordinates[i*2+1]=points[i].z;}
        boundaries.push({phase:first.phase,waterBodyId:first.metadata.waterBodyId??null,waterBodyKind:first.metadata.waterBodyKind??'unknown',closed,coordinates});vertices+=points.length;
      }
    }
    const result={modelRevision:SHORELINE_MODEL_REVISION,worldRevision:source.worldRevision,hydrologyRevision:source.hydrologyRevision,
      coordinateSystem:'world-xz-metres',origin,boundaries,targetErrorM:request.targetErrorM,
      error:{reference:'fixed-mountain-triangles',maxErrorM:null,status:'unknown',continuousSurfaceMaxErrorM:null},
      budgetDegraded:degraded,modelDegraded,complete:!degraded,
      ...(iceArea?{iceMask:Uint8Array.from(iceArea,v=>Math.round(Math.max(0,Math.min(1,v))*255))}:{}),
      ...(area?{waterMask:Uint8Array.from(area,v=>Math.round(Math.max(0,Math.min(1,v))*255)),rasterSide}:{}),stats:{samples:sourceMisses-sourceStart,columnProbes:samples,sourceCacheHits:sourceHits-sourceHitStart,sourceCacheBytes:sourceBytes,hintCacheBytes:source.hintStats?.().bytes??0,cells,vertices,elapsedMs:performance.now()-start,cacheHit:false,cacheBytes:bytes}};
    check();
    if(!degraded) {
      const size=256+boundaries.reduce((n,v)=>n+v.coordinates.byteLength+64,0)+(result.waterMask?.byteLength??0)+(result.iceMask?.byteLength??0);
      if(size<=maxCacheBytes){while(bytes+size>maxCacheBytes&&cache.size){const k=cache.keys().next().value;bytes-=cache.get(k).size;cache.delete(k);}cache.set(key,{value:clone(result),size});bytes+=size;}
    }
    result.stats.cacheBytes=bytes;return result;
  }
  return {steps,query(request){const it=steps(request);let next;do{next=it.next();}while(!next.done);return next.value;},
    async queryAsync(request,{yieldControl=()=>new Promise(resolve=>setTimeout(resolve,0))}={}) {
      const it=steps(request);let next;do{next=it.next();if(!next.done)await yieldControl();}while(!next.done);return next.value;
    },clear(){cache.clear();sourceCache.clear();source.clearHints?.();bytes=0;sourceBytes=0;},stats(){return {sourceCalls:sourceMisses,sourceCacheBytes:sourceBytes,sourceCacheEntries:sourceCache.size,maxSourceBytes,cacheBytes:bytes,cacheEntries:cache.size,hits,misses,maxCacheBytes};}};
}
