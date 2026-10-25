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
  ctx.font='bold 10px "Qianlin Song",serif';ctx.textAlign='right';ctx.textBaseline='top';
  ctx.fillStyle='#fff8e5';ctx.shadowColor='rgba(25,20,14,.9)';ctx.shadowBlur=2;
  const infoAt=(x,y,priority=0)=>{
    const cx=((x%columns)+columns)%columns-columns/2,cz=y-columns/4;
    const tile=owner.surfaceCache.get(cx,cz,priority,false,false,!!owner.refinementReady);
    return tile?.summary?.info??owner.generator.cachedChunkInfo(cx,cz);
  };
  if(pixel>=24)for(let y=box.top;y<box.bottom;y++)for(let x=box.left;x<box.right;x++){
    const at=project(state,...point(x+1,y)),left=project(state,...point(x,y)),bottom=project(state,...point(x+1,y+1));
    if(!at||!left||!bottom||at.x<0||at.x>state.width+24||at.y<0||at.y>state.height)continue;
    if(at.x-left.x<24||Math.hypot(bottom.x-at.x,bottom.y-at.y)<16)continue;
    const col=((x%columns)+columns)%columns;
    const code=fine?infoAt(col,y,(at.x-state.width/2)**2+(at.y-state.height/2)**2)?.type:macroCodeAt(world,y*columns+col);
    if(code===undefined)continue;
    const text=owner.chunkInformationCode?.(code)??String(code).padStart(3,'0');
    ctx.fillText(text,at.x-4,at.y+4);
  }
  ctx.shadowBlur=0;
  drawSelectedInformation(ctx,{state,world,owner,fine,columns,point,infoAt});
  ctx.restore();
}

function drawSelectedInformation(ctx,{state,world,owner,fine,columns,point,infoAt}){
  const selected=owner.selectedChunk;
  if(!selected||!owner.chunkInformationInspected)return;
  const size=world.options.equatorChunks;
  const x=fine?selected.cx+columns/2:Math.floor((selected.cx+size/2)*columns/size);
  const y=fine?selected.cz+columns/4:Math.floor((selected.cz+size/4)*columns/size);
  let center=project(state,...point(x+.5,y+.5));
  if(!center||center.x<0||center.x>state.width||center.y<0||center.y>state.height){
    center=project(state,(selected.cx+.5)/size*TAU,-(selected.cz+.5)/size*TAU);
  }
  if(!center||center.x<0||center.x>state.width||center.y<0||center.y>state.height)return;
  const right=project(state,...point(x+1,y+.5))??center;
  const left=project(state,...point(x,y+.5))??center;
  const cell=y*columns+x;
  const info=fine?infoAt(x,y):{cx:x,cz:y,type:macroCodeAt(world,cell),
    elevation:world.height[cell],temperature:world.temperature[cell]};
  const lines=owner.chunkInformationDetails?.(info,fine)??[info?String(info.type):'…'];
  ctx.font='12px "Qianlin Song",serif';ctx.textAlign='left';ctx.textBaseline='top';
  const width=Math.min(state.width-16,Math.max(180,...lines.map(text=>ctx.measureText(text).width+24)));
  const height=lines.length*22+20;
  let px=right.x+12;
  if(px+width>state.width-8)px=left.x-width-12;
  px=Math.max(8,Math.min(state.width-width-8,px));
  const py=Math.max(8,Math.min(state.height-height-8,center.y-height/2));
  ctx.fillStyle='rgba(249,243,230,.97)';ctx.fillRect(px,py,width,height);
  ctx.strokeStyle='rgba(131,91,69,.5)';ctx.lineWidth=1;ctx.strokeRect(px,py,width,height);
  ctx.fillStyle='#65463c';
  lines.forEach((text,index)=>ctx.fillText(text,px+12,py+10+index*22));
}
