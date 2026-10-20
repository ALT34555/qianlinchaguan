import {TILE,TAU,clamp} from './tiles.mjs';
import {sampleSurface,macroAt} from '../../systems/world/planet-macro/backend.mjs';
import {samplePlanetSurface} from '../../systems/world/planet-macro/surface.mjs';
import {macroCurveGeometry as macroCurve,clearTributaryCache} from '../../systems/world/planet-macro/source-rivers.mjs';
import {continuousSatelliteColor,continuousBiomeColor} from '../../systems/world/planet-macro/satellite.mjs';
import {coverRGB} from '../../systems/world/planet-macro/surface-cover.mjs';
import {macroRiverThreshold} from '../../systems/world/planet-macro/river-dimensions.mjs';
import {LakeVectors,lakeMask,focusLakeAt} from './lake-vectors.mjs';
import {iceCoverage,ICE_RGB} from './ice-cover.mjs';
import {cooperativeLakeShore,LakeShorePending} from '../../systems/world/planet-macro/lake-shores.mjs';

const waterColors=[[0,0,0],[34,79,103],[51,113,139],[64,139,166],[64,139,166]];
const riverWidths=[1.8,2.4,3.2,4.2,5.4];
export function riverStrokePixels(flow,threshold,z){
  if(z<7||z>13)return 0;
  const ratio=flow/Math.max(1e-9,threshold),tier=ratio<1?0:ratio<3?1:ratio<10?2:ratio<30?3:4;
  return riverWidths[tier]*(1+(z-7)*.12);
}
export function sampleOverview(w,wx,wz){
  const span=w.circumference/w.columns,x=Math.floor((wx/w.circumference+.5)*w.columns),
    y=clamp(Math.floor((wz/w.circumference+.25)*w.columns),0,w.rows-1),i=y*w.columns+((x%w.columns)+w.columns)%w.columns;
  const height=macroAt(w,wx,wz),kind=w.water[i]===2?2:height<=0?1:0;
  const cos=Math.max(.01,Math.cos(wz/w.circumference*TAU));
  return {height,kind,temperature:macroAt(w,wx,wz,'temperature'),macro:{height,
    rainfall:macroAt(w,wx,wz,'rainfall'),evaporation:macroAt(w,wx,wz,'evaporation'),
    eastSlope:(macroAt(w,wx+span,wz)-macroAt(w,wx-span,wz))/(2*span*cos),
    southSlope:(macroAt(w,wx,wz+span)-macroAt(w,wx,wz-span))/(2*span)}};
}
export function overviewRiver(w,cell){
  const receiver=w.receiver[cell];if(receiver<0||!w.riverWidth[cell])return null;
  const point=i=>({wx:((i%w.columns+.5)/w.columns-.5)*w.circumference,
    wz:(Math.floor(i/w.columns)+.5)/w.columns*w.circumference-w.circumference/4});
  return {points:[point(cell),point(receiver)],halfWidth:w.riverWidth[cell]};
}
const pause=()=>new Promise(resolve=>{
  const channel=new MessageChannel();channel.port1.onmessage=()=>{channel.port1.close();channel.port2.close();resolve();};channel.port2.postMessage(0);
});
export class TileRenderer{
  constructor(world,budget=32*2**20){
    this.world=world;this.budget=budget;this.bytes=0;this.cache=new Map();this.samples=0;this.hits=0;
    this.lakes=new LakeVectors(world);this.focusId=null;
    this.riverThreshold=macroRiverThreshold(world.options.equatorChunks/world.columns,world.options.precipitation);
    this.rivers=[];for(let i=0;i<world.height.length;i++)if(world.riverWidth[i]>0&&world.receiver[i]>=0)this.rivers.push(i);
    cooperativeLakeShore(world,{pause,progress:p=>this.progress?.(p),cancelled:()=>this.cancelled,sample:(wx,wz,h)=>this.lakes.surface(wx,wz,h,this.focusId)});
  }
  async waitForShore(promise,cancelled){
    let done=false,failure;promise.then(()=>{done=true;},error=>{failure=error;done=true;});
    while(!done){await new Promise(resolve=>setTimeout(resolve,16));if(cancelled())return false;}
    if(failure)throw failure;return !cancelled();
  }
  async fields(tile,cancelled=()=>false){
    const cacheKey=tile.z>=13&&this.focusId!==null?`${tile.key}/lake:${this.focusId}`:tile.key;
    const previous=this.cache.get(cacheKey);
    if(previous){this.cache.delete(cacheKey);this.cache.set(cacheKey,previous);this.hits++;return previous;}
    const w=this.world,n=2**tile.z,span=w.circumference/n,meters=span/TILE,data=new Uint8ClampedArray(TILE*TILE*8);
    for(let row=0;row<TILE;row++){
      if(row%8===0){await pause();if(cancelled())return null;}
      for(let col=0;col<TILE;col++){
        const wx=(tile.x+(col+.5)/TILE)/n*w.circumference-w.circumference/2,wz=(tile.y+(row+.5)/TILE)/n*w.circumference-w.circumference/4;
        let s;
        while(!s){try{s=meters>128?sampleOverview(w,wx,wz):meters<=4?samplePlanetSurface(w,wx,wz,true,true):sampleSurface(w,wx,wz,true,false,tile.z>=13,true);if(tile.z>=7&&tile.z<13&&s.kind===2)s.kind=0;}
          catch(error){if(!(error instanceof LakeShorePending))throw error;if(!await this.waitForShore(error.promise,cancelled))return null;}}
        const {height:base,rainfall:rain,evaporation:evap,eastSlope,southSlope}=s.macro;
        this.samples++;
        const step=w.circumference/w.columns,cos=Math.max(.01,Math.cos(wz/w.circumference*TAU));
        const slope=Math.max(Math.abs(eastSlope),Math.abs(southSlope)),relief=slope*64*Math.SQRT2;
        const satellite=continuousSatelliteColor(s.temperature,rain,evap,relief,slope),biome=continuousBiomeColor(s.temperature,rain,evap);
        const shade=clamp(1-2*step*(eastSlope*cos+.65*southSlope)/550+(s.height-base)/180,.5,1.4),rock=clamp((s.height-600)/1700,0,1),i=(row*TILE+col)*8;
        for(let k=0;k<3;k++){
          data[i+k]=(satellite[k]*(1-rock*.35)+[202,199,183][k]*rock*.35)*shade;
          data[i+3+k]=(biome[k]*(1-rock)+[202,199,183][k]*rock)*shade;
        }
        if(s.surfaceColor!==undefined){const blend=1-clamp((meters-8)/120,0,1),ground=coverRGB(s);for(let k=0;k<3;k++)data[i+k]=data[i+k]*(1-blend)+ground[k]*shade*blend;}
        data[i+6]=s.kind|(Math.round(iceCoverage(s.temperature)*31)<<3);
      }
    }
    if(meters>4){await this.drawRivers(data,tile,cancelled);if(cancelled())return null;}
    // 颜色与地形共用一份采样缓存
    while(this.bytes+data.byteLength>this.budget&&this.cache.size){const key=this.cache.keys().next().value;this.bytes-=this.cache.get(key).byteLength;this.cache.delete(key);}
    if(data.byteLength<=this.budget){this.cache.set(cacheKey,data);this.bytes+=data.byteLength;}
    return data;
  }
  async render(tile,layer,cancelled,progress,context={}){
    this.progress=progress;this.cancelled=cancelled??(()=>false);
    if(this.cancelled())return null;
    const w=this.world,n=2**tile.z,center={wx:(tile.x+.5)/n*w.circumference-w.circumference/2,wz:(tile.y+.5)/n*w.circumference-w.circumference/4};
    const focus=tile.z>=13?(context.focusId===undefined?focusLakeAt(w,center.wx,center.wz):context.focusId):null;
    if(focus!==this.focusId){this.focusId=focus;clearTributaryCache(w);}
    let shapes;
    try{shapes=await this.lakes.prepare(tile,this.cancelled,progress);await this.lakes.prepareDetail(tile,this.focusId,this.cancelled,progress);}
    catch(error){if(error.name==='AbortError')return null;throw error;}
    const fields=await this.fields(tile,this.cancelled);if(!fields||cancelled?.())return null;
    const pixels=new Uint8ClampedArray(TILE*TILE*4),offset=layer==='biome'?3:0,mask=lakeMask(shapes,tile,w,this.lakes.activeDetail?this.focusId:null);
    const hasFocus=shapes.some(s=>s.id===this.focusId)&&this.lakes.activeDetail!==null;
    for(let i=0;i<TILE*TILE;i++){
      const packed=fields[i*8+6],kind=packed&7,color=waterColors[kind],alpha=fields[i*8+7]/255;
      const coverage=mask[i],ice=(packed>>>3)/31*(kind?1:alpha),lakeIce=(packed>>>3)/31;
      for(let k=0;k<3;k++){const base=kind?color[k]:fields[i*8+offset+k],water=base*(1-alpha)+waterColors[3][k]*alpha;const ground=water+(ICE_RGB[k]-water)*ice,lake=waterColors[2][k]+(ICE_RGB[k]-waterColors[2][k])*lakeIce;pixels[i*4+k]=ground*(1-coverage)+lake*coverage;}pixels[i*4+3]=255;
    }
    return {pixels,side:TILE,stats:{samples:this.samples,hits:this.hits,bytes:this.bytes,lakes:this.lakes.stats(),focusId:hasFocus?this.focusId:null}};
  }
  async drawRivers(fields,tile,cancelled){
    const w=this.world,n=2**tile.z,span=w.circumference/n,meters=span/TILE,columns=w.columns/n;
    const centerX=(tile.x+.5)/n*w.circumference-w.circumference/2,left=centerX-span/2,top=tile.y/n*w.circumference-w.circumference/4;
    const delta=x=>x-Math.round(x/w.circumference)*w.circumference;
    let processed=0;
    for(const cell of this.rivers){
      const col=cell%w.columns,row=Math.floor(cell/w.columns),cx=(col+.5)/w.columns*w.circumference-w.circumference/2;
      if(Math.abs(delta(cx-centerX))>span/2+w.circumference/w.columns*2||row<tile.y*columns-2||row>(tile.y+1)*columns+2)continue;
      if(processed++%16===0){await pause();if(cancelled())return;}
      const coarse=meters>w.circumference/w.columns/16;
      const curve=coarse?overviewRiver(w,cell):macroCurve(w,cell);if(!curve)continue;
      for(let j=1;j<curve.points.length;j++){
        const a=curve.points[j-1],b=curve.points[j],ax=(centerX+delta(a.wx-centerX)-left)/meters,az=(a.wz-top)/meters,bx=ax+delta(b.wx-a.wx)/meters,bz=(b.wz-top)/meters;
        const cos=Math.max(.01,Math.cos((a.wz+b.wz)/2/w.circumference*TAU)),t=(j-.5)/(curve.points.length-1);
        const flow=w.flow[cell]+(w.flow[curve.receiver]-w.flow[cell])*t,emphasis=riverStrokePixels(flow,this.riverThreshold,tile.z)/3;
        const rx=Math.max(.55,emphasis,curve.halfWidth/meters/cos),ry=Math.max(.55,emphasis,curve.halfWidth/meters);
        const margin=tile.z>=7&&tile.z<=13?1.5:1;
        const x0=clamp(Math.floor(Math.min(ax,bx)-rx*margin-1),0,TILE-1),x1=clamp(Math.ceil(Math.max(ax,bx)+rx*margin+1),0,TILE-1),y0=clamp(Math.floor(Math.min(az,bz)-ry*margin-1),0,TILE-1),y1=clamp(Math.ceil(Math.max(az,bz)+ry*margin+1),0,TILE-1);
        const dx=(bx-ax)/rx,dy=(bz-az)/ry,length=dx*dx+dy*dy;
        for(let y=y0;y<=y1;y++)for(let x=x0;x<=x1;x++){
          const px=(x+.5-ax)/rx,py=(y+.5-az)/ry,t=clamp((px*dx+py*dy)/Math.max(1e-12,length),0,1),d=Math.hypot(px-dx*t,py-dy*t);
          if(d>1.5)continue;const i=(y*TILE+x)*8+7,alpha=clamp((1.5-d)*Math.min(rx,ry),0,1);
          fields[i]=255-(255-fields[i])*(1-alpha);
        }
      }
    }
  }
}
