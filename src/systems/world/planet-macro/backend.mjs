export const MACRO_RULE='tectonic-macro-experiment';
let ruleVersion='';
export function setRuleVersion(value){ruleVersion=value;}
export function getRuleVersion(){return ruleVersion;}
import {createContinentalCrust} from './continental-crust.mjs';
import {driftProfile} from './drift-profile.mjs';
import {Heap} from './heap.mjs';
import {sampleLakeShore} from './lake-shores.mjs';
import {raiseMacroIslands} from './macro-islands.mjs';
import {buildMountainRidges} from './mountain-ranges.mjs';
import {buildHydrology} from './hydrology.mjs';
import {thermalTransport} from './thermal-transport.mjs';
import {oasisMoisture} from './oasis-moisture.mjs';
import {macroDepth,macroBandWidth,macroRiverThreshold} from './river-dimensions.mjs';
import {sampleCover,sampleCoverAt,finishCover,coverSlope,coverGradient} from './surface-cover.mjs';
import {PLATE_LAYOUT,planetSize,createPlates,assignPlateIds,requirePlateLayout,isCurrentPlateLayout} from './plate-layout.mjs';
const TAU = Math.PI * 2;
const YEAR = 31557600;
// 陆地降水常数源、干燥、底雨与阻挡
const CLIMATE_SOURCE=.03,CLIMATE_SUBSIDE=.06,CONVECTIVE=.08,RAIN_BASE=.05,BLOCK_HEIGHT=150,BLOCK_WIDTH=650,ORO_FINE_SCALE=220;
const gauss=(v,mu,s)=>{const z=(v-mu)/s;return Math.exp(-z*z);};
const clamp = (v, a = 0, b = 1) => Math.max(a, Math.min(b, v));
const smooth = v => { const t = clamp(v); return t * t * (3 - 2 * t); };
const mix = (a, b, t) => a + (b - a) * t;
const dot = (a, b) => a[0]*b[0]+a[1]*b[1]+a[2]*b[2];
const cross = (a, b) => [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]];
const unit = a => { const n = Math.hypot(...a) || 1; return a.map(v => v/n); };
function random(seed) { return () => { let t=seed+=0x6d2b79f5;t=Math.imul(t^t>>>15,t|1);t^=t+Math.imul(t^t>>>7,t|61);return ((t^t>>>14)>>>0)/4294967296; }; }
function hash(x,y,z,seed) { let n=Math.imul(x,73856093)^Math.imul(y,19349663)^Math.imul(z,83492791)^seed;n=Math.imul(n^n>>>16,0x7feb352d);n=Math.imul(n^n>>>15,0x846ca68b);return ((n^n>>>16)>>>0)/2147483648-1; }
function noise(x,y,z,seed) {
  const ix=Math.floor(x),iy=Math.floor(y),iz=Math.floor(z),tx=smooth(x-ix),ty=smooth(y-iy),tz=smooth(z-iz);
  return mix(mix(mix(hash(ix,iy,iz,seed),hash(ix+1,iy,iz,seed),tx),mix(hash(ix,iy+1,iz,seed),hash(ix+1,iy+1,iz,seed),tx),ty),
    mix(mix(hash(ix,iy,iz+1,seed),hash(ix+1,iy,iz+1,seed),tx),mix(hash(ix,iy+1,iz+1,seed),hash(ix+1,iy+1,iz+1,seed),tx),ty),tz);
}
const sphere = (lon,lat) => [Math.cos(lat)*Math.cos(lon),Math.sin(lat),Math.cos(lat)*Math.sin(lon)];
function rotate(p,axis,angle) { const c=Math.cos(angle),s=Math.sin(angle),d=dot(axis,p),v=cross(axis,p);return p.map((x,i)=>x*c+v[i]*s+axis[i]*d*(1-c)); }

export function normalizeOptions(input={}) {
  const options={seed:1934353244,columns:512,plateLayout:PLATE_LAYOUT,majorPlateCount:16,minorPlateCount:4,plateCount:20,activity:5,driftSteps:0,monsoonDirection:90,landRatio:.3,
    recipe:'balanced',equatorChunks:128000,equatorTemperature:30,poleTemperature:-25,precipitation:1,flagCount:16,...input};
  for(const [key,min,max] of [['seed',0,0xffffffff],['columns',64,1024],['activity',0,9],['driftSteps',0,12],['monsoonDirection',0,359],['equatorChunks',256,640000],['flagCount',0,32]])
    if(!Number.isInteger(options[key])||options[key]<min||options[key]>max)throw Error(`参数 ${key} 超出范围`);
  if(options.columns%4)throw Error('列数须为4的倍数');
  const {major,minor,total}=planetSize(options.columns);
  Object.assign(options,{plateLayout:PLATE_LAYOUT,majorPlateCount:major,minorPlateCount:minor,plateCount:total});
  if(options.equatorChunks/options.columns<64)throw Error('最低尺度为64区块/粗格');
  if(!Number.isFinite(options.landRatio)||options.landRatio<0||options.landRatio>1)throw Error('陆占比超出范围');
  if(!Number.isFinite(options.precipitation)||options.precipitation<0||options.precipitation>2)throw Error('降水倍率超出范围');
  if(!['balanced','archipelago','mountains','rift'].includes(options.recipe))throw Error('未知地形配方');
  for(const key of ['equatorTemperature','poleTemperature'])if(!Number.isFinite(options[key])||Math.abs(options[key])>100)throw Error('温度超出范围');
  return options;
}

// 三档高度包络，格宽取档，活动度共用
const HEIGHT_BASE=3800,HEIGHT_EXP=1.504;
const HEIGHT_STOPS=[64,100,160,250,360,480,625];
const HEIGHT_CEILING={
  256:[4096,4096,4096,4096,4096,4096,4096],
  512:[4096,4229,4374,4516,5942,7378,9000],
  1024:[4096,5546,7632,10334,12414,14347,16384]
};
const logMix=(a,b,t)=>Math.exp(Math.log(a)+(Math.log(b)-Math.log(a))*t);
function ceilingAt(values,chunks){
  if(chunks<=HEIGHT_STOPS[0])return values[0];
  const last=HEIGHT_STOPS.length-1;
  if(chunks>=HEIGHT_STOPS[last])return values[last];
  let i=0;while(chunks>HEIGHT_STOPS[i+1])i++;
  return logMix(values[i],values[i+1],Math.log(chunks/HEIGHT_STOPS[i])/Math.log(HEIGHT_STOPS[i+1]/HEIGHT_STOPS[i]));
}
function classBlend(columns){
  if(columns<=256)return [256,256,0];
  if(columns>=1024)return [1024,1024,0];
  const base=columns<=512?256:512,span=base===256?512:1024;
  return [base,span,(Math.log(columns)-Math.log(base))/Math.log(span/base)];
}
export function terrainHeightCap(options){
  const chunks=options.equatorChunks/options.columns,[lo,hi,t]=classBlend(options.columns);
  const low=ceilingAt(HEIGHT_CEILING[lo],chunks),high=ceilingAt(HEIGHT_CEILING[hi],chunks),ceiling=lo===hi?low:logMix(low,high,t);
  return HEIGHT_BASE+(ceiling-HEIGHT_BASE)*Math.pow(clamp(options.activity??5,0,9)/9,HEIGHT_EXP);
}

