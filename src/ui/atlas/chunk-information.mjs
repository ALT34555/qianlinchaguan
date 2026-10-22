import {TAU,project,unproject,wrap,levelFor} from './tiles.mjs';
import {macroCodeAt} from './surface-sampler.mjs';

export const CHUNK_INFORMATION_LEVEL=16;

export function chunkGrid(world,state){
  const fine=levelFor(state)>=CHUNK_INFORMATION_LEVEL;
  return {fine,columns:fine?world.options.equatorChunks:world.columns};
}

// 屏幕采样只确定可见范围
function bounds(state,columns){
  const points=[];
  for(let y=0;y<=12;y++)for(let x=0;x<=12;x++){
    const p=unproject(state,x*state.width/12,y*state.height/12);
    if(p)points.push(p);
  }
  if(!points.length)return null;
  const xs=points.map(p=>(state.lon+wrap(p.lon-state.lon))/TAU*columns+columns/2);
  const ys=points.map(p=>(.25-p.lat/TAU)*columns);
  const polar=state.view==='globe'&&[-Math.PI/2,Math.PI/2].some(lat=>{
    const p=project(state,state.lon,lat);return p&&p.x>=0&&p.x<=state.width&&p.y>=0&&p.y<=state.height;
  });
  return {left:polar?0:Math.floor(Math.min(...xs))-1,right:polar?columns:Math.ceil(Math.max(...xs))+1,
    top:Math.max(0,Math.floor(Math.min(...ys))-1),bottom:Math.min(columns/2,Math.ceil(Math.max(...ys))+1)};
}

function stroke(ctx,state,points){
  let previous;
  for(const [lon,lat] of points){
    const p=project(state,lon,lat);
    if(!p){previous=null;continue;}
    if(previous&&Math.abs(p.x-previous.x)<state.width/2)ctx.lineTo(p.x,p.y);
    else ctx.moveTo(p.x,p.y);
    previous=p;
  }
}

export function drawChunkInformation(ctx,{state,world,owner}){
  if(!world||!owner?.surfaceCache)return;
  const {fine,columns}=chunkGrid(world,state),box=bounds(state,columns);
  if(!box)return;
  const unit=TAU/columns,pixel=unit*state.scale;
  const point=(x,y)=>[(x/columns-.5)*TAU,(.25-y/columns)*TAU];
  ctx.save();ctx.strokeStyle='rgba(40,27,17,.6)';ctx.lineWidth=.7;
  ctx.beginPath();
  if(pixel>=2){
    const steps=state.view==='globe'?64:1;
    for(let x=box.left;x<=box.right;x++){
      const samples=[];
      for(let i=0;i<=steps;i++)samples.push(point(x,box.top+(box.bottom-box.top)*i/steps));
      stroke(ctx,state,samples);
    }
    for(let y=box.top;y<=box.bottom;y++){
      const samples=[];
      const divisions=Math.max(16,steps,Math.ceil((box.right-box.left)*unit/(Math.PI/8)));
      for(let i=0;i<=divisions;i++)samples.push(point(box.left+(box.right-box.left)*i/divisions,y));
      stroke(ctx,state,samples);
    }
  }
  ctx.stroke();
  ctx.font='11px "Qianlin Song",serif';ctx.textAlign='center';ctx.textBaseline='middle';
  const seen=new Set(),labels=[];
  const spacingX=Math.max(100,Math.min(150,pixel)),spacingY=Math.max(44,Math.min(80,pixel));
  for(let sy=spacingY/2;sy<state.height;sy+=spacingY)for(let sx=spacingX/2;sx<state.width;sx+=spacingX){
    const p=unproject(state,sx,sy);if(!p)continue;
    const x=((Math.floor((p.lon/TAU+.5)*columns)%columns)+columns)%columns;
    const y=Math.min(columns/2-1,Math.max(0,Math.floor((.25-p.lat/TAU)*columns)));
    const key=`${x},${y}`;if(seen.has(key))continue;seen.add(key);
    const at=project(state,...point(x+.5,y+.5));if(!at)continue;
    if(labels.some(q=>Math.abs(q.x-at.x)<96&&Math.abs(q.y-at.y)<38))continue;
    labels.push(at);
    let code,cx=x,cz=y;
    if(fine){
      cx=x-columns/2;cz=y-columns/4;
      const tile=owner.surfaceCache.get(cx,cz,(at.x-state.width/2)**2+(at.y-state.height/2)**2,false,false,!!owner.refinementReady);
      code=(tile?.summary?.info??owner.generator.cachedChunkInfo(cx,cz))?.type;
    }else code=macroCodeAt(world,y*columns+x);
    const text=code===undefined?'…':String(code),coordinates=`(${cx}, ${cz})`;
    const width=Math.max(ctx.measureText(text).width,ctx.measureText(coordinates).width)+10;
    ctx.fillStyle='rgba(28,24,18,.72)';ctx.fillRect(at.x-width/2,at.y-16,width,32);
    ctx.fillStyle='#f5e9cb';ctx.fillText(text,at.x,at.y-7);ctx.fillText(coordinates,at.x,at.y+7);
  }
  ctx.restore();
}
