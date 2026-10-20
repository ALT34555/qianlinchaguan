import {TAU,LIMIT,clamp,project,detailLevel,unproject,levelFor} from './tiles.mjs';
import {planetCoordinates} from '../../systems/world/WorldSettings.ts';
import {padTilePixels,expandTriangle} from './tile-seams.mjs';
import {ancestor,planTiles,TileStore} from './tile-plan.mjs';
import {focusLakeAt} from './lake-vectors.mjs';
const layers=new WeakMap();
export class GameTiles{
  constructor(view,world){
    this.owner=view;this.world=world;this.store=new TileStore();this.failedTiles=new Set();this.queue=[];this.version=0;this.serial=0;
    this.worker=new Worker(new URL('./tiles.worker.mjs',import.meta.url),{type:'module'});
    this.worker.onmessage=({data:m})=>{
      if(m.id!==this.inFlight?.id)return;
      if(m.kind==='progress'){this.progress=m;this.owner.requestRender();return;}
      this.progress=null;
      this.inFlight=null;
      if(m.kind==='tile'){
        const canvas=document.createElement('canvas');canvas.width=canvas.height=m.side+2;
        canvas.getContext('2d').putImageData(new ImageData(padTilePixels(m.pixels,m.side),m.side+2,m.side+2),0,0);
        this.store.set(this.storageStyle(m.tile,m.layer,m.focusId??null),{...m.tile,canvas});this.version++;this.stats=m.stats;
      }else if(m.kind==='error'){this.failedTiles.add(`${m.layer}:${m.tile.key}`);this.version++;}
      if(!this.paused)this.owner.requestRender();
    };
    this.worker.onerror=()=>{this.failed=true;this.progress=null;this.inFlight=null;this.queue=[];this.worker.terminate();view.requestRender();};
    this.worker.postMessage({kind:'world',world,generation:1});
  }
  updateStyle(style){this.style=style;}
  storageStyle(tile,style=this.style,focus=this.focusId){return tile.z>=13?`${style}:lake:${focus??'none'}`:style;}
  setPaused(paused){
    if(this.paused===paused)return;
    this.paused=paused;this.camera=null;
    if(paused){this.queue=[];this.cancel();}
  }
  scale(){return this.state.scale;}
  pump(){
    if(this.inFlight||this.failed||this.paused||!this.owner.active)return;
    while(this.queue.length){
      const tile=this.queue.shift();if(this.store.has(this.storageStyle(tile),tile.key)||this.failedTiles.has(`${this.style}:${tile.key}`))continue;
      if(tile.prefetch&&(!this.owner.refinementReady||this.plan.z<13&&tile.z>=13))continue;
      this.inFlight={id:++this.serial,tile,layer:this.style,focusId:this.focusId};
      this.worker.postMessage({kind:'tile',...this.inFlight,layer:this.style,generation:1});break;
    }
  }
  cancel(){if(this.inFlight&&!this.cancelling){this.cancelling=true;this.worker.postMessage({kind:'cancel',generation:1});}}
  draw(ctx,state){
    this.state=state;this.view=state.view;this.width=state.width;this.height=state.height;
    const focus=this.owner.labAtlasFocus??{x:state.width/2,y:state.height/2};
    const point=unproject(state,focus.x,focus.y),selected=this.owner.selectedChunk;
    this.focusId=levelFor(state)>=13&&(point||selected)?focusLakeAt(this.world,selected?(selected.cx+.5)*64:point.lon/TAU*this.world.circumference,selected?(selected.cz+.5)*64:-point.lat/TAU*this.world.circumference):null;
    const key=JSON.stringify(state),focusKey=`${focus.x}/${focus.y}/${this.focusId}`;
    if(this.camera!==key||this.planStyle!==this.style||this.planVersion!==this.version||this.focusKey!==focusKey){
      const failed=new Set([...this.failedTiles].filter(k=>k.startsWith(this.style+':')).map(k=>k.slice(this.style.length+1)));
      this.camera=key;this.planStyle=this.style;this.planVersion=this.version;this.focusKey=focusKey;
      this.plan=planTiles(state,k=>this.store.has(this.storageStyle({z:Number(k.split('/')[0])}),k),failed,focus);
      this.queue=this.plan.queue;
      this.store.protectedKeys=new Set([...this.plan.protectedKeys].map(k=>this.store.id(this.storageStyle({z:Number(k.split('/')[0])}),k)));this.store.trim();
      if(this.inFlight&&(this.inFlight.layer!==this.style||this.inFlight.tile.z>=13&&this.inFlight.focusId!==this.focusId||!this.plan.protectedKeys.has(this.inFlight.tile.key)&&!this.queue.some(t=>t.key===this.inFlight.tile.key)))this.cancel();
    }
    if(!this.inFlight)this.cancelling=false;
    const tiles=this.plan.tiles,cacheKey=`${key}:${this.style}:${this.focusId}:${this.version}`,output=ctx;
    if(this.backgroundKey!==cacheKey){
      this.background??=document.createElement('canvas');
      if(this.background.width!==Math.ceil(state.width)||this.background.height!==Math.ceil(state.height)){this.background.width=Math.ceil(state.width);this.background.height=Math.ceil(state.height);}
      ctx=this.background.getContext('2d');ctx.clearRect(0,0,state.width,state.height);
      const complete=tiles.every(t=>this.store.has(this.storageStyle(t),t.key));
      if(!complete||this.owner.isGlobe){
        const v=this.owner,cell=v.baseCellSize*v.scale;
        if(v.isGlobe){const p=planetCoordinates(v.centerCx,v.centerCz,v.generator.generation.planet.equatorChunks);v.planetLayer.drawGlobe(ctx,state.width,state.height,v.globeRadius,p.longitude,p.latitude,v.overviewStyle());}
        else v.planetLayer.drawFlat(ctx,state.width,state.height,cell,v.centerCx,v.centerCz,v.panX,v.panY,v.overviewStyle(),v.isMercator);
      }
      const found=new Map();
      for(const tile of tiles)for(let z=tile.z;z>=2;z--){const p=ancestor(tile,z),cached=this.store.get(this.storageStyle(p),p.key);if(cached){found.set(p.key,cached);break;}}
      ctx.save();if(state.view==='globe'){ctx.beginPath();ctx.arc(state.width/2,state.height/2,state.scale,0,TAU);ctx.clip();}
      for(const tile of [...found.values()].sort((a,b)=>a.z-b.z))this.drawTile(ctx,tile,state);
      ctx.restore();this.backgroundKey=cacheKey;
    }
    output.drawImage(this.background,0,0,state.width,state.height);this.pump();
    return {loaded:tiles.filter(t=>this.store.has(this.storageStyle(t),t.key)).length,total:tiles.length,z:this.plan.z,shore:this.progress,focusId:this.focusId};
  }
}
export function drawGameAtlas(v){
  if(!v.active)return true;
  if(!v.isPlanet||v.exploration||v.survivalPreview||v.showClimate||v.showChunkColors||v.filterTypeId!==null||v.filterCategory!==null){suspendGameAtlas(v,true);return false;}
  const world=v.generator.exposeHydrology()?.planetMacro;if(!world)return false;
  let layer=layers.get(v);if(layer?.world!==world){layer?.worker.terminate();layer=new GameTiles(v,world);layers.set(v,layer);}
  layer.setPaused(false);if(layer.failed)return false;
  layer.updateStyle(v.showSatellite?'satellite':v.showProjection?'terrain':'biome');
  const dpr=Math.min(window.devicePixelRatio||1,2),width=v.canvas.width/dpr,height=v.canvas.height/dpr,size=v.generator.generation.planet.equatorChunks,cell=v.baseCellSize*v.scale;
  const center=v.isGlobe?planetCoordinates(v.centerCx,v.centerCz,size):{longitude:(v.centerCx-v.panX/cell)/size*360,latitude:-v.worldZ(v.mapZ(v.centerCz)-v.panY/cell)/size*360};
  const state={width,height,view:v.isGlobe?'globe':v.isMercator?'mercator':'projection',lon:center.longitude/360*TAU,lat:center.latitude/360*TAU,scale:v.isGlobe?v.globeRadius:cell*size/TAU};
  const ctx=v.ctx;ctx.save();ctx.scale(dpr,dpr);ctx.fillStyle='#241c13';ctx.fillRect(0,0,width,height);
  const progress=layer.draw(ctx,state);v.surfaceCache.beginFrame();
  const meters=world.radius/state.scale*(v.isMercator?Math.cos(state.lat):1);
  if(!v.isGlobe&&cell>=24){
    const left=Math.floor(v.centerCx+(-width/2-v.panX)/cell),right=Math.ceil(v.centerCx+(width/2-v.panX)/cell),top=Math.floor(v.worldZ(v.mapZ(v.centerCz)+(-height/2-v.panY)/cell)),bottom=Math.ceil(v.worldZ(v.mapZ(v.centerCz)+(height/2-v.panY)/cell));
    const coords=v.centerCells(left,right,top,bottom,v.centerCx-v.panX/cell,v.worldZ(v.mapZ(v.centerCz)-v.panY/cell)).slice(0,192).filter(([,z])=>z>=-size/4&&z<size/4).map(([x,z])=>[((x+size/2)%size+size)%size-size/2,z]);
    drawAtlasDetails(v,ctx,state,coords,cell,size);
  }
  v.surfaceCache.endFrame();v.drawPlayerMarker(ctx,width,height,cell);v.drawTerritoryLayer(ctx,width,height,cell);v.drawRainClouds(ctx,width,height,cell);
  const at=v.getCenterCoordinates();v.onStatsUpdate?.({centerCx:at.cx,centerCz:at.cz,scale:v.scale,visibleChunks:0,climateCounts:[0,0,0,0,0],climatePercents:[0,0,0,0,0],matchedCount:0,matchedPercent:0,activeFilterName:'',level:v.isGlobe?'globe':'overview'});
  const target=meters*100,unit=10**Math.floor(Math.log10(target)),length=[1,2,5,10].map(n=>n*unit).filter(n=>n<=target).pop()??unit;
  ctx.fillStyle='rgba(27,24,18,.78)';ctx.fillRect(12,height-90,progress.shore?340:256,52);
  ctx.fillStyle='#eadfc0';ctx.strokeStyle='#eadfc0';ctx.font='12px "Qianlin Song",serif';ctx.textAlign='left';ctx.fillText(`${detailLevel(meters)} L${progress.z} · ${progress.loaded}/${progress.total}${progress.shore?' · '+(progress.shore.stage??'湖形计算中'):progress.z>=13&&progress.focusId!==null?' · 聚焦湖 '+progress.focusId:''}${layer.failedTiles.size?' · 部分细节失败':''}`,20,height-74);ctx.fillText((length>=1000?`${length/1000} km`:`${length} m`)+(progress.shore?` · 已采样 ${Math.round(progress.shore.sampled/1000)} 千点`:''),20,height-56);ctx.beginPath();ctx.moveTo(20,height-50);ctx.lineTo(20+length/meters,height-50);ctx.stroke();ctx.restore();return true;
}
function drawAtlasDetails(v,ctx,state,coords,cell,size){
  for(const [cx,cz] of coords){
    const wrapped=cx+Math.round((v.centerCx-v.panX/cell-cx)/size)*size,x=state.width/2+v.panX+(wrapped-v.centerCx)*cell,y=state.height/2+v.panY+(v.mapZ(cz)-v.mapZ(v.centerCz))*cell,h=(v.mapZ(cz+1)-v.mapZ(cz))*cell;
    if(v.showChunkInfo&&v.refinementReady){
      const tile=v.surfaceCache.get(cx,cz,(x-state.width/2)**2+(y-state.height/2)**2,false,false,true),info=tile?.summary?.info??v.generator.cachedChunkInfo(cx,cz);
      if(info&&cell>=36){ctx.fillStyle='#eadfc0';ctx.font='10px "Qianlin Song",serif';ctx.fillText(String(info.type),x+3,y+12);if(v.showProjection)v.drawFlowArrow(ctx,info,x,y,cell,h);}
    }
    if(v.hoveredChunk?.cx===cx&&v.hoveredChunk?.cz===cz||v.selectedChunk?.cx===cx&&v.selectedChunk?.cz===cz){ctx.strokeStyle=v.selectedChunk?.cx===cx&&v.selectedChunk?.cz===cz?'#eac876':'#ffffff';ctx.lineWidth=1;ctx.strokeRect(x,y,cell,h);}
  }
}
export function suspendGameAtlas(v,paused){layers.get(v)?.setPaused(paused);}
export function disposeGameAtlas(v){const layer=layers.get(v);if(layer){layer.worker.terminate();layers.delete(v);}}
class TileMesh {
  drawTile(ctx,tile,view){
    const span=TAU/2**tile.z,lon0=tile.x*span-Math.PI,lat0=Math.PI/2-tile.y*span;
    const steps=this.view==='globe'?clamp(Math.ceil(span*this.scale()/32),1,24):this.view==='mercator'?8:1,side=tile.canvas.width-2;
    const center=project(view,lon0+span/2,clamp(lat0-span/2,-LIMIT,LIMIT),false);
    const vertex=(x,y)=>{const lat=clamp(lat0-y*span,view.view==='mercator'?-LIMIT:-Math.PI/2,view.view==='mercator'?LIMIT:Math.PI/2),p=project(view,lon0+x*span,lat);if(p&&this.view!=='globe')p.x=center.x+(x-.5)*span*view.scale;return p;};
    for(let y=0;y<steps;y++)for(let x=0;x<steps;x++){
      const u=x/steps,v=y/steps,d=1/steps,a=vertex(u,v),b=vertex(u+d,v),c=vertex(u,v+d),e=vertex(u+d,v+d);
      this.triangle(ctx,tile.canvas,[a,b,c],[[u*side,v*side],[(u+d)*side,v*side],[u*side,(v+d)*side]]);
      this.triangle(ctx,tile.canvas,[b,e,c],[[(u+d)*side,v*side],[(u+d)*side,(v+d)*side],[u*side,(v+d)*side]]);
    }
  }
  triangle(ctx,image,p,uv){
    uv=uv.map(([x,y])=>[x+1,y+1]);
    if(p.some(v=>!v))return;
    if(this.view!=='globe'&&Math.max(...p.map(v=>v.x))-Math.min(...p.map(v=>v.x))>this.scale()*Math.PI)return;
    if(Math.max(...p.map(v=>v.x))<0||Math.min(...p.map(v=>v.x))>this.width||Math.max(...p.map(v=>v.y))<0||Math.min(...p.map(v=>v.y))>this.height)return;
    const [a,b,c]=p,[u,v,w]=uv,det=(v[0]-u[0])*(w[1]-u[1])-(v[1]-u[1])*(w[0]-u[0]);if(!det)return;
    const ax=((b.x-a.x)*(w[1]-u[1])-(c.x-a.x)*(v[1]-u[1]))/det,ay=((b.y-a.y)*(w[1]-u[1])-(c.y-a.y)*(v[1]-u[1]))/det;
    const bx=((c.x-a.x)*(v[0]-u[0])-(b.x-a.x)*(w[0]-u[0]))/det,by=((c.y-a.y)*(v[0]-u[0])-(b.y-a.y)*(w[0]-u[0]))/det;
    const [ca,cb,cc]=expandTriangle(p);
    ctx.save();ctx.beginPath();ctx.moveTo(ca.x,ca.y);ctx.lineTo(cb.x,cb.y);ctx.lineTo(cc.x,cc.y);ctx.closePath();ctx.clip();ctx.transform(ax,ay,bx,by,a.x-ax*u[0]-bx*u[1],a.y-ay*u[0]-by*u[1]);ctx.drawImage(image,0,0);ctx.restore();
  }
}
GameTiles.prototype.drawTile=TileMesh.prototype.drawTile;
GameTiles.prototype.triangle=TileMesh.prototype.triangle;
