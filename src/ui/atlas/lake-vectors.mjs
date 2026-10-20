import {sampleNaturalHeight} from '../../systems/world/planet-macro/backend.mjs';
import {Heap} from '../../systems/world/planet-macro/heap.mjs';
import {pausePreview} from '../../systems/world/planet-macro/preview-scheduler.mjs';
import {TILE} from './tiles.mjs';

const wrap=(x,n)=>((x%n)+n)%n;
const abort=()=>{throw new DOMException('湖形计算已取消','AbortError');};
export function focusLakeAt(w,wx,wz){
  const span=w.circumference/w.columns,x=Math.floor(wx/span+w.columns/2),y=Math.floor(wz/span+w.rows/2);
  let selected=null,distance=Infinity;
  for(let dz=-2;dz<=2;dz++)for(let dx=-2;dx<=2;dx++){
    const row=y+dz;if(row<0||row>=w.rows)continue;
    const cell=row*w.columns+wrap(x+dx,w.columns);if(w.water[cell]!==2)continue;
    const d=(dx+.5-(wx/span+w.columns/2-x))**2+(dz+.5-(wz/span+w.rows/2-y))**2;
    if(d<distance){distance=d;selected=w.lakeId[cell];}
  }
  return selected;
}
function* retryTask(task){let next;do{next=task.next();if(!next.done)yield next.value;}while(!next.done);return next.value;}
function simplify(points,error){
  if(points.length<5)return points;
  const distance=(p,a,b)=>{const dx=b[0]-a[0],dy=b[1]-a[1],t=Math.max(0,Math.min(1,((p[0]-a[0])*dx+(p[1]-a[1])*dy)/(dx*dx+dy*dy||1)));return Math.hypot(p[0]-a[0]-dx*t,p[1]-a[1]-dy*t);};
  const line=p=>{
    const keep=new Set([0,p.length-1]),stack=[[0,p.length-1]];
    while(stack.length){const [a,b]=stack.pop();let best=error,at=-1;
      for(let i=a+1;i<b;i++){const d=distance(p[i],p[a],p[b]);if(d>best){best=d;at=i;}}
      if(at>=0){keep.add(at);stack.push([a,at],[at,b]);}
    }
    return [...keep].sort((a,b)=>a-b).map(i=>p[i]);
  };
  let split=1;for(let i=2;i<points.length;i++)if(Math.hypot(points[i][0]-points[0][0],points[i][1]-points[0][1])>Math.hypot(points[split][0]-points[0][0],points[split][1]-points[0][1]))split=i;
  return [...line(points.slice(0,split+1)).slice(0,-1),...line([...points.slice(split),points[0]]).slice(0,-1)];
}
function roundRing(points,radius){
  const result=[];
  for(let i=0;i<points.length;i++){
    const a=points[(i+points.length-1)%points.length],b=points[i],c=points[(i+1)%points.length];
    const ab=Math.hypot(a[0]-b[0],a[1]-b[1]),bc=Math.hypot(c[0]-b[0],c[1]-b[1]);
    const r=Math.min(radius,ab/4,bc/4),p=b.map((v,k)=>v+(a[k]-v)*r/(ab||1)),q=b.map((v,k)=>v+(c[k]-v)*r/(bc||1));
    result.push(p,p.map((v,k)=>(v+2*b[k]+q[k])/4),q);
  }
  return result;
}
export function stitchRings(segments,error=0){
  const key=p=>`${p[0].toFixed(4)},${p[1].toFixed(4)}`,adj=new Map(),used=new Set(),rings=[];
  segments.forEach((s,i)=>s.forEach(p=>{const k=key(p);if(!adj.has(k))adj.set(k,[]);adj.get(k).push(i);}));
  for(let i=0;i<segments.length;i++){
    if(used.has(i))continue;
    const ring=[segments[i][0]],start=key(ring[0]);let edge=i,point=ring[0],closed=false;
    while(!used.has(edge)){
      used.add(edge);const pair=segments[edge];point=key(pair[0])===key(point)?pair[1]:pair[0];
      if(key(point)===start){closed=true;break;}ring.push(point);
      edge=(adj.get(key(point))??[]).find(id=>!used.has(id));if(edge===undefined)break;
    }
    if(closed&&ring.length>=3){const simple=simplify(ring,error);if(simple.length>=3)rings.push(error?roundRing(simple,error*.45):simple);}
  }
  return rings;
}
export class LakeVectors{
  constructor(world,{sampler=sampleNaturalHeight,pause=pausePreview}={}){
    this.world=world;this.sampler=sampler;this.pause=pause;this.models=new Map();this.shapes=new Map();this.details=new Map();this.sampled=0;this.coarseVisited=0;
    this.div=Math.max(4,Math.min(32,2**Math.ceil(Math.log2(world.circumference/world.columns/2500))));
    this.step=world.circumference/world.columns/this.div;this.width=world.columns*this.div;this.height=world.rows*this.div;
    this.catalog=new Map();this.nearby=new Map();
    for(let i=0;i<world.water.length;i++)if(world.water[i]===2){const id=world.lakeId[i];if(!this.catalog.has(id))this.catalog.set(id,{id,level:world.lakes[id]?.level??world.spill[i],cells:[]});this.catalog.get(id).cells.push(i);}
    for(const lake of this.catalog.values())for(const cell of lake.cells){const x=cell%world.columns,y=Math.floor(cell/world.columns);
      for(let dz=-2;dz<=2;dz++)for(let dx=-2;dx<=2;dx++){const row=y+dz;if(row<0||row>=world.rows)continue;const key=row*world.columns+wrap(x+dx,world.columns);if(!this.nearby.has(key))this.nearby.set(key,new Set());this.nearby.get(key).add(lake.id);}
    }
  }
  normalize(x,y){
    y=wrap(y,this.height*2);if(y>this.height){y=this.height*2-y;x+=this.width/2;}
    return {x:y===0||y===this.height?0:wrap(x,this.width),y};
  }
  key(x,y){const p=this.normalize(x,y);return p.y*this.width+p.x;}
  natural(x,y){const p=this.normalize(x,y);this.sampled++;return this.sampler(this.world,p.x*this.step-this.world.circumference/2,p.y*this.step-this.world.circumference/4);}
  async execute(task,cancelled,progress){
    let deadline=performance.now()+6;
    try{while(true){if(cancelled())abort();const next=task.next();if(next.done)return next.value;
      if(performance.now()>=deadline){progress?.(next.value);await this.pause();deadline=performance.now()+6;}
    }}finally{task.return();}
  }
  *buildCoarse(lake){
    const start=performance.now(),nodes=[],lookup=new Map(),heap=new Heap(),wet=new Set(),w=this.world;
    let cap=Math.min(lake.level,w.lakeTopology?.source===w.fingerprint?w.lakeTopology.models?.[lake.id]?.level??lake.level:lake.level),escape=null;
    const node=(x,y)=>{const p=this.normalize(x,y),key=this.key(p.x,p.y);let n=lookup.get(key);
      if(!n){n={...p,key,id:nodes.length,h:this.natural(p.x,p.y),cost:Infinity,done:false};nodes.push(n);lookup.set(key,n);}return n;};
    for(const cell of lake.cells){const n=node((cell%w.columns+.5)*this.div,(Math.floor(cell/w.columns)+.5)*this.div);if(n.h<cap){n.cost=n.h;heap.push(n.id,n.cost);}}
    let visited=0;
    while(heap.size){
      const n=nodes[heap.pop()],cost=heap.last;if(n.done||cost!==n.cost)continue;if(cost>=cap)break;
      n.done=true;visited++;if(visited%128===0)yield {stage:'湖形粗采样',lake:lake.id,visited,sampled:this.sampled};
      if(visited>200000)throw Error(`湖 ${lake.id} 粗采样超过预算`);
      const cell=Math.min(w.rows-1,Math.floor(n.y/this.div))*w.columns+Math.floor(n.x/this.div)%w.columns;
      if(w.water[cell]===1&&n.h<0){cap=cost;escape='ocean';break;}
      if(n.x%this.div===this.div/2&&n.y%this.div===this.div/2&&w.water[cell]===2&&w.lakeId[cell]!==lake.id){const level=w.lakes[w.lakeId[cell]]?.level??w.spill[cell];if(level<cap){cap=Math.max(cost,level);escape='lake';if(cost>=cap)break;}}
      for(const [dx,dy] of [[-1,0],[1,0],[0,-1],[0,1]]){
        const q=node(n.x+dx,n.y+dy);if(q.done)continue;let gate=Math.max(cost,q.h);if(gate>=cap||gate>=q.cost)continue;
        for(const t of [.25,.5,.75])gate=Math.max(gate,this.natural(n.x+dx*t,n.y+dy*t));
        if(gate<cap&&gate<q.cost){q.cost=gate;heap.push(q.id,gate);}
      }
    }
    let rootX=lake.cells[0]%w.columns*this.div,minX=Infinity,minY=Infinity,maxX=-Infinity,maxY=-Infinity;
    const cells=new Map();
    for(const n of nodes)if(n.done&&n.h<cap&&n.cost<cap){
      wet.add(n.key);const x=rootX+wrap(n.x-rootX+this.width/2,this.width)-this.width/2;
      minX=Math.min(minX,x);maxX=Math.max(maxX,x);minY=Math.min(minY,n.y);maxY=Math.max(maxY,n.y);
      for(let dy=-1;dy<=0;dy++)for(let dx=-1;dx<=0;dx++)cells.set(`${x+dx},${n.y+dy}`,[x+dx,n.y+dy]);
    }
    this.coarseVisited+=visited;
    const model={id:lake.id,level:cap,originalLevel:lake.level,escape,wet,cells:[...cells.values()],heights:new Map(),rootX,bounds:[minX-1,minY-1,maxX+1,maxY+1],visited,elapsed:performance.now()-start};
    this.models.set(lake.id,model);return model;
  }
  heightAt(model,x,y){
    const key=`${x},${y}`;if(model.heights.has(key))return model.heights.get(key);
    const h=this.natural(x,y);model.heights.set(key,h);return h;
  }
  *contours(model,target){
    const segments=[],depth=Math.max(0,Math.ceil(Math.log2(this.step/target))),level=model.level;
    const value=(x,y)=>{
      const raw=this.heightAt(model,x,y)-level,h=Math.abs(raw)<.001?.001:raw,ix=Math.floor(x),iy=Math.floor(y),wet=(dx,dy)=>model.wet.has(this.key(ix+dx,iy+dy));
      if(Number.isInteger(x)&&Number.isInteger(y))return wet(0,0)?h:Math.max(.001,h);
      if(Number.isInteger(x)&&!wet(0,0)&&!wet(0,1)||Number.isInteger(y)&&!wet(0,0)&&!wet(1,0))return Math.max(.001,h);
      return h;
    };
    const visit=(x,y,size,left,values=null)=>{
      const coords=[[x,y],[x+size,y],[x+size,y+size],[x,y+size]],v=values??coords.map(p=>value(...p)),mask=v.reduce((m,h,i)=>m|(h<0?1<<i:0),0);
      const center=value(x+size/2,y+size/2);
      if(mask===0||mask===15){
        const probes=[center],divisions=2**left;
        for(let k=1;k<divisions;k++){const d=size*k/divisions;probes.push(value(x+d,y),value(x+size,y+d),value(x+d,y+size),value(x,y+d));}
        if(probes.every(h=>(h<0)===(mask===15)))return;
      }
      if(left>0){const d=size/2;for(const [dx,dy] of [[0,0],[d,0],[d,d],[0,d]]){
        const sx=x+dx,sy=y+dy,sub=[[sx,sy],[sx+d,sy],[sx+d,sy+d],[sx,sy+d]].map(p=>{const corner=coords.findIndex(q=>q[0]===p[0]&&q[1]===p[1]);return corner>=0?v[corner]:value(...p);});
        visit(sx,sy,d,left-1,sub);
      }return;}
      const crosses=[];for(let i=0;i<4;i++){const j=(i+1)%4;if((v[i]<0)===(v[j]<0))continue;const t=v[i]/(v[i]-v[j]);crosses.push({edge:i,p:coords[i].map((a,k)=>(a+(coords[j][k]-a)*t)*this.step)});}
      if(crosses.length===2)segments.push(crosses.map(c=>c.p));
      else if(crosses.length===4){const pairs=(mask===5)===(center<0)?[[0,1],[2,3]]:[[0,3],[1,2]];for(const [a,b] of pairs)segments.push([crosses[a].p,crosses[b].p]);}
    };
    for(let i=0;i<model.cells.length;i++){
      const [x,y]=model.cells[i];if(y<0||y>=this.height)continue;
      visit(x,y,1,depth,[[x,y],[x+1,y],[x+1,y+1],[x,y+1]].map(p=>value(...p,true)));
      if(i%32===0)yield {stage:'湖形岸边加点',lake:model.id,visited:i,sampled:this.sampled};
    }
    const ends=new Map();for(const segment of segments)for(const p of segment){const k=p.map(v=>v.toFixed(4)).join(',');ends.set(k,(ends.get(k)??0)+1);}
    model.diagnostics={segments:segments.length,open:[...ends].filter(([k,n])=>n!==2).slice(0,15),badEnds:[...ends.values()].filter(n=>n!==2).length};
    return stitchRings(segments,target*.4).map(r=>r.map(([x,y])=>[x-this.world.circumference/2,y-this.world.circumference/4]));
  }
  idsFor(tile){
    const w=this.world,n=2**tile.z,a=Math.floor(tile.x/n*w.columns),b=Math.floor((tile.x+1)/n*w.columns),c=Math.floor(tile.y/n*w.columns),d=Math.floor((tile.y+1)/n*w.columns),ids=new Set();
    for(let y=c;y<=Math.min(w.rows-1,d);y++)for(let x=a;x<=b;x++)for(const id of this.nearby.get(y*w.columns+wrap(x,w.columns))??[])ids.add(id);
    for(const model of this.models.values()){const box=this.tileBox(model,tile);if(box)ids.add(model.id);}
    return [...ids].sort((a,b)=>a-b);
  }
  tileBox(model,tile){
    const n=2**tile.z,span=this.width/n,cx=(tile.x+.5)*span,x=model.rootX+wrap(cx-model.rootX+this.width/2,this.width)-this.width/2,top=tile.y*span;
    const [a,b,c,d]=model.bounds;return x+span/2<a||x-span/2>c||top+span<b||top>d?null:{left:x-span/2,top,span};
  }
  // L7 起给粗矢量湖形
  async prepare(tile,cancelled=()=>false,progress){
    const shapes=[];if(tile.z<7)return shapes;
    for(const id of this.idsFor(tile)){
      if(cancelled())abort();const lake=this.catalog.get(id);if(!lake)continue;
      const model=this.models.get(id)??await this.execute(retryTask(this.buildCoarse(lake)),cancelled,progress);
      if(!this.tileBox(model,tile))continue;
      const z=Math.min(12,tile.z),key=`${id}/${z}`;
      if(!this.shapes.has(key)){
        const target=Math.max(this.step/4,this.world.circumference/2**z/TILE*1.5);
        const rings=await this.execute(this.contours(model,target),cancelled,progress);this.shapes.set(key,{id,z,level:model.level,rings});
        while(this.shapes.size>96)this.shapes.delete(this.shapes.keys().next().value);
      }
      shapes.push(this.shapes.get(key));
    }
    const retained=new Set(shapes.map(s=>s.id));
    for(const [id,model] of this.models){
      while(model.heights.size>65536)model.heights.delete(model.heights.keys().next().value);
      if(this.models.size<=8||retained.has(id))continue;
      this.models.delete(id);
      for(const [key,shape] of this.shapes)if(shape.id===id)this.shapes.delete(key);
      for(const [key,detail] of this.details)if(detail.id===id)this.details.delete(key);
    }
    return shapes;
  }
  *detailSteps(model,tile){
    const box=this.tileBox(model,tile);if(!box)return null;
    const step=Math.max(64,Math.min(256,this.world.circumference/2**tile.z/TILE)),pad=this.step*1.5;
    const left=Math.floor((box.left*this.step-pad)/step)*step,top=Math.max(0,Math.floor((box.top*this.step-pad)/step)*step);
    const columns=Math.ceil((box.left*this.step+box.span*this.step+pad-left)/step)+1,rows=Math.ceil((Math.min(this.height*this.step,(box.top+box.span)*this.step+pad)-top)/step)+1;
    const heights=new Float32Array(columns*rows),wet=new Uint8Array(columns*rows),queue=new Uint32Array(columns*rows);let head=0,tail=0;
    for(let y=0;y<rows;y++){
      for(let x=0;x<columns;x++)heights[y*columns+x]=this.natural((left+x*step)/this.step,(top+y*step)/this.step);
      if(y%8===0)yield {stage:'聚焦湖岸采样',lake:model.id,visited:y*columns,sampled:this.sampled};
    }
    const add=i=>{if(i<0||i>=wet.length||wet[i]||heights[i]>=model.level)return;wet[i]=1;queue[tail++]=i;};
    for(const key of model.wet){const raw=key%this.width,gy=Math.floor(key/this.width),gx=model.rootX+wrap(raw-model.rootX+this.width/2,this.width)-this.width/2;
      const x=Math.round((gx*this.step-left)/step),y=Math.round((gy*this.step-top)/step);if(x>=0&&x<columns&&y>=0&&y<rows)add(y*columns+x);
    }
    while(head<tail){const i=queue[head++],x=i%columns,y=Math.floor(i/columns);
      for(const [dx,dy] of [[-1,0],[1,0],[0,-1],[0,1]]){const xx=x+dx,yy=y+dy;if(xx<0||xx>=columns||yy<0||yy>=rows)continue;const j=yy*columns+xx;if(wet[j]||heights[j]>=model.level)continue;
        if(this.natural((left+(x+dx*.5)*step)/this.step,(top+(y+dy*.5)*step)/this.step)>=model.level)continue;add(j);
      }
      if(head%512===0)yield {stage:'聚焦湖岸连通',lake:model.id,visited:head,sampled:this.sampled};
    }
    return {id:model.id,left,top,step,columns,rows,wet,samples:columns*rows,visited:tail};
  }
  async prepareDetail(tile,focusId,cancelled=()=>false,progress){
    this.activeDetail=null;if(tile.z<13||focusId===null)return;
    const model=this.models.get(focusId);if(!model)return;
    const key=`${focusId}/${tile.key}`;
    if(!this.details.has(key)){const detail=await this.execute(this.detailSteps(model,tile),cancelled,progress);if(detail)this.details.set(key,detail);while(this.details.size>12)this.details.delete(this.details.keys().next().value);}
    this.activeDetail=this.details.get(key)??null;
  }
  surface(wx,wz,height,focusId){
    const w=this.world,gx=(wx+w.circumference/2)/this.step,gy=(wz+w.circumference/4)/this.step;
    for(const model of this.models.values()){
      const x=model.rootX+wrap(gx-model.rootX+this.width/2,this.width)-this.width/2,[a,b,c,d]=model.bounds;
      if(x<a||x>c||gy<b||gy>d||height>=model.level)continue;
      const ix=Math.floor(x),iy=Math.floor(gy);let connected=[[0,0],[1,0],[1,1],[0,1]].some(([dx,dy])=>model.wet.has(this.key(ix+dx,iy+dy)));
      if(model.id!==focusId)continue;
      const detail=this.activeDetail;
      if(detail?.id===model.id){
        const px=(x*this.step-detail.left)/detail.step,py=(gy*this.step-detail.top)/detail.step,col=Math.floor(px),row=Math.floor(py);
        if(col>=0&&row>=0&&col<detail.columns-1&&row<detail.rows-1)connected=[[0,0],[1,0],[1,1],[0,1]].some(([dx,dy])=>detail.wet[(row+dy)*detail.columns+col+dx]);
      }
      if(!connected)continue;
      return {height,water:model.level,kind:2,salinity:w.lakes[model.id]?.salinity??'fresh',saltLakeId:w.lakes[model.id]?.salinity==='salt'?model.id:null};
    }
    return {height,water:-Infinity,kind:0,salinity:null,saltLakeId:null};
  }
  stats(){return {models:this.models.size,shapes:this.shapes.size,details:this.details.size,detail:this.activeDetail?{id:this.activeDetail.id,samples:this.activeDetail.samples,step:this.activeDetail.step}:null,sampled:this.sampled,coarseVisited:this.coarseVisited,step:this.step,vertices:[...this.shapes.values()].reduce((sum,s)=>sum+s.rings.reduce((n,r)=>n+r.length,0),0)};}
}
export function lakeMask(shapes,tile,w,excluded=null){
  const mask=new Float32Array(TILE*TILE),n=2**tile.z,span=w.circumference/n,meters=span/TILE,center=(tile.x+.5)*span-w.circumference/2,top=tile.y*span-w.circumference/4;
  for(const shape of shapes){
    if(shape.id===excluded)continue;
    const rings=shape.rings.map(r=>r.map(([wx,wz])=>[(center+wrap(wx-center+w.circumference/2,w.circumference)-w.circumference/2-(center-span/2))/meters,(wz-top)/meters]));
    for(let row=0;row<TILE;row++)for(const offset of [.25,.75]){
      const y=row+offset,cross=[];
      for(const ring of rings)for(let j=0,i=ring.length-1;j<ring.length;i=j++){
        const a=ring[i],b=ring[j];if((a[1]>y)===(b[1]>y))continue;cross.push(a[0]+(y-a[1])*(b[0]-a[0])/(b[1]-a[1]));
      }
      cross.sort((a,b)=>a-b);
      for(let k=0;k+1<cross.length;k+=2){const a=Math.max(0,cross[k]),b=Math.min(TILE,cross[k+1]);for(let x=Math.floor(a);x<Math.ceil(b);x++){const coverage=Math.max(0,Math.min(x+1,b)-Math.max(x,a))*.5;if(x>=0&&x<TILE)mask[row*TILE+x]=Math.min(1,mask[row*TILE+x]+coverage);}}
    }
  }
  return mask;
}