let gridCache=null,spreadHeap=null;
function grid(options) {
  const w=options.columns,h=w/2,n=w*h,radius=options.equatorChunks*64/TAU;
  if(gridCache?.w===w&&gridCache.radius===radius)return {w,h,n,radius,points:gridCache.points,areas:gridCache.areas.slice(),neighbors:gridCache.neighbors,lengths:gridCache.lengths};
  const points=new Float32Array(n*3),areas=new Float32Array(n),neighbors=new Int32Array(n*8),lengths=new Float32Array(n*8);
  const dirs=[[-1,0],[1,0],[0,-1],[0,1],[-1,-1],[1,-1],[-1,1],[1,1]],cosLon=new Float64Array(w),sinLon=new Float64Array(w);
  for(let x=0;x<w;x++){const lon=(x+.5)/w*TAU-Math.PI;cosLon[x]=Math.cos(lon);sinLon[x]=Math.sin(lon);}
  for(let y=0;y<h;y++){
    const lat=Math.PI/2-(y+.5)*Math.PI/h,cosLat=Math.cos(lat),sinLat=Math.sin(lat),row=y*w;
    const area=radius*radius*(TAU/w)*(Math.sin(Math.PI/2-y*Math.PI/h)-Math.sin(Math.PI/2-(y+1)*Math.PI/h))/1e6;
    areas.fill(area,row,row+w);
    for(let x=0;x<w;x++){const i3=(row+x)*3;points[i3]=cosLat*cosLon[x];points[i3+1]=sinLat;points[i3+2]=cosLat*sinLon[x];}
  }
  const halfW=w>>1,wMask=w-1;
  for(let y=0;y<h;y++){
    const row=y*w,py=points[row*3+1];
    for(let d=0;d<8;d++){
      const dxDir=dirs[d][0];let ny=y+dirs[d][1],xShift=0;
      if(ny<0){ny=-ny-1;xShift=halfW;}else if(ny>=h){ny=h*2-ny-1;xShift=halfW;}
      const nRow=ny*w,dy=py-points[nRow*3+1],dy2=dy*dy;
      for(let x=0;x<w;x++){
        const i=row+x,ix=i*3,j=nRow+((x+dxDir+xShift)&wMask),jx=j*3,dx=points[ix]-points[jx],dz=points[ix+2]-points[jx+2],at=i*8+d;
        neighbors[at]=j;lengths[at]=2*Math.asin(clamp(Math.sqrt(dx*dx+dy2+dz*dz)/2))*radius;
      }
    }
  }
  gridCache={w,radius,points,areas,neighbors,lengths};
  return {w,h,n,radius,points,areas:areas.slice(),neighbors,lengths};
}
function spread(source,g,width) {
  const out=new Float32Array(source);
  if(!spreadHeap||spreadHeap.ids.length<g.n)spreadHeap=new Heap(g.n);else spreadHeap.size=0;
  const heap=spreadHeap;
  for(let i=0;i<g.n;i++)if(out[i]>.025)heap.push(i,-out[i]);
  while(heap.size){const i=heap.pop(),v=-heap.last;if(v<out[i]-1e-6)continue;
    for(let d=0;d<4;d++){const at=i*8+d,j=g.neighbors[at],next=v*Math.exp(-g.lengths[at]/width);
      if(next>.025&&next>out[j]+1e-5){out[j]=next;heap.push(j,-next);}}
  }
  return out;
}

function tectonics(g,o) {
  const plates=createPlates(o,random(o.seed^0x74656374)),ids=new Uint16Array(g.n),warped=new Float32Array(g.n*3);
  const P=driftProfile(o.activity);
  for(const plate of plates)plate.speed=1+(plate.speed-1)*(P.dispersion/.35);
  const upliftSource=new Float32Array(g.n),riftSource=new Float32Array(g.n),shearSource=new Float32Array(g.n),boundary=new Uint8Array(g.n),ridgePushX=new Float32Array(g.n),ridgePushY=new Float32Array(g.n);
  const crust=createContinentalCrust(g,o,plates,noise,random(o.seed^0x63727573),P.span);
  for(const plate of plates)plate.oceanic=crust.continental(plate.start,plate,0)<.5;
  for(let i=0;i<g.n;i++){const i3=i*3,x=g.points[i3],y=g.points[i3+1],z=g.points[i3+2],sx=x*3.6,sy=y*3.6-7,sz=z*3.6+11;
    const qx=Math.fround(x+noise(sx,sy,sz,o.seed)*.10),qy=Math.fround(y+noise(sx+19,sy,sz,o.seed)*.10),qz=Math.fround(z+noise(sx+38,sy,sz,o.seed)*.10),n=Math.hypot(qx,qy,qz)||1;
    warped[i3]=qx/n;warped[i3+1]=qy/n;warped[i3+2]=qz/n;}
  const angle=P.span,steps=o.driftSteps>0?o.driftSteps:Math.round(P.steps),rotationHistory=[];
  const contStep=new Uint8Array(g.n),contCache=new Float64Array(g.n);
  for(let step=0;step<=steps;step++){
    const fraction=step/steps,isLast=step===steps;
    for(const plate of plates)plate.center=rotate(plate.start,plate.axis,angle*plate.speed*fraction);
    rotationHistory.push(plates.map(p=>[...p.center]));
    assignPlateIds(warped,plates,ids);
    if(step===0||!angle)continue;
    const px=g.points,nbr=g.neighbors,act=P.speed;
    for(let i=0;i<g.n;i++){
      const ii=ids[i];
      for(let dir=0;dir<2;dir++){
        const j=nbr[i*8+(dir?3:1)],jj=ids[j];
        if(ii===jj)continue;
        const a=plates[ii],b=plates[jj],i3=i*3,j3=j*3;
        const ax=px[i3],ay=px[i3+1],az=px[i3+2],bx=px[j3],by=px[j3+1],bz=px[j3+2];
        let mx=ax+bx,my=ay+by,mz=az+bz,n=Math.hypot(mx,my,mz)||1;mx/=n;my/=n;mz/=n;
        let nx=bx-ax,ny=by-ay,nz=bz-az;n=Math.hypot(nx,ny,nz)||1;nx/=n;ny/=n;nz/=n;
        const aa=a.axis,bb=b.axis;
        const vax=aa[1]*mz-aa[2]*my,vay=aa[2]*mx-aa[0]*mz,vaz=aa[0]*my-aa[1]*mx;
        const vbx=bb[1]*mz-bb[2]*my,vby=bb[2]*mx-bb[0]*mz,vbz=bb[0]*my-bb[1]*mx;
        const relx=(vax*a.speed-vbx*b.speed)*act,rely=(vay*a.speed-vby*b.speed)*act,relz=(vaz*a.speed-vbz*b.speed)*act;
        const push=relx*nx+rely*ny+relz*nz;
        const cx=my*nz-mz*ny,cy=mz*nx-mx*nz,cz=mx*ny-my*nx;
        const slide=Math.abs(relx*cx+rely*cy+relz*cz);
        const convergence=clamp((push-.08)/1.25),divergence=clamp((-push-.08)/1.25),shear=clamp((slide-.12)/1.6);
        let continental=contStep[i]===step?contCache[i]:(contStep[i]=step,contCache[i]=crust.continentalXYZ(ax,ay,az,a,fraction)),oceanic=1-continental;
        upliftSource[i]=Math.max(upliftSource[i],convergence*(.60+.85*continental)+divergence*oceanic*.38);
        riftSource[i]=Math.max(riftSource[i],divergence*(.25+.75*continental)+convergence*oceanic*.58);
        shearSource[i]=Math.max(shearSource[i],shear);
        continental=contStep[j]===step?contCache[j]:(contStep[j]=step,contCache[j]=crust.continentalXYZ(bx,by,bz,b,fraction));oceanic=1-continental;
        upliftSource[j]=Math.max(upliftSource[j],convergence*(.60+.85*continental)+divergence*oceanic*.38);
        riftSource[j]=Math.max(riftSource[j],divergence*(.25+.75*continental)+convergence*oceanic*.58);
        shearSource[j]=Math.max(shearSource[j],shear);
        if(isLast){
          const type=convergence>.12?1:divergence>.12?2:shear>.12?3:4;boundary[i]=type;boundary[j]=type;
          if(type===1){
            // 球面切向挤压投影
            const along=relx*mx+rely*my+relz*mz,tx=relx-along*mx,ty=rely-along*my,tz=relz-along*mz;
            const el=Math.hypot(mx,mz)||1,ex=-mz/el,ez=mx/el;
            const nx2=-my*mx/el,ny2=el,nz2=-my*mz/el;
            const east=tx*ex+tz*ez,north=tx*nx2+ty*ny2+tz*nz2;
            ridgePushX[i]=-east;ridgePushY[i]=north;ridgePushX[j]=east;ridgePushY[j]=-north;
          }
        }
      }
    }
  }
  const uplift=spread(upliftSource,g,g.radius*P.beltWidth),rift=spread(riftSource,g,g.radius*P.beltWidth*.6),shear=spread(shearSource,g,g.radius*P.beltWidth/3);
  const heights=new Float32Array(g.n),basins=new Float32Array(g.n),crustIds=new Uint8Array(g.n);
  const pCount=plates.length,rotC=new Float64Array(pCount),rotS=new Float64Array(pCount),rotK=new Float64Array(pCount),axX=new Float64Array(pCount),axY=new Float64Array(pCount),axZ=new Float64Array(pCount);
  for(let k=0;k<pCount;k++){const p=plates[k],th=-angle*p.speed,c=Math.cos(th);rotC[k]=c;rotS[k]=Math.sin(th);rotK[k]=1-c;axX[k]=p.axis[0];axY[k]=p.axis[1];axZ[k]=p.axis[2];}
  const s71=o.seed^71;
  for(let i=0;i<g.n;i++){
    const i3=i*3,pxx=g.points[i3],pyy=g.points[i3+1],pzz=g.points[i3+2],pid=ids[i],ax=axX[pid],ay=axY[pid],az=axZ[pid],cc=rotC[pid],ss=rotS[pid],kk=rotK[pid],dd=ax*pxx+ay*pyy+az*pzz;
    const rx=pxx*cc+(ay*pzz-az*pyy)*ss+ax*dd*kk,ry=pyy*cc+(az*pxx-ax*pzz)*ss+ay*dd*kk,rz=pzz*cc+(ax*pyy-ay*pxx)*ss+az*dd*kk,field=crust.fieldXYZ(rx,ry,rz);
    const margin=field.score-crust.reference,continental=smooth(margin/.045+.5),sh=shear[i];basins[i]=field.basin;crustIds[i]=field.core;
    heights[i]=margin*6000+noise(Math.fround(rx)*5,Math.fround(ry)*5,Math.fround(rz)*5,s71)*120+
      uplift[i]*P.upliftScale*(.15+.85*continental)*(1-.85*field.basin)-rift[i]*P.riftDepth+(sh?sh*noise(pxx*18,pyy*18,pzz*18,o.seed)*120:0);
  }
  upliftSource.fill(0);
  if(crust.halfWidthKm>0){
    for(let i=0;i<g.n;i++)for(const d of [1,3]){
      const j=g.neighbors[i*8+d];if(crustIds[i]===crustIds[j])continue;
      const collision=smooth((Math.min(uplift[i],uplift[j])-.7)/.55)*(1-clamp(Math.max(rift[i],rift[j]))),value=1-collision;
      upliftSource[i]=Math.max(upliftSource[i],value);upliftSource[j]=Math.max(upliftSource[j],value);
    }
    const corridors=spread(upliftSource,g,crust.halfWidthKm*700);
    for(let i=0;i<g.n;i++){basins[i]=Math.max(basins[i],corridors[i]);heights[i]-=corridors[i]*(1400+800*(1-o.landRatio));}
  }
  return {ids,plates,rotationHistory,boundary,uplift,rift,shear,ridgePushX,ridgePushY,heights,basins,continents:crust.continents,basinHalfWidthKm:crust.halfWidthKm};
}

