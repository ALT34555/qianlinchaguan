import {getTributaries,getTributariesAsync,macroCurveGeometry} from '../../systems/world/planet-macro/source-rivers.mjs';
import {lakeOutletSource} from '../../systems/world/planet-macro/river-headwaters.mjs';
import {macroBandWidth} from '../../systems/world/planet-macro/river-dimensions.mjs';
import {lakeLandscapeForCell} from '../../systems/world/planet-macro/lake-shores.mjs';
import {sampleNaturalHeight} from '../../systems/world/planet-macro/backend.mjs';
import {overviewRiver} from './surface-sampler.mjs';
import {TAU,TILE,project,wrap,levelFor} from './tiles.mjs';

export const MIN_RIVER_PIXELS=5;
export const OVERVIEW_RIVER_PIXELS=3;
export const riverDisplayPolicy=level=>({minimum:level<6?OVERVIEW_RIVER_PIXELS:MIN_RIVER_PIXELS,
  decorativeMinimum:level<=6?0:level<=12?1:3,macroOnly:level<=6,boundaries:level>=17});

// 宏观主河向量：不触发支流网络，可随底图立即交付
async function collectMacroVectors(world,tile,sampler,cancelled,yieldControl,boundaries,logicalLevel,vectors){
  const C=world.circumference,n=2**tile.z,span=C/n,cellSpan=C/world.columns;
  const center=(tile.x+.5)*span-C/2,left=center-span/2,top=tile.y*span-C/4;
  const delta=x=>x-Math.round(x/C)*C;
  const row0=Math.max(0,Math.floor(tile.y*world.columns/n)-2),row1=Math.min(world.rows-1,Math.ceil((tile.y+1)*world.columns/n)+2);
  const add=(id,kind,points)=>{
    const coordinates=Float64Array.from(points.flatMap(p=>[p.wx,p.wz,p.halfWidth]));
    let minX=Infinity,maxX=-Infinity,minZ=Infinity,maxZ=-Infinity,pad=span*5/TILE;
    for(const p of points){const x=center+delta(p.wx-center);minX=Math.min(minX,x);maxX=Math.max(maxX,x);minZ=Math.min(minZ,p.wz);maxZ=Math.max(maxZ,p.wz);pad=Math.max(pad,p.halfWidth/Math.max(.01,Math.cos(p.wz/C*TAU)));}
    if(maxX+pad<left||minX-pad>left+span||maxZ+pad<top||minZ-pad>top+span)return;
    vectors.push({id,kind,coordinates,boundaries:kind==='macro'&&boundaries});
  };
  for(let row=row0;row<=row1;row++){
    if(cancelled())return false;
    await yieldControl();
    for(const cell of sampler.riversByRow[row]){
      const x=((cell%world.columns+.5)/world.columns-.5)*C;
      if(Math.abs(delta(x-center))>span/2+cellSpan*2)continue;
      const coarse=span/TILE>cellSpan/16;
      const outlet=lakeOutletSource(world,cell);
      const curve=outlet?macroCurveGeometry(world,cell):coarse?overviewRiver(world,cell):macroCurveGeometry(world,cell);
      if(!curve)continue;
      add('macro:'+cell,'macro',curve.points.map((p,k)=>({...p,halfWidth:p.halfWidth??macroBandWidth(world.flow[k?curve.receiver:cell])/2})));
    }
  }
  return true;
}

const vectorOf=(id,kind,points)=>({id,kind,coordinates:Float64Array.from(points.flatMap(p=>[p.wx,p.wz,p.halfWidth])),boundaries:false});

// 宏观格装饰支流：湖泊出流与泉眼归入同任务
export function collectCellVectors(world,cell,tier=1,network){
  const out=[];
  if(world.water[cell]===2){
    if(tier<2)return out;
    const outlet=lakeLandscapeForCell(world,cell,sampleNaturalHeight)?.outlet;
    if(outlet&&(outlet.ownerCell??outlet.cell)===cell){
      out.push(vectorOf(outlet.id,'rainfall',outlet.points));
      const p=outlet.pond,dx=(p.radiusX-p.radiusZ)/outlet.cos;
      out.push(vectorOf(outlet.id+':pond','lake-pond',[{wx:p.wx-dx,wz:p.wz,halfWidth:p.radiusZ},{wx:p.wx+dx,wz:p.wz,halfWidth:p.radiusZ}]));
    }
  }
  network??=getTributaries(world,cell);
  for(const edge of network.edges)
    out.push(vectorOf(edge.id,edge.sourceKind??'rainfall',edge.points.map(p=>({...network.frame.coordinates(p.x,p.z),halfWidth:p.halfWidth??edge.halfWidth}))));
  for(const source of network.sources){
    const p=source.pool;if(source.connected||!p||p.dampOnly)continue;
    const dx=Math.cos(p.yaw)*(p.rx-p.rz),dz=Math.sin(p.yaw)*(p.rx-p.rz);
    out.push(vectorOf(source.id+':pool','wet-point',[{...network.frame.coordinates(source.x-dx,source.z-dz),halfWidth:p.rz},{...network.frame.coordinates(source.x+dx,source.z+dz),halfWidth:p.rz}]));
  }
  return out;
}

