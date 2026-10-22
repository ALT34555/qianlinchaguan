import {clipIce,type IceVertex,iceVertexLevel,iceVertexCoverage} from './WaterSurface';
import {terrainHeightAt,terrainDiagonal,type HeightSampler} from './TerrainSurface';
type Polygon=IceVertex[];
const EPS=1e-5;
function hasArea(p:Polygon):boolean {
  if(p.length<3)return false;const a=p[0];let area=0;
  for(let i=1;i<p.length-1;i++)area+=(p[i].x-a.x)*(p[i+1].z-a.z)-(p[i].z-a.z)*(p[i+1].x-a.x);
  return Math.abs(area)>1e-10;
}
function clip(poly:Polygon,distance:(v:IceVertex)=>number):Polygon {
  const out:Polygon=[];
  for(let i=0;i<poly.length;i++){
    const a=poly[i],b=poly[(i+1)%poly.length],da=distance(a),db=distance(b);
    if(da>=0)out.push(a);
    if((da>=0)!==(db>=0)){
      const t=da/(da-db),v={} as IceVertex;
      for(const k of ['x','y','z','bottom','coverage','depth'] as const)v[k]=a[k]+(b[k]-a[k])*t;
      out.push(v);
    }
  }
  return out;
}
export class IceShell {
  private readonly patches=new Map<string,Polygon[]>();
  constructor(private readonly seed:number,private readonly bed:HeightSampler,private readonly top:HeightSampler,
    private readonly bottom:HeightSampler,private readonly coverage:HeightSampler) {}
  clear():void {this.patches.clear();}
  private cell(x:number,z:number):Polygon[] {
    const key=`${x},${z}`,hit=this.patches.get(key);if(hit)return hit;
    const coords=[[x,z],[x,z+1],[x+1,z+1],[x+1,z]],amounts=coords.map(([a,b])=>iceVertexCoverage(a,b,this.coverage));
    let result:Polygon[]=[];
    if(amounts.some(v=>v>=.5)){
      const heights=coords.map(([a,b])=>iceVertexLevel(a,b,this.top)),finite=heights.filter(Number.isFinite);
      if(finite.length){
        const fallback=Math.min(...finite),verts=coords.map(([a,b],i)=>{
          const y=Number.isFinite(heights[i])?heights[i]:fallback,ground=terrainHeightAt(a,b,this.seed,this.bed),lower=iceVertexLevel(a,b,this.bottom);
          return {x:a,z:b,y,bottom:Math.min(y,Math.max(ground,Number.isFinite(lower)?lower:y)),coverage:amounts[i],depth:y-ground};
        });
        result=(terrainDiagonal(x,z,this.seed)?[[0,1,3],[1,2,3]]:[[0,1,2],[0,2,3]])
          .map(tri=>clipIce(clipIce(tri.map(i=>verts[i]),'coverage'),'depth')).filter(p=>p.length>=3);
      }
    }
    if(this.patches.size>=2048)this.patches.delete(this.patches.keys().next().value!);
    this.patches.set(key,result);return result;
  }
  private footprint(x:number,z:number,half:number):Polygon[] {
    const out:Polygon[]=[],x0=x-half,x1=x+half,z0=z-half,z1=z+half;
    for(let iz=Math.floor(z0-.5)+.5;iz<=z1;iz++)for(let ix=Math.floor(x0-.5)+.5;ix<=x1;ix++)for(const source of this.cell(ix,iz)){
      let p=clip(source,v=>v.x-x0);p=clip(p,v=>x1-v.x);p=clip(p,v=>v.z-z0);p=clip(p,v=>z1-v.z);
      if(hasArea(p))out.push(p);
    }
    return out;
  }
  ground(x:number,z:number,half:number,feet=Infinity):number {
    let height=-Infinity;
    for(const p of this.footprint(x,z,half))for(const v of clip(p,v=>feet-v.bottom+EPS))height=Math.max(height,v.y);
    return height;
  }
  blocked(x:number,y:number,z:number,height:number,half:number):boolean {
    return this.footprint(x,z,half).some(p=>hasArea(clip(clip(p,v=>v.y-y-EPS),v=>y+height-v.bottom-EPS)));
  }
  ceiling(x:number,z:number,half:number,head:number):number {
    let result=Infinity;
    for(const p of this.footprint(x,z,half))for(const v of clip(p,v=>v.bottom-head+EPS))result=Math.min(result,v.bottom);
    return result;
  }
  resolve(x:number,y:number,z:number,height:number,half:number,bed:number):number {
    const polygons=this.footprint(x,z,half);
    if(!polygons.some(p=>hasArea(clip(clip(p,v=>v.y-y-EPS),v=>y+height-v.bottom-EPS))))return y;
    const top=Math.max(...polygons.flatMap(p=>p.map(v=>v.y))),bottom=Math.min(...polygons.flatMap(p=>p.map(v=>v.bottom)));
    const below=bottom-height-EPS,above=top+EPS;
    return below>=bed&&Math.abs(y-below)<Math.abs(y-above)?below:above;
  }
}