function recipes(g,o,t) {
  const rng=random(o.seed^0x72656369),operations=[],areas=g.areas;
  let area=0;for(let i=0;i<g.n;i++)area+=areas[i];
  const src=o.recipe==='mountains'?t.uplift:o.recipe==='rift'?t.rift:null;
  let focus=null;const weights=[];let focusArea=0;
  if(src){
    // 分桶截取前1/8面积候选再排序
    const target=area/8,B=2048,bArea=new Float64Array(B);
    let maxV=0;for(let i=0;i<g.n;i++)if(src[i]>maxV)maxV=src[i];
    if(maxV>0){
      const scale=(B-1)/maxV;
      for(let i=0;i<g.n;i++)bArea[(src[i]*scale)|0]+=areas[i];
      let acc=0,cutB=B-1;
      for(;cutB>=0;cutB--){acc+=bArea[cutB];if(acc>=target)break;}
      const cutoff=cutB>0?cutB/scale:0,cand=[];
      for(let i=0;i<g.n;i++)if(src[i]>=cutoff)cand.push(i);
      cand.sort((a,b)=>src[b]-src[a]||a-b);focus=cand;
    }else focus=Array.from({length:g.n},(_,i)=>i);
    for(let k=0;k<focus.length;k++){focusArea+=areas[focus[k]];weights.push(focusArea);if(focusArea>=target)break;}
  }
  const chooseFocus=()=>{const at=rng()*focusArea;let lo=0,hi=weights.length-1;while(lo<hi){const mid=(lo+hi)>>1;if(weights[mid]<at)lo=mid+1;else hi=mid;}return focus[lo];};
  const count=o.recipe==='archipelago'?24:12;
  for(let k=0;k<count;k++){
    const index=focus?chooseFocus():-1;
    const center=focus?Array.from(g.points.subarray(index*3,index*3+3)):sphere(rng()*TAU,Math.asin(rng()*2-1)),axis=unit(cross(center,sphere(rng()*TAU,Math.asin(rng()*2-1))));
    const side=cross(center,axis),long=o.recipe==='archipelago'?.05+rng()*.09:.12+rng()*.22;
    const amplitude=o.recipe==='rift'?-150-rng()*200:(k%4===0?-1:1)*(70+rng()*240);
    operations.push({center,axis,side,long,wide:long*(.25+rng()*.35),amplitude});
  }
  const pts=g.points,heights=t.heights,basins=t.basins,opCount=operations.length,opHr=new Float64Array(opCount),activeOps=new Int32Array(opCount);
  for(let k=0;k<opCount;k++)opHr[k]=Math.hypot(operations[k].center[0],operations[k].center[2]);
  for(let y=0;y<g.h;y++){
    const row=y*g.w,py=pts[row*3+1],cosLat=Math.sqrt(Math.max(0,1-py*py));let actLen=0;
    for(let k=0;k<opCount;k++)if(py*operations[k].center[1]+cosLat*opHr[k]+1e-6>=.75)activeOps[actLen++]=k;
    if(!actLen)continue;
    for(let x=0;x<g.w;x++){
      const i=row+x,i3=i*3,px=pts[i3],pz=pts[i3+2];let change=0;
      for(let a=0;a<actLen;a++){
        const op=operations[activeOps[a]],c=op.center;
        if(px*c[0]+py*c[1]+pz*c[2]<.75)continue;
        const ax=op.axis,sd=op.side,ux=(px*ax[0]+py*ax[1]+pz*ax[2])/op.long,uy=(px*sd[0]+py*sd[1]+pz*sd[2])/op.wide;
        change+=op.amplitude*Math.exp(-2*(ux*ux+uy*uy));
      }
      heights[i]+=clamp(change,-400,400)*(1-.9*basins[i]);
    }
  }
  let threshold,minH=Infinity,maxH=-Infinity,maxIdx=0,minIdx=0;
  for(let i=0;i<g.n;i++){const h=heights[i];if(h>maxH){maxH=h;maxIdx=i;}if(h<minH||(h===minH&&i>minIdx)){minH=h;minIdx=i;}}
  if(o.landRatio===0)threshold=heights[maxIdx]+1;
  else if(o.landRatio===1)threshold=heights[minIdx]-1;
  else{
    // 分桶定位海平面分位桶再精排
    const target=area*o.landRatio,B=4096,span=maxH-minH;
    if(span<=0)threshold=maxH;
    else{
      const scale=(B-1)/span,bArea=new Float64Array(B);
      for(let i=0;i<g.n;i++)bArea[((heights[i]-minH)*scale)|0]+=areas[i];
      let acc=0,cutB=B-1;
      for(;cutB>=0;cutB--){acc+=bArea[cutB];if(acc>=target)break;}
      const bHigh=Math.min(B-1,cutB+1),bLow=Math.max(0,cutB-1);
      const highCut=bHigh===B-1?Infinity:minH+(bHigh+1)/scale,lowCut=bLow===0?-Infinity:minH+bLow/scale;
      let land=0;const mid=[];
      for(let i=0;i<g.n;i++){const h=heights[i];if(h>highCut)land+=areas[i];else if(h>=lowCut)mid.push(i);}
      mid.sort((a,b)=>heights[b]-heights[a]||a-b);
      for(let k=0;k<mid.length;k++){const i=mid[k];land+=areas[i];threshold=heights[i];if(land>=target)break;}
    }
  }
  let basinArea=0,basinSeaArea=0;
  for(let i=0;i<g.n;i++){
    heights[i]=clamp(heights[i]-threshold,-3000,3800);
    if(basins[i]>.8){basinArea+=areas[i];if(heights[i]<=0)basinSeaArea+=areas[i];}
  }
  t.basinSeaRatio=basinArea?basinSeaArea/basinArea:1;
  return operations;
}

