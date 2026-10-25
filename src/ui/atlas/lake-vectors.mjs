import {lakeTemplateCatalog,lakeTemplateTile,sampleTemplateLake,templateCell} from '../../systems/world/planet-macro/lake-templates.mjs';
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
  constructor(world){this.world=world;this.models=new Map();this.shapes=new Map();this.details=new Map();this.catalog=lakeTemplateCatalog(world).lakes;this.nearby=new Map();this.activeDetail=null;this.sampled=0;this.coarseVisited=0;this.step=world.circumference/world.columns;}
  idsFor(tile){
    if(tile.z<7){
      const w=this.world,n=2**tile.z,ids=[];
      for(const lake of this.catalog.values()){
        const visible=lake.cells.some(cell=>{
          for(let dz=-1;dz<=1;dz++)for(let dx=-1;dx<=1;dx++){
            const i=templateCell(w,cell%w.columns+dx,Math.floor(cell/w.columns)+dz);
            if(Math.floor(i%w.columns/w.columns*n)===tile.x&&Math.floor(Math.floor(i/w.columns)/w.columns*n)===tile.y)return true;
          }
          return false;
        });
        if(visible)ids.push(lake.id);
      }
      return ids;
    }
    const w=this.world,n=2**tile.z,ids=new Set(),x0=Math.floor(tile.x*w.columns/n),x1=Math.ceil((tile.x+1)*w.columns/n),y0=Math.max(0,Math.floor(tile.y*w.columns/n)),y1=Math.min(w.rows-1,Math.ceil((tile.y+1)*w.columns/n));
    for(let y=y0;y<=y1;y++)for(let x=x0;x<=x1;x++)for(const f of lakeTemplateTile(w,templateCell(w,x,y)))ids.add(f.id);
    return [...ids];
  }
  async prepare(tile,cancelled=()=>false){
    if(cancelled())abort();if(tile.z<2)return [];
    return this.idsFor(tile).map(id=>{const l=this.catalog.get(id),shape={id,z:tile.z,level:l.level,rings:[],template:true};this.models.set(id,l);return shape;});
  }
  async prepareDetail(){this.activeDetail=null;}
  surface(wx,wz,height){return sampleTemplateLake(this.world,wx,wz,height)??{height,water:-Infinity,kind:0,salinity:null,saltLakeId:null};}
  stats(){return {models:this.models.size,shapes:0,details:0,detail:null,sampled:0,coarseVisited:0,step:this.step,vertices:0,templates:true};}
}
export function markSmallLakes(mask,tile,w,side=TILE){
  const n=2**tile.z;
  if(n*side>=w.columns)return mask;
  for(const lake of lakeTemplateCatalog(w).lakes.values())for(const cell of lake.cells){
    const x=((cell%w.columns+.5)/w.columns*n-tile.x)*side;
    const y=((Math.floor(cell/w.columns)+.5)/w.columns*n-tile.y)*side;
    if(x<0||y<0||x>=side||y>=side)continue;
    const at=Math.floor(y)*side+Math.floor(x);
    mask[at]=Math.max(mask[at],.65);
  }
  return mask;
}
export function lakeMask(shapes,tile,w,excluded=null){
  const mask=new Float32Array(TILE*TILE),n=2**tile.z,span=w.circumference/n,meters=span/TILE,center=(tile.x+.5)*span-w.circumference/2,top=tile.y*span-w.circumference/4;
  if(shapes.some(s=>s.template)){
    for(let row=0;row<TILE;row++)for(let col=0;col<TILE;col++){
      const wx=center-span/2+(col+.5)*meters,wz=top+(row+.5)*meters,s=sampleTemplateLake(w,wx,wz);
      if(s?.kind===2&&s.id!==excluded)mask[row*TILE+col]=1;
    }
    return tile.z<7?markSmallLakes(mask,tile,w):mask;
  }
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