export async function collectCellVectorsAsync(world,cell,tier=1,options={}){
  if(world.water[cell]===2&&tier<2)return [];
  const network=await getTributariesAsync(world,cell,options);
  if(!network||options.cancelled?.())return null;
  return collectCellVectors(world,cell,tier,network);
}

// 装饰支流：逐宏观格构建，瓦片只引用同一份几何
export async function collectRiverVectors(world,tile,sampler,cancelled=()=>false,yieldControl=async()=>{},boundaries=false,logicalLevel=tile.z,{decorative=true}={}){
  const vectors=[];
  if(!await collectMacroVectors(world,tile,sampler,cancelled,yieldControl,boundaries,logicalLevel,vectors))return null;
  if(decorative&&logicalLevel>=7&&2**tile.z>=world.columns){
    const n=2**tile.z,x0=Math.floor(tile.x*world.columns/n)-1,x1=Math.ceil((tile.x+1)*world.columns/n)+1;
    const y0=Math.max(0,Math.floor(tile.y*world.columns/n)-1),y1=Math.min(world.rows-1,Math.ceil((tile.y+1)*world.columns/n)+1);
    for(let row=y0;row<=y1;row++)for(let col=x0;col<=x1;col++){
      if(cancelled())return null;
      await yieldControl();
      const cell=row*world.columns+(col%world.columns+world.columns)%world.columns;
      for(const vector of collectCellVectors(world,cell,logicalLevel>=16?2:1))vectors.push(vector);
    }
  }
  return vectors;
}

export function riverWidthPixels(state,C,a,b){
  const lon=a[0]/C*TAU,lat=-a[1]/C*TAU,cos=Math.max(.01,Math.cos(lat));
  const dx=wrap((b[0]-a[0])/C*TAU)*C/TAU*cos,dz=b[1]-a[1],length=Math.hypot(dx,dz)||1;
  const half=(a[2]+b[2])/2,p=project(state,lon,lat,false);
  const q=project(state,lon-dz/length*half/cos/C*TAU,lat-dx/length*half/C*TAU,false);
  const end=project(state,b[0]/C*TAU,-b[1]/C*TAU,false);
  if(!p||!q||!end)return MIN_RIVER_PIXELS;
  const sx=end.x-p.x,sy=end.y-p.y,l=Math.hypot(sx,sy)||1;
  return Math.abs((q.x-p.x)*sy-(q.y-p.y)*sx)/l*2;
}

export function drawRiverVectors(ctx,state,C,tiles,{level=levelFor(state),pass='all'}={}){
  const policy=riverDisplayPolicy(level);
  const unique=new Map();
  for(const tile of [...tiles].sort((a,b)=>(a.z??0)-(b.z??0)))for(const vector of tile.riverVectors??[]){
    if((policy.macroOnly||pass==='macro')&&vector.kind!=='macro'||pass==='decorative'&&vector.kind==='macro')continue;
    unique.set(vector.id,vector);
  }
  ctx.save();ctx.strokeStyle='rgb(51,113,139)';ctx.lineCap='round';ctx.lineJoin='round';
  for(const vector of unique.values()){
    const c=vector.coordinates;
    for(let i=3;i<c.length;i+=3){
      const a=c.subarray(i-3,i),b=c.subarray(i,i+3),native=riverWidthPixels(state,C,a,b);
      const minimum=vector.kind==='macro'?policy.minimum:policy.decorativeMinimum;
      ctx.lineWidth=policy.boundaries&&vector.kind==='macro'?minimum:Math.max(minimum,native);
      const lon=a[0]/C*TAU,lat=-a[1]/C*TAU,dl=wrap((b[0]-a[0])/C*TAU),dlat=-(b[1]-a[1])/C*TAU;
      const steps=Math.max(1,Math.ceil(Math.hypot(dl,dlat)/.03));
      ctx.beginPath();let previous=null;
      for(let j=0;j<=steps;j++){
        const t=j/steps,p=project(state,lon+dl*t,lat+dlat*t);
        if(p&&previous&&Math.abs(p.x-previous.x)<Math.PI*state.scale){ctx.lineTo(p.x,p.y);}
        else if(p)ctx.moveTo(p.x,p.y);
        previous=p;
      }
      ctx.stroke();
    }
  }
  ctx.restore();return unique.size;
}

// 装饰支流作为独立来源追加，异步到达后自然补画
export function drawRiverLayers(ctx,state,C,tiles,drawWater,decorativeTiles=[]){
  const policy=riverDisplayPolicy(levelFor(state));
  const sources=decorativeTiles.length?[...tiles,...decorativeTiles]:tiles;
  drawRiverVectors(ctx,state,C,decorativeTiles.length?sources:tiles,{pass:policy.boundaries?'macro':'all'});
  drawWater();
  if(policy.boundaries)drawRiverVectors(ctx,state,C,sources,{pass:'decorative'});
}