export function climateBase(g,o,heights,terrain={}) {
  const temperature=new Float32Array(g.n),rainfall=new Float32Array(g.n),evaporation=new Float32Array(g.n),moisture=new Float32Array(g.n);
  const index=(x,y)=>{if(y<0){y=-y-1;x+=g.w/2;}if(y>=g.h){y=g.h*2-y-1;x+=g.w/2;}return y*g.w+((x%g.w)+g.w)%g.w;};
  const read=(values,x,y)=>{const ix=Math.floor(x),iy=Math.floor(y),tx=x-ix,ty=y-iy;return mix(mix(values[index(ix,iy)],values[index(ix+1,iy)],tx),mix(values[index(ix,iy+1)],values[index(ix+1,iy+1)],tx),ty);};
  const land=new Float32Array(g.n);
  for(let y=0;y<g.h;y++)for(let x=0;x<g.w;x++){
    const i=y*g.w+x,lat=Math.PI/2-(y+.5)*Math.PI/g.h;
    temperature[i]=o.poleTemperature+(o.equatorTemperature-o.poleTemperature)*Math.cos(lat)**1.4-Math.max(0,heights[i])*.0065;
    land[i]=Math.max(0,heights[i]);
  }
  const barrier=terrain.peaks?Float32Array.from(terrain.peaks,(peak,i)=>land[i]+(peak-land[i])*.65):land;
  // 陆地梯度只算一次，海面按 0 不参与抬升
  const gradE=new Float32Array(g.n),gradN=new Float32Array(g.n);
  for(let y=0;y<g.h;y++)for(let x=0;x<g.w;x++){
    const i=y*g.w+x;gradE[i]=(read(land,x+1,y)-read(land,x-1,y))*.5;gradN[i]=(read(land,x,y+1)-read(land,x,y-1))*.5;
  }
  const passes=Math.max(24,Math.min(96,g.w/4)),mrad=(o.monsoonDirection??90)*Math.PI/180;
  const windX=new Float32Array(g.n),windY=new Float32Array(g.n),liftGain=new Float32Array(g.n),leeLoss=new Float32Array(g.n),leeShadow=new Float32Array(g.n);
  // 风场：连续纬向、季风、地形阻挡绕流
  for(let y=0;y<g.h;y++){
    const lat=Math.PI/2-(y+.5)*Math.PI/g.h,deg=Math.abs(lat)*180/Math.PI,cosLat=Math.max(.25,Math.cos(lat));
    const east=-gauss(deg,12,15)+gauss(deg,48,14)-.8*gauss(deg,80,9);
    const merid=Math.sign(lat)*(.35*gauss(deg,18,15)-.22*gauss(deg,45,15)+.25*gauss(deg,72,12));
    const monsoon=gauss(deg,18,13)*.6;
    for(let x=0;x<g.w;x++){
      const i=y*g.w+x;
      const ax=east/cosLat+monsoon*Math.cos(mrad),ay=merid+monsoon*Math.sin(mrad);
      let ahead=0,behind=0;
      for(let k=1;k<=3;k++){
        ahead=Math.max(ahead,read(barrier,x+ax*k,y+ay*k)-land[i]);
        behind=Math.max(behind,read(barrier,x-ax*k,y-ay*k)-land[i]);
      }
      // 迎风阻挡越强绕流越明显，背风下沉减雨
      const blocked=smooth(clamp((ahead-BLOCK_HEIGHT)/BLOCK_WIDTH)),shadow=smooth(clamp(behind/BLOCK_WIDTH));
      const tx=-gradN[i],ty=gradE[i],tl=Math.hypot(tx,ty)||1,sign=ax*tx+ay*ty>=0?1:-1,speed=Math.hypot(ax,ay)||1;
      windX[i]=ax*(1-.6*blocked)+tx/tl*sign*.7*blocked*speed;
      windY[i]=ay*(1-.6*blocked)+ty/tl*sign*.7*blocked*speed;
      const lift=ax*gradE[i]+ay*gradN[i];
      liftGain[i]=Math.max(0,lift)*(1+.8*blocked)/1700;
      leeLoss[i]=Math.max(0,-lift)*(.5+1.2*shadow)/1700;
      leeShadow[i]=shadow;
    }
  }
  const fraction=terrain.fraction??Float32Array.from(heights,v=>v>0?1:0);
  const heatOffset=thermalTransport(g,o,land,terrain.peaks??land,fraction,windX,windY);
  for(let i=0;i<g.n;i++){
    temperature[i]+=heatOffset[i];
    evaporation[i]=clamp(650+(temperature[i]-5)*28,80,1900);
  }
  // 固定风场的输送权重只算一次
  const upwind=new Int32Array(g.n*4),transport=new Float32Array(g.n*4);
  for(let y=0;y<g.h;y++)for(let x=0;x<g.w;x++){
    const i=y*g.w+x,sx=x-windX[i],sy=y-windY[i],ix=Math.floor(sx),iy=Math.floor(sy),tx=sx-ix,ty=sy-iy,k=i*4;
    upwind.set([index(ix,iy),index(ix+1,iy),index(ix,iy+1),index(ix+1,iy+1)],k);
    transport.set([(1-tx)*(1-ty),tx*(1-ty),(1-tx)*ty,tx*ty],k);
  }
  let vapor=new Float32Array(g.n),next=new Float32Array(g.n);
  for(let i=0;i<g.n;i++)vapor[i]=heights[i]<=0?1:0;
  const decay=Math.exp(-.005*128/g.w);
  for(let pass=0;pass<passes;pass++)for(let y=0;y<g.h;y++){
    const deg=Math.abs(Math.PI/2-(y+.5)*Math.PI/g.h)*180/Math.PI;
    const base=RAIN_BASE*128/g.w*(.5+.65*gauss(deg,0,16)+.4*gauss(deg,50,13)-.3*gauss(deg,28,9));
    for(let x=0;x<g.w;x++){
      const i=y*g.w+x,k=i*4,up=vapor[upwind[k]]*transport[k]+vapor[upwind[k+1]]*transport[k+1]+vapor[upwind[k+2]]*transport[k+2]+vapor[upwind[k+3]]*transport[k+3];
      const rate=up*clamp(base+liftGain[i]-leeLoss[i],0,.75);
      rainfall[i]+=rate;
      const recycled=Math.max(0,up-rate)*decay+CLIMATE_SOURCE*clamp(evaporation[i]/1200,0,1)*clamp(up*.8+.2,.2,1);
      next[i]=heights[i]<=0?.65+.35*clamp((temperature[i]+15)/45):clamp(recycled-CLIMATE_SUBSIDE*leeShadow[i],0,1);
    }
    if(y===g.h-1)[vapor,next]=[next,vapor];
  }
  for(let i=0;i<g.n;i++){
    const deg=Math.abs(Math.PI/2-(Math.floor(i/g.w)+.5)*Math.PI/g.h)*180/Math.PI;
    rainfall[i]=(rainfall[i]/passes+CONVECTIVE*clamp(evaporation[i]/1200,0,1)*clamp(vapor[i]*1.5,.35,1)*(.35+.8*gauss(deg,22,22)))*18000*(g.w/128)*o.precipitation;
    moisture[i]=clamp(rainfall[i]/Math.max(150,evaporation[i]),0,2)/2;
  }
  return {temperature,rainfall,evaporation,moisture,windE:windX,windN:windY,shadow:leeShadow,heatOffset};
}

