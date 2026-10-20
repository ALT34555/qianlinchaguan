import {CHUNK_SIZE} from '../core/config';
import type {ChunkInfo,RiverPoint} from '../systems/world/WorldContract';
import type {MapSummary} from '../systems/world/ChunkCache';
import {snowLine} from '../systems/world/TerrainLayers';
import {isStillWater} from './PlanetAtlasLayer';

type Project=(x:number,z:number)=>[number,number];
type Lookup=(cx:number,cz:number)=>ChunkInfo|null|undefined;
const simplified=new WeakMap<readonly RiverPoint[],Map<number,readonly RiverPoint[]>>();

/** 缩放档缓存曲线，保留宽度变化 */
export function riverLOD(path:readonly RiverPoint[],cell:number):readonly RiverPoint[]{
  if(path.length<3)return path;
  const level=Math.max(0,Math.min(8,Math.ceil(Math.log2(Math.max(1,cell)/12))));
  let cache=simplified.get(path);if(!cache){cache=new Map();simplified.set(path,cache);}
  const found=cache.get(level);if(found)return found;
  const tolerance=CHUNK_SIZE*.5/(12*2**level),keep=new Set([0,path.length-1]),stack=[[0,path.length-1]];
  while(stack.length){
    const [start,end]=stack.pop()!,a=path[start],b=path[end],dx=b.x-a.x,dz=b.z-a.z,length=dx*dx+dz*dz;
    let worst=tolerance,index=-1;
    for(let i=start+1;i<end;i++){
      const p=path[i],t=length?Math.max(0,Math.min(1,((p.x-a.x)*dx+(p.z-a.z)*dz)/length)):0;
      const error=Math.max(Math.hypot(p.x-a.x-t*dx,p.z-a.z-t*dz),Math.abs(p.width-a.width-(b.width-a.width)*t));
      if(error>worst){worst=error;index=i;}
    }
    if(index>=0){keep.add(index);stack.push([start,index],[index,end]);}
  }
  const result=[...keep].sort((a,b)=>a-b).map(i=>path[i]);cache.set(level,result);return result;
}

/** 阴影只读取已有高程 */
export function drawMapRelief(ctx:CanvasRenderingContext2D,info:ChunkInfo,x:number,y:number,w:number,h:number,lookup:Lookup):void{
  const east=lookup(info.cx+1,info.cz),west=lookup(info.cx-1,info.cz),south=lookup(info.cx,info.cz+1),north=lookup(info.cx,info.cz-1);
  if(east&&west&&south&&north){
    const light=(-(east.elevation-west.elevation)*.5*.707-(south.elevation-north.elevation)*.5*.707)*.012;
    if(light){ctx.fillStyle=light>0?`rgba(255,255,255,${Math.min(.38,light*.45)})`:`rgba(0,0,0,${Math.min(.42,-light*.48)})`;ctx.fillRect(x,y,w,h);}
  }
  if(isStillWater(info)){
    const depth=info.lakeLevel!==null?Math.max(0,info.lakeLevel-info.elevation):Math.abs(info.elevation);
    ctx.fillStyle=`rgba(3,20,50,${Math.min(.28,depth/90)})`;ctx.fillRect(x,y,w,h);
  }
  const snow=snowLine(info.temperature);
  if(info.elevation>snow){ctx.fillStyle=`rgba(240,248,255,${Math.min(.32,(info.elevation-snow)/480)})`;ctx.fillRect(x,y,w,h);}
}

/** 节点与真实曲线共用河宽和样式 */
export function drawMapRiver(ctx:CanvasRenderingContext2D,summary:MapSummary,cell:number,project:Project,lookup:Lookup,period=0,summaryAt?:(cx:number,cz:number)=>MapSummary|undefined):void{
  ctx.strokeStyle='#38bdf8';ctx.lineCap='round';ctx.lineJoin='round';
  if(!summary.schematic){
    for(const original of summary.paths){
      const first=original[0];if(!first)continue;
      const at=project(first.x/CHUNK_SIZE,first.z/CHUNK_SIZE),south=project(first.x/CHUNK_SIZE,first.z/CHUNK_SIZE+1);
      const path=riverLOD(original,Math.max(cell,Math.abs(south[1]-at[1])));
      for(let i=1;i<path.length;i++){
        const a=path[i-1],b=path[i],[ax,ay]=project(a.x/CHUNK_SIZE,a.z/CHUNK_SIZE),[bx,by]=project(b.x/CHUNK_SIZE,b.z/CHUNK_SIZE);
        ctx.lineWidth=Math.max(1,(a.width+b.width)*cell/CHUNK_SIZE);
        ctx.beginPath();ctx.moveTo(ax,ay);ctx.lineTo(bx,by);ctx.stroke();
      }
    }
    return;
  }
  const info=summary.info,next=info.downstream;if(!next||info.riverBlocks<=0)return;
  const delta=(x:number)=>period?((x+period/2)%period+period)%period-period/2:x;
  const x=info.cx+.5,z=info.cz+.5,dx=delta(next.cx-info.cx),dz=next.cz-info.cz;
  const anchor=(cx:number,cz:number,fx:number,fz:number):[number,number]=>{
    const own=summaryAt?.(cx,cz);
    let point=own&&!own.schematic?own.paths[0]?.[0]:undefined;
    if(!point)for(let oz=-1;oz<=1&&!point;oz++)for(let ox=-1;ox<=1&&!point;ox++){
      const upstream=summaryAt?.(cx+ox,cz+oz),downstream=upstream?.info.downstream;
      if(upstream&&!upstream.schematic&&downstream&&delta(downstream.cx-cx)===0&&downstream.cz===cz){
        const path=upstream.paths[0];point=path?.[Math.min(32,path.length-1)];
      }
    }
    return point?[fx+delta(point.x/CHUNK_SIZE-fx),point.z/CHUNK_SIZE]:[fx,fz];
  };
  const after=lookup(next.cx,next.cz)?.downstream;
  let ix=0,iz=0;
  for(let oz=-1;oz<=1;oz++)for(let ox=-1;ox<=1;ox++){
    if(!ox&&!oz)continue;
    const source=lookup(info.cx+ox,info.cz+oz);
    if(source?.downstream&&delta(source.downstream.cx-info.cx)===0&&source.downstream.cz===info.cz){ix-=ox;iz-=oz;}
  }
  const tangent=(ax:number,az:number):[number,number]=>{const length=Math.hypot(ax,az);return length?[ax/length,az/length]:[dx,dz];};
  const [tx,tz]=tangent(dx+ix,dz+iz),[ux,uz]=tangent(dx+(after?delta(after.cx-next.cx):dx),dz+(after?after.cz-next.cz:dz));
  const [sx,sz]=anchor(info.cx,info.cz,x,z),[ex,ez]=anchor(next.cx,next.cz,x+dx,z+dz);
  const reach=Math.hypot(dx,dz)/3,[ax,ay]=project(sx,sz),[bx,by]=project(ex,ez);
  const [c1x,c1y]=project(sx+tx*reach,sz+tz*reach),[c2x,c2y]=project(ex-ux*reach,ez-uz*reach);
  ctx.lineWidth=Math.max(1,(info.riverWidth?info.riverWidth/CHUNK_SIZE:info.riverBlocks)*cell);
  ctx.beginPath();ctx.moveTo(ax,ay);ctx.bezierCurveTo(c1x,c1y,c2x,c2y,bx,by);ctx.stroke();
}