function calibrateRainfall(g,o,field){
  let total=0,area=0;for(let i=0;i<g.n;i++){total+=field.rainfall[i]*g.areas[i];area+=g.areas[i];}
  const mean=total/area,target=985.5*o.precipitation,factor=mean>0?target/mean:0;
  for(let i=0;i<g.n;i++){field.rainfall[i]*=factor;field.moisture[i]=clamp(field.rainfall[i]/Math.max(150,field.evaporation[i]),0,2)/2;}
  field.rainfallCalibration={meanMmYear:mean>0?target:0,baselineMmYear:985.5,scale:factor};return field;
}
export function climate(g,o,heights) {
  if(g.w<=128){const out=climateBase(g,o,heights);delete out.heatOffset;return calibrateRainfall(g,o,out);}
  const w=128,h=64,scale=g.w/w,coarse=new Float32Array(w*h);
  const rowCos=new Float32Array(g.h),peaks=new Float32Array(w*h),fraction=new Float32Array(w*h);
  for(let sy=0;sy<g.h;sy++)rowCos[sy]=Math.cos(Math.PI/2-(sy+.5)*Math.PI/g.h);
  for(let y=0;y<h;y++)for(let x=0;x<w;x++){
    let sum=0,weight=0,landWeight=0,peak=0;
    for(let dy=0;dy<scale;dy++){
      const sy=y*scale+dy,a=rowCos[sy],row=sy*g.w+x*scale;
      for(let dx=0;dx<scale;dx++){const value=heights[row+dx];sum+=value*a;weight+=a;if(value>0)landWeight+=a;peak=Math.max(peak,value);}
    }
    coarse[y*w+x]=sum/weight;
    peaks[y*w+x]=peak;fraction[y*w+x]=landWeight/weight;
  }
  const field=climateBase({w,h,n:w*h},o,coarse,{peaks,fraction}),out={temperature:new Float32Array(g.n),rainfall:new Float32Array(g.n),evaporation:new Float32Array(g.n),moisture:new Float32Array(g.n)};
  const index=(x,y)=>{if(y<0){y=-y-1;x+=w/2;}if(y>=h){y=h*2-y-1;x+=w/2;}return y*w+((x%w)+w)%w;};
  const fineIndex=(x,y)=>{if(y<0){y=-y-1;x+=g.w/2;}if(y>=g.h){y=g.h*2-y-1;x+=g.w/2;}return y*g.w+((x%g.w)+g.w)%g.w;};
  const land=new Float32Array(g.n);
  for(let i=0;i<g.n;i++)land[i]=Math.max(0,heights[i]);
  for(let y=0;y<g.h;y++){
    // 基温按行算，热量距平插值
    const lat=Math.PI/2-(y+.5)*Math.PI/g.h;
    const baseTemperature=o.poleTemperature+(o.equatorTemperature-o.poleTemperature)*Math.cos(lat)**1.4;
    for(let x=0;x<g.w;x++){
      const i=y*g.w+x,sx=(x+.5)/scale-.5,sy=(y+.5)/scale-.5,ix=Math.floor(sx),iy=Math.floor(sy),tx=sx-ix,ty=sy-iy;
      const bigScale=mix(mix(field.rainfall[index(ix,iy)],field.rainfall[index(ix+1,iy)],tx),
        mix(field.rainfall[index(ix,iy+1)],field.rainfall[index(ix+1,iy+1)],tx),ty);
      // 迎风抬升：粗网格风场取最近格，坡向用 4 格跨距
      const near=index(Math.round(sx),Math.round(sy));
      const windE=field.windE[near],windN=field.windN[near],shadow=field.shadow[near];
      const speed=Math.sqrt(windE*windE+windN*windN)||1;
      const east=land[fineIndex(x+2,y)]-land[fineIndex(x-2,y)],south=land[fineIndex(x,y+2)]-land[fineIndex(x,y-2)];
      const slope=(windE*east+windN*south)*.25/speed;
      out.rainfall[i]=bigScale*clamp(1+slope/ORO_FINE_SCALE*(slope>0?1.25:1.15*(.5+.5*shadow)),.2,3);
      const heat=mix(mix(field.heatOffset[index(ix,iy)],field.heatOffset[index(ix+1,iy)],tx),mix(field.heatOffset[index(ix,iy+1)],field.heatOffset[index(ix+1,iy+1)],tx),ty);
      out.temperature[i]=baseTemperature-Math.max(0,heights[i])*.0065+heat;
      out.evaporation[i]=clamp(650+(out.temperature[i]-5)*28,80,1900);
      out.moisture[i]=clamp(out.rainfall[i]/Math.max(150,out.evaporation[i]),0,2)/2;
    }
  }
  return calibrateRainfall(g,o,out);
}

// 连续低地走廊，阻力只计算一次
export function macroValleys(g,o,height){
  const original=height.slice(),friction=new Float32Array(g.n).fill(1),scale=g.radius/260000;
  for(let i=0;i<g.n;i++){
    const h=original[i];if(h<=0)continue;
    const k=i*3,x=g.points[k]*scale,y=g.points[k+1]*scale,z=g.points[k+2]*scale;
    const signed=noise(x,y,z,o.seed^0x62d4a73b)*.8+noise(x*.37,y*.37,z*.37,o.seed^0x76616c32)*.2;
    const valley=Math.exp(-signed*signed/0.009);
    let low=h,high=h;
    for(let d=0;d<8;d++){const v=Math.max(0,original[g.neighbors[i*8+d]]);low=Math.min(low,v);high=Math.max(high,v);}
    const gentle=1-smooth((high-low-80)/320),inland=smooth(h/60),mountain=1-smooth((h-900)/1300);
    const floor=Math.min(h,Math.max(1,low+.25));
    height[i]=Math.max(floor,h-valley*75*gentle*inland*mountain);
    friction[i]=Math.exp(signed*1.5-valley*1.7*gentle);
  }
  return friction;
}

function drainage(g,o,height,c,friction) {
  return buildHydrology(g,o,height,c,i=>friction[i]);
}

function biomes(g,t,c,h) {
  const out=new Uint8Array(g.n);
  for(let i=0;i<g.n;i++)out[i]=t.heights[i]<=0?0:h.water[i]===2?1:c.temperature[i]<-8?2:c.rainfall[i]<250?3:c.temperature[i]>23&&c.rainfall[i]>1400?4:c.temperature[i]>18?5:c.moisture[i]>.65?6:c.temperature[i]<7?7:8;
  return out;
}

function macroContext(world,wx,wz) {
  const {columns:w,rows:h,circumference:C}=world;
  let x=(wx/C+.5)*w-.5,y=(wz/C+.25)*w-.5;
  const index=(ix,iy)=>{iy=((iy%(h*2))+h*2)%(h*2);if(iy>=h){iy=h*2-iy-1;ix+=w/2;}return iy*w+((ix%w)+w)%w;};
  const ix=Math.floor(x),iy=Math.floor(y),tx=x-ix,ty=y-iy;
  const poleDistance=Math.min(Math.abs(wz+C/4),Math.abs(wz-C/4)),span=C/w;
  return {a:index(ix,iy),b:index(ix+1,iy),c:index(ix,iy+1),d:index(ix+1,iy+1),tx,ty,pole:poleDistance<span?smooth(poleDistance/span):-1,poleId:wz<0?0:1};
}
function readMacro(world,c,field){
  const values=world[field];let value=mix(mix(values[c.a],values[c.b],c.tx),mix(values[c.c],values[c.d],c.tx),c.ty);
  if(c.pole>=0)value=mix(world.poles[field]?.[c.poleId]??value,value,c.pole);
  return value;
}
export function macroAt(world,wx,wz,field='height'){return readMacro(world,macroContext(world,wx,wz),field);}
/** 同一坐标的字段共享插值上下文 */
export function macroAtMany(world,wx,wz,fields){
  const context=macroContext(world,wx,wz),out={};
  for(const field of fields)out[field]=readMacro(world,context,field);
  return out;
}
/** 稀疏取样：陆面抬高与湖掩码分开，湖掩码为最贵一步 */
export function landHeight(world,wx,wz){return sampleSurface(world,wx,wz,true,false,false).height;}
export function lakeMask(world,wx,wz,land){
  const context=macroContext(world,wx,wz);
  if(land<0||world.water[context.a]===1)return {kind:0,water:-Infinity};
  const lake=sampleLakeShore(world,wx,wz,land,readMacro(world,context,'height'),context,sampleNaturalHeight,true);
  return lake?{kind:lake.kind,water:lake.water}:{kind:0,water:-Infinity};
}

export function sampleMacroChannels(world,wx,wz,context=null){
  const c=context??macroContext(world,wx,wz),gradient=coverGradient(world,c);
  return {height:readMacro(world,c,'height'),rainfall:readMacro(world,c,'rainfall'),evaporation:readMacro(world,c,'evaporation'),eastSlope:gradient.east,southSlope:gradient.south};
}

function coverAtNode(world,wx,wz){
  const context=macroContext(world,wx,wz),p=sphere(wx/world.circumference*TAU,-wz/world.circumference*TAU);
  const uplift=readMacro(world,context,'uplift')*(context.pole>=0?context.pole:1);
  return sampleCover(world,p,{temperature:readMacro(world,context,'temperature'),rainfall:readMacro(world,context,'rainfall'),evaporation:readMacro(world,context,'evaporation'),waterAccess:oasisMoisture(world,wx,wz),uplift,slope:coverSlope(world,context)});
}

export function regionalRelief(world,wx,wz,context=null,point=null,baseValue=null) {
  const lon=wx/world.circumference*TAU,lat=-wz/world.circumference*TAU,p=point??sphere(lon,lat),base=baseValue??macroAt(world,wx,wz);
  // 连续球面噪声，无独立排水汇
  const g=world.heightScale??1,ref=160*g;
  const coast=smooth(Math.abs(base)/ref),uplift=clamp(context?readMacro(world,context,'uplift'):macroAt(world,wx,wz,'uplift'));
  const roughness=(base>0?18+uplift*150:12)*g;
  const layer=(scale,seed)=>noise(p[0]*world.radius/scale,p[1]*world.radius/scale,p[2]*world.radius/scale,world.options.seed^seed);
  return coast*roughness*(layer(12000,0x72656731)*.55+layer(3000,0x72656732)*.30+layer(750,0x72656733)*.15);
}

export function sampleNaturalHeight(world,wx,wz){
  const context=macroContext(world,wx,wz),base=readMacro(world,context,'height');
  const p=sphere(wx/world.circumference*TAU,-wz/world.circumference*TAU),r=world.radius;
  const detail=world.heightScale??1;
  return base+(regionalRelief(world,wx,wz,context,p,base)+noise(p[0]*r/42,p[1]*r/42,p[2]*r/42,world.options.seed^0x64657461)*1.5*detail*smooth(Math.abs(base)/(30*detail)));
}

export function sampleSurface(world,wx,wz,details=true,carveRivers=false,shapeLakes=true,channels=false,cover=details) {
  const {columns:w,rows:h,circumference:C,radius}=world,x=(wx/C+.5)*w-.5,y=(wz/C+.25)*w-.5;
  const lat=-wz/C*TAU,lon=wx/C*TAU,p=sphere(lon,lat),ix=Math.round(x),iy=Math.round(y);
  const index=(a,b)=>{if(b<0){b=-b-1;a+=w/2;}if(b>=h){b=h*2-b-1;a+=w/2;}return clamp(b,0,h-1)*w+((a%w)+w)%w;};
  const context=macroContext(world,wx,wz),cell=index(ix,iy);let height=readMacro(world,context,'height'),water=-Infinity,kind=0,river=null,riverDistance=Infinity,salinity=null,saltLakeId=null;
  const naturalBase=height;
  if(details){
    const detail=world.heightScale??1;
    height+=regionalRelief(world,wx,wz,context,p,height)+noise(p[0]*radius/42,p[1]*radius/42,p[2]*radius/42,world.options.seed^0x64657461)*1.5*detail*smooth(Math.abs(height)/(30*detail));
  }
  // 滨海低地跨宏观格填海水
  if(height<0&&(world.water[cell]===1||world.water[context.a]===1||world.water[context.b]===1||world.water[context.c]===1||world.water[context.d]===1)){water=0;kind=1;}
  if(shapeLakes&&kind!==1){
    const lake=sampleLakeShore(world,wx,wz,height,naturalBase,context,sampleNaturalHeight,details);
    if(lake){height=lake.height;water=lake.water;kind=lake.kind;salinity=lake.salinity;saltLakeId=lake.saltLakeId;}
  }
  if(carveRivers)for(let dz=-1;dz<=1;dz++)for(let dx=-1;dx<=1;dx++){
    const i=index(ix+dx,iy+dz),r=world.receiver[i],width=world.riverWidth[i];if(!width||r<0)continue;
    let ax=((i%w+.5)/w-.5)*C,bx=((r%w+.5)/w-.5)*C;
    const wrap=v=>((v+C/2)%C+C)%C-C/2;
    ax=wx+wrap(ax-wx);bx=ax+wrap(bx-ax);
    const az=(Math.floor(i/w)+.5)/h*C/2-C/4,bz=(Math.floor(r/w)+.5)/h*C/2-C/4;
    const cos=Math.max(.01,Math.cos(lat)),vx=(bx-ax)*cos,vz=bz-az,px=(wx-ax)*cos,pz=wz-az;
    const u=clamp((px*vx+pz*vz)/Math.max(1,vx*vx+vz*vz)),distance=Math.hypot(px-vx*u,pz-vz*u);
    if(distance>width*2+8)continue;
    const level=(kind===1||kind===2)&&Number.isFinite(water)?water:mix(world.spill[i],world.spill[r],u),influence=1-smooth((distance-width)/(width+8)),depth=macroDepth(world.flow[i]);
    height=mix(height,Math.min(height,level-depth*(1-smooth(distance/(width*1.2)))),influence);
    if(distance<width&&height<level){
      water=Math.max(water,level);kind=3;
      if(distance/width<riverDistance){
        riverDistance=distance/width;
        const a=sphere(ax/C*TAU,-az/C*TAU),b=sphere(bx/C*TAU,-bz/C*TAU),velocity=unit(cross(cross(a,b),p));
        const east=[-Math.sin(lon),0,Math.cos(lon)],north=[-Math.sin(lat)*Math.cos(lon),Math.cos(lat),-Math.sin(lat)*Math.sin(lon)];
        river={cell:i,receiver:r,dischargeM3s:world.flow[i],downstreamDischargeM3s:world.flow[r],widthM:width*2,directionEast:dot(velocity,east),directionNorth:dot(velocity,north)};
      }
    }
  }
  if(kind===1)salinity='salt';else if(kind===3){salinity='fresh';saltLakeId=null;}
  const temperature=readMacro(world,context,'temperature'),surface={height,water,kind,cell,temperature,river,wetland:0,salinity,saltLakeId};
  if(details&&cover){
    if(!(water>height))Object.assign(surface,sampleCoverAt(world,wx,wz,coverAtNode));
    finishCover(surface);
  }
  if(channels)surface.macro=sampleMacroChannels(world,wx,wz,context);
  return surface;
}

function flags(world,g,o) {
  if(!o.flagCount)return {sites:[],checked:0,columns:0};
  const candidates=[];
  for(let i=0;i<g.n;i++){
    if(world.height[i]<12||world.height[i]>1400||world.water[i]||world.temperature[i]<-3||world.temperature[i]>34)continue;
    let slope=0,wet=false;for(let d=0;d<8;d++){const j=g.neighbors[i*8+d];slope=Math.max(slope,Math.abs(world.height[i]-world.height[j])/Math.max(1,g.lengths[i*8+d]));wet ||= world.water[j]===1||world.water[j]===2;}
    if(slope>.04||wet)continue;
    const lat=Math.PI/2-(Math.floor(i/g.w)+.5)*Math.PI/g.h;if(Math.abs(lat)>Math.PI*.42)continue;
    const score=1-slope/.04+world.moisture[i]*.4-Math.abs(world.temperature[i]-18)/45;
    const area=Math.cos(lat),draw=Math.max(Number.EPSILON,(hash(i,0,0,o.seed^0x464c4147)+1)/2);
    candidates.push({i,score,key:-Math.log(draw)/Math.max(.001,area*(.5+score))});
  }
  candidates.sort((a,b)=>a.key-b.key||a.i-b.i);
  const sites=[];let checked=0,columns=0;
  const minDistance=128*64;
  for(const {i,score} of candidates){
    if(sites.length>=o.flagCount||checked>=256)break;
    const p=g.points.subarray(i*3,i*3+3);if(sites.some(s=>Math.acos(clamp(dot(p,s.point),-1,1))*g.radius<minDistance))continue;
    const wx=((i%g.w+.5)/g.w-.5)*world.circumference,wz=(Math.floor(i/g.w)+.5)/g.h*world.circumference/2-world.circumference/4;
    const cx=Math.floor(wx/64),cz=Math.floor(wz/64);let min=Infinity,max=-Infinity,ok=true;checked++;
    const probe=(x,z)=>{columns++;const s=sampleSurface(world,cx*64+x,cz*64+z);min=Math.min(min,s.height);max=Math.max(max,s.height);return max-min<4&&s.water<s.height;};
    outer:for(const z of [0,16,32,48,63])for(const x of [0,16,32,48,63])if(!probe(x,z)){ok=false;break outer;}
    if(ok){outer:for(let z=0;z<64;z++)for(let x=0;x<64;x++)if(!probe(x,z)){ok=false;break outer;}}
    if(ok)sites.push({cell:i,cx,cz,point:Array.from(p),score,relief:max-min,height:(min+max)/2});
  }
  return {sites,checked,columns};
}

export function dataBytes(value) {
  const seen=new Set();let bytes=0;
  const visit=x=>{if(ArrayBuffer.isView(x)){if(!seen.has(x.buffer)){seen.add(x.buffer);bytes+=x.buffer.byteLength;}return;}if(x&&typeof x==='object')for(const child of Object.values(x))visit(child);};visit(value);return bytes;
}
export function transferBuffers(value) { const set=new Set();const visit=x=>{if(ArrayBuffer.isView(x)){set.add(x.buffer);return;}if(x&&typeof x==='object')for(const child of Object.values(x))visit(child);};visit(value);return [...set]; }
export function fingerprint(world) {
  let hash=2166136261;
  const add=bytes=>{let local=hash;for(let i=0;i<bytes.length;i++){local^=bytes[i];local=Math.imul(local,16777619);}hash=local;};
  for(const key of Object.keys(world).sort())if(ArrayBuffer.isView(world[key])){const v=world[key];add(new Uint8Array(v.buffer,v.byteOffset,v.byteLength));}
  add(new TextEncoder().encode(JSON.stringify([world.rule,world.ruleVersion,world.options,world.plates,world.rotationHistory,world.operations,world.flags,world.poles,world.continents])));
  if(world.islands!==undefined)add(new TextEncoder().encode(JSON.stringify(world.islands)));
  if(world.macroIslands!==undefined)add(new TextEncoder().encode(JSON.stringify(world.macroIslands)));
  if(world.mountainRanges!==undefined)add(new TextEncoder().encode(JSON.stringify(world.mountainRanges)));
  if(world.mountainLandforms!==undefined)add(new TextEncoder().encode(JSON.stringify(world.mountainLandforms)));
  if(world.oases!==undefined)add(new TextEncoder().encode(JSON.stringify(world.oases)));
  return (hash>>>0).toString(16).padStart(8,'0');
}
export function validateWorld(world) {
  const n=world.height.length;let invalid=0,cycles=0,uphill=0,terminalArea=0,terminalFlow=0,landArea=0,riverCells=0,maxDepth=0;
  if(!isCurrentPlateLayout(world))invalid++;
  if(!Array.isArray(world.continents)||world.continents.length<4||world.continents.length>world.plates.length)invalid++;
  else for(const core of world.continents){
    if(!Number.isInteger(core.owner)||core.owner<0||core.owner>=world.plates.length||!Number.isFinite(core.bias))invalid++;
    for(const p of [core.start,core.center])if(!Array.isArray(p)||p.length!==3||p.some(v=>!Number.isFinite(v))||Math.abs(Math.hypot(...p)-1)>.001)invalid++;
  }
  for(let i=0;i<n;i++){
    for(const key of ['height','temperature','rainfall','flow','spill'])if(!Number.isFinite(world[key][i]))invalid++;
    if(world.plate[i]>=world.plates.length)invalid++;
    const r=world.receiver[i];if(r>=0){if(r>=n||world.rank[r]>=world.rank[i])cycles++;if(world.spill[r]>world.spill[i]+.01)uphill++;}
    else {terminalArea+=world.catchment[i];terminalFlow+=world.flow[i];}
    if(world.height[i]>0)landArea+=world.areas[i];if(world.riverWidth[i]>0)riverCells++;
    if(world.water[i]===2)maxDepth=Math.max(maxDepth,(world.lakes[world.lakeId[i]]?.level??world.spill[i])-world.height[i]);
  }
  const s=world.stats,input=s.runoffInput+(s.lakeRainM3s??0)-(s.lakeRunoffReplacedM3s??0);
  if(s.waterBalanceVersion===2){
    let rain=0,replaced=0,loss=0;
    const differs=(a,b)=>!Number.isFinite(a)||!Number.isFinite(b)||Math.abs(a-b)>1e-8*Math.max(1,Math.abs(a),Math.abs(b));
    for(const lake of world.lakes){
      if(differs(lake.inflowM3s,lake.outflowM3s+lake.evaporationM3s)||differs(world.flow[lake.exitCell],lake.outflowM3s))invalid++;
      rain+=lake.rainfallM3s;replaced+=lake.landRunoffReplacedM3s;loss+=lake.evaporationM3s;
    }
    if(differs(rain,s.lakeRainM3s)||differs(replaced,s.lakeRunoffReplacedM3s)||differs(loss,s.lakeLossM3s)||differs(terminalFlow,s.runoffOutput))invalid++;
  }
  const output=terminalFlow+(s.waterBalanceVersion===2?(s.lakeLossM3s??0)+(s.dryBasinLossM3s??0)+(s.riverLossM3s??0):0);
  const conservation=Math.abs(input-output)/Math.max(1e-9,input),areaError=Math.abs(terminalArea-landArea)/Math.max(1e-9,landArea);
  return {ok:!invalid&&!cycles&&!uphill&&conservation<1e-9&&areaError<1e-9,invalid,cycles,uphill,conservation,areaError,riverCells,maxLakeDepth:maxDepth};
}

export function generateWorld(input={},progress=()=>{}) {
  const options=normalizeOptions(input),timings={},start=performance.now();
  const stage=(name,fn)=>{progress(name);const at=performance.now(),result=fn();timings[name]=performance.now()-at;return result;};
  const g=stage('球面网格',()=>grid(options)),t=stage('板块漂移与碰撞',()=>tectonics(g,options));
  const mountainRanges=stage('山脉脊线',()=>buildMountainRidges(g,options,t.heights,t.uplift,t.boundary,t.ridgePushX,t.ridgePushY));
  const operations=stage('简化地形配方',()=>recipes(g,options,t));
  const macroIslands=stage('宏观格取岛',()=>raiseMacroIslands(g,options,t.heights,t.boundary));
  const valleyFriction=stage('河谷走廊',()=>macroValleys(g,options,t.heights));
  const heightCap=terrainHeightCap(options),heightScale=heightCap/HEIGHT_BASE;
  const c=stage('风带与降水',()=>climate(g,options,t.heights)),h=stage('流域与河湖',()=>drainage(g,options,t.heights,c,valleyFriction));
  if(heightScale!==1){
    // 高度放大后补足气温递减
    for(let i=0;i<t.heights.length;i++){const old=t.heights[i];t.heights[i]=old>0?old*heightScale:Math.max(-3000,old*heightScale);c.temperature[i]-=Math.max(0,t.heights[i]-old)*.0065;}
    for(let i=0;i<h.spill.length;i++)h.spill[i]*=heightScale;
    for(const lake of h.lakes){lake.level*=heightScale;lake.maxDepth*=heightScale;}
    for(const island of macroIslands){island.peakHeight*=heightScale;island.baseHeight*=heightScale;}
  }
  const world={rule:MACRO_RULE,ruleVersion,options,columns:g.w,rows:g.h,circumference:options.equatorChunks*64,radius:g.radius,heightScale,heightCap,
    plate:t.ids,plates:t.plates,rotationHistory:t.rotationHistory,boundary:t.boundary,uplift:t.uplift,rift:t.rift,shear:t.shear,height:t.heights,
    areas:g.areas,...c,receiver:h.receiver,rank:h.rank,spill:h.spill,flow:h.flow,catchment:h.catchment,riverWidth:h.riverWidth,lakeId:h.lakeId,water:h.water,lakes:h.lakes,
    biome:biomes(g,t,c,h),continents:t.continents,operations,macroIslands,mountainRanges:mountainRanges.ridges,mountainLandforms:mountainRanges.landforms,oases:h.oases,ridgePushX:t.ridgePushX,ridgePushY:t.ridgePushY,poles:{},flags:[],stats:{runoffInput:h.input,runoffOutput:h.output,terminal:h.terminal},timings};
  for(const key of ['height','temperature','rainfall','moisture','evaporation'])world.poles[key]=[0,1].map(pole=>{
    let sum=0;for(let x=0;x<g.w;x++)sum+=world[key][(pole?g.h-1:0)*g.w+x];return sum/g.w;});
  world.stats.islands=world.macroIslands.length;
  const flagged=stage('置旗地精确验收',()=>flags(world,g,options));world.flags=flagged.sites;
  const totalArea=g.areas.reduce((a,b)=>a+b,0),landArea=g.areas.reduce((sum,a,i)=>sum+(world.height[i]>0?a:0),0);
  Object.assign(world.stats,{cells:g.n,areaKm2:totalArea,landRatio:landArea/totalArea,lakes:h.lakes.length,lakeCells:h.water.reduce((s,v)=>s+(v===2?1:0),0),
    saltLakes:h.saltBasins,dryBasins:h.dryBasins,lakeLossM3s:h.lakeLoss,lakeRainM3s:h.lakeRain,lakeRunoffReplacedM3s:h.lakeRunoff,dryBasinLossM3s:h.dryLoss,riverLossM3s:h.riverLoss,waterBalanceVersion:2,
    shallowCandidates:h.shallowCandidates,plainLakes:h.plainLakes,oases:h.oases.length,flagCandidatesChecked:flagged.checked,flagColumns:flagged.columns,
    mountainRanges:mountainRanges.ridges.length,mountainRangePeak:Math.round(mountainRanges.peak*heightScale),filledCells:0,
    mountainBranches:mountainRanges.ridges.filter(r=>r.kind==='branch').length,mountainLandforms:mountainRanges.landforms.length,
    mountainKernelCount:mountainRanges.kernels,mountainStampSamples:mountainRanges.samples,
    majorPlates:options.majorPlateCount,minorPlates:options.minorPlateCount,
    continentalSeeds:t.continents.length,basinSeaRatio:t.basinSeaRatio,basinHalfWidthKm:t.basinHalfWidthKm,requestedFlags:options.flagCount,foundFlags:world.flags.length,climateColumns:Math.min(128,g.w)});
  world.audit=stage('一致性检查',()=>validateWorld(world));world.fingerprint=stage('数据指纹',()=>fingerprint(world));
  if(world.lakeTopology)world.lakeTopology.source=world.fingerprint;
  world.stats.typedArrayBytes=dataBytes(world);timings.total=performance.now()-start;
  if(!world.audit.ok)throw Error('宏观世界检查失败：'+JSON.stringify(world.audit));
  return world;
}

export function encodeSnapshot(world) {
  const fields={},metadata={};
  for(const [key,value] of Object.entries(world)){if(ArrayBuffer.isView(value))fields[key]={type:value.constructor.name,data:Array.from(value)};else metadata[key]=value;}
  return JSON.stringify({metadata,fields});
}
export function decodeSnapshot(text) {
  const {metadata,fields}=JSON.parse(text),types={Float32Array,Float64Array,Uint8Array,Uint16Array,Uint32Array,Int32Array};
  if(metadata?.rule!=='tectonic-macro-experiment')throw Error('未知宏观规则');
  requirePlateLayout(metadata);
  const options=normalizeOptions(metadata.options);
  if(metadata.columns!==options.columns||metadata.rows!==options.columns/2||metadata.circumference!==options.equatorChunks*64||metadata.radius!==metadata.circumference/TAU)throw Error('宏观坐标身份不匹配');
  const world={...metadata};for(const [key,item] of Object.entries(fields)){if(!types[item.type])throw Error('未知数组类型');world[key]=new types[item.type](item.data);}
  if(fingerprint(world)!==world.fingerprint||!validateWorld(world).ok)throw Error('快照损坏');return world;
}

export function binarySnapshotParts(world) {
  const metadata={},fields=[],data=[];let offset=0;
  for(const [key,value] of Object.entries(world)){
    if(ArrayBuffer.isView(value)){
      const padding=(8-offset%8)%8;if(padding){data.push(new Uint8Array(padding));offset+=padding;}
      fields.push({key,type:value.constructor.name,length:value.length,offset});
      data.push(new Uint8Array(value.buffer,value.byteOffset,value.byteLength));offset+=value.byteLength;
    }else metadata[key]=value;
  }
  const json=new TextEncoder().encode(JSON.stringify({metadata,fields,endian:'little'})),start=Math.ceil((8+json.length)/8)*8;
  const header=new Uint8Array(start);header.set([80,77,65,67]);new DataView(header.buffer).setUint32(4,json.length,true);header.set(json,8);
  return {parts:[header,...data],byteLength:start+offset,headerBytes:start};
}

export function decodeBinarySnapshot(buffer) {
  if(!(buffer instanceof ArrayBuffer)||buffer.byteLength<8)throw Error('二进制快照不完整');
  const head=new Uint8Array(buffer,0,4);if(head[0]!==80||head[1]!==77||head[2]!==65||head[3]!==67)throw Error('未知二进制快照');
  const length=new DataView(buffer).getUint32(4,true),start=Math.ceil((8+length)/8)*8;
  if(start>buffer.byteLength)throw Error('二进制快照头损坏');
  const {metadata,fields,endian}=JSON.parse(new TextDecoder().decode(new Uint8Array(buffer,8,length)));
  if(endian!=='little'||new Uint8Array(new Uint16Array([1]).buffer)[0]!==1)throw Error('不支持该字节序');
  if(metadata?.rule!=='tectonic-macro-experiment')throw Error('未知宏观规则');
  requirePlateLayout(metadata);
  const options=normalizeOptions(metadata.options);
  if(metadata.columns!==options.columns||metadata.rows!==options.columns/2||metadata.circumference!==options.equatorChunks*64||metadata.radius!==metadata.circumference/TAU)throw Error('宏观坐标身份不匹配');
  const types={Float32Array,Float64Array,Uint8Array,Uint16Array,Uint32Array,Int32Array},world={...metadata},n=metadata.columns*metadata.rows;
  for(const field of fields){const Type=types[field.type];
    if(!Type||!Number.isInteger(field.offset)||field.offset<0||field.length!==n||start+field.offset+n*Type.BYTES_PER_ELEMENT>buffer.byteLength)throw Error('二进制字段损坏');
    world[field.key]=new Type(buffer,start+field.offset,n);
  }
  if(fingerprint(world)!==world.fingerprint||!validateWorld(world).ok)throw Error('快照损坏');return world;
}

