import {coastRaster,COAST_RASTER_SIZE,COAST_TEMPLATE_META} from './coast-template-raster.mjs';
import {coastVariantCoordinates} from './coast-variants.mjs';
import {coastHeightScale} from './coast-scale.mjs';
import {mainRiverField} from './river-continuity.mjs';
import {islandCoastCatalog} from './island-coasts.mjs';

const caches=new WeakMap(),clamp=v=>Math.max(0,Math.min(1,v)),smooth=v=>{const t=clamp(v);return t*t*(3-2*t);};
const kinds=['mud','sand','rock','mud-sand','mud-rock','sand-rock'];
export const COAST_VARIANT_COUNT=192;
export const PURE_COAST_SHARE=.8;
export {COAST_TEMPLATE_META};
function hash(v){v=Math.imul(v^(v>>>16),0x7feb352d);v=Math.imul(v^(v>>>15),0x846ca68b);return (v^(v>>>16))>>>0;}
function cell(w,x,y){return y<0||y>=w.rows?-1:y*w.columns+((x%w.columns)+w.columns)%w.columns;}
function wrap(v,W){return ((v+W/2)%W+W)%W-W/2;}
function macroSample(w,gx,gz){
  const x=Math.floor(gx-.5),y=Math.floor(gz-.5),u=gx-.5-x,v=gz-.5-y;
  const a=w.height[cell(w,x,y)],b=w.height[cell(w,x+1,y)],c=w.height[cell(w,x,y+1)],d=w.height[cell(w,x+1,y+1)];
  const span=w.circumference/w.columns;
  const dx=((b-a)*(1-v)+(d-c)*v)/span,dz=((c-a)*(1-u)+(d-b)*u)/span;
  return {height:(a*(1-u)+b*u)*(1-v)+(c*(1-u)+d*u)*v,dx,dz,slope:Math.hypot(dx,dz)};
}

export function coastCatalog(w){
  let catalog=caches.get(w);if(catalog)return catalog;
  const W=w.columns,H=w.rows,span=w.circumference/W,blocked=new Uint8Array(W*H),protectedCells=new Uint8Array(W*H),entries=new Map(),bins=new Map(),paths=[],patches=[];
  catalog={blocked,protectedCells,entries,bins,paths,patches,pureLength:0,totalLength:0};caches.set(w,catalog);
  const rivers=mainRiverField(w),islandCells=islandCoastCatalog(w).cells;
  for(const {landCell,seaCell} of rivers.mouths.values()){
    const x=landCell%W,y=Math.floor(landCell/W),sy=Math.floor(seaCell/W);let dx=seaCell%W-x;dx=wrap(dx,W);
    for(let z=Math.min(y,sy);z<=Math.max(y,sy);z++)for(let d=Math.min(0,dx);d<=Math.max(0,dx);d++){const i=cell(w,x+d,z);if(i>=0)blocked[i]=1;}
  }
  for(let y=0;y<H;y++)for(let x=0;x<W;x++){
    const i=y*W+x;let safe=!blocked[i]&&!rivers.widths[i]&&!islandCells.has(i)&&y>0&&y<H-1;
    for(let dz=-1;dz<=1&&safe;dz++)for(let dx=-1;dx<=1;dx++){const n=cell(w,x+dx,y+dz);if(n>=0&&w.water[n]===2)safe=false;}
    protectedCells[i]=safe?0:1;
  }
  const nodes=new Map(),segments=[];
  function node(x,y){
    x=((x%W)+W)%W;const key=Math.round(x*1e8)+':'+Math.round(y*1e8);let n=nodes.get(key);
    if(!n){n={x,y,edges:[]};nodes.set(key,n);}return n;
  }
  function segment(a,b){if(a===b)return;const s={a,b,used:false};a.edges.push(s);b.edges.push(s);segments.push(s);}
  for(let y=0;y<H-1;y++)for(let x=0;x<W;x++){
    const ids=[cell(w,x,y),cell(w,x+1,y),cell(w,x+1,y+1),cell(w,x,y+1)],h=ids.map(i=>w.height[i]);
    const mask=h.reduce((m,v,i)=>m|(v>0?1<<i:0),0);if(mask===0||mask===15||!ids.some(i=>w.water[i]===1)||ids.some(i=>w.water[i]===2))continue;
    const pos=[[x+.5,y+.5],[x+1.5,y+.5],[x+1.5,y+1.5],[x+.5,y+1.5]],cross=[];
    for(let e=0;e<4;e++){
      const j=(e+1)%4;if((h[e]>0)===(h[j]>0))continue;
      const t=h[e]/(h[e]-h[j]);cross[e]=node(pos[e][0]+(pos[j][0]-pos[e][0])*t,pos[e][1]+(pos[j][1]-pos[e][1])*t);
    }
    const present=cross.filter(Boolean);
    if(present.length===2)segment(present[0],present[1]);
    else if(present.length===4){const pairs=((mask===5)===(h.reduce((s,v)=>s+v,0)>0))?[[0,1],[2,3]]:[[3,0],[1,2]];for(const [a,b] of pairs)segment(cross[a],cross[b]);}
  }
  function trace(first,start){
    const points=[start];let current=start,next=first;
    while(next&&!next.used){next.used=true;current=next.a===current?next.b:next.a;points.push(current);next=current.edges.find(e=>!e.used);}
    const geometry=[];let length=0;
    for(let i=1;i<points.length;i++){
      const a=points[i-1],b=points[i],dx=wrap(b.x-a.x,W),dz=b.y-a.y,l=Math.hypot(dx,dz)*span;if(l<1e-7)continue;
      const midX=((a.x+dx*.5)%W+W)%W,midZ=Math.max(.50001,Math.min(H-.50001,a.y+dz*.5));
      const gradient=macroSample(w,midX,midZ),side=gradient.dx*(-dz)+gradient.dz*dx>=0?1:-1;
      geometry.push({x:a.x,z:a.y,dx,dz,nx:-dz*span/l*side,nz:dx*span/l*side,length:l,arc:length});length+=l;
    }
    if(!geometry.length)return;
    const count=Math.max(1,Math.round(length/(span*.75))),path={id:paths.length,length,period:length/count,closed:points[0]===points.at(-1),patches:[]};paths.push(path);
    for(let i=0;i<count;i++){
      const seed=hash(w.options.seed^Math.imul(path.id+1,0x45d9f3b)^Math.imul(i+1,0x27d4eb2d));
      const p={seed,base:seed%6*4+((seed>>>9)&3),variant:(seed>>>17)&3,geology:seed%6,length:0};path.patches.push(p);patches.push(p);
    }
    for(const s of geometry){
      s.path=path;const radius=path.period/(span*2);
      const cuts=[0,1];
      for(const [a,d] of [[s.x,s.dx],[s.z,s.dz]])if(Math.abs(d)>1e-12)for(let edge=Math.floor(Math.min(a,a+d))+1;edge<Math.max(a,a+d);edge++)cuts.push((edge-a)/d);
      for(let edge=(Math.floor(s.arc/path.period)+1)*path.period;edge<s.arc+s.length;edge+=path.period)cuts.push((edge-s.arc)/s.length);
      cuts.sort((a,b)=>a-b);
      for(let k=1;k<cuts.length;k++){
        const t=(cuts[k-1]+cuts[k])/2,i=cell(w,Math.floor(s.x+s.dx*t),Math.floor(s.z+s.dz*t));
        if(i>=0&&!protectedCells[i])path.patches[Math.min(count-1,Math.floor((s.arc+s.length*t)/path.period))].length+=(cuts[k]-cuts[k-1])*s.length;
      }
      for(let y=Math.floor(Math.min(s.z,s.z+s.dz)-radius);y<=Math.floor(Math.max(s.z,s.z+s.dz)+radius);y++)for(let x=Math.floor(Math.min(s.x,s.x+s.dx)-radius);x<=Math.floor(Math.max(s.x,s.x+s.dx)+radius);x++){
        const i=cell(w,x,y);if(i<0||protectedCells[i])continue;
        if(!bins.has(i))bins.set(i,[]);bins.get(i).push(s);
      }
    }
  }
  for(const s of segments)if(!s.used&&(s.a.edges.length===1||s.b.edges.length===1))trace(s,s.a.edges.length===1?s.a:s.b);
  for(const s of segments)if(!s.used)trace(s,s.a);
  const ranked=patches.filter(p=>p.length>0).sort((a,b)=>a.seed-b.seed),total=ranked.reduce((s,p)=>s+p.length,0),target=total*PURE_COAST_SHARE;
  let sum=0,bestError=target,cut=0;
  for(let i=0;i<ranked.length;i++){sum+=ranked[i].length;const error=Math.abs(sum-target);if(error<bestError){bestError=error;cut=i+1;}}
  for(let i=0;i<ranked.length;i++)if(i>=cut)ranked[i].base+=24;
  catalog.totalLength=total;catalog.pureLength=ranked.slice(0,cut).reduce((s,p)=>s+p.length,0);
  const scale=coastHeightScale(w);
  for(const [i,list] of bins){const p=list[0].path.patches[0];entries.set(i,{cell:i,base:p.base,variant:p.variant,geology:p.geology,scale});}
  return catalog;
}

export function sampleCoastRaster(base,variant,u,v){
  const p=coastVariantCoordinates(variant,u,v),grid=coastRaster(base),size=COAST_RASTER_SIZE,last=size-1;
  const x=p.u*last,z=p.v*last,ix=Math.min(last-1,Math.floor(x)),iz=Math.min(last-1,Math.floor(z)),dx=x-ix,dz=z-iz,i=iz*size+ix;
  return ((grid[i]*(1-dx)+grid[i+1]*dx)*(1-dz)+(grid[i+size]*(1-dx)+grid[i+size+1]*dx)*dz)*p.gain;
}
function pathStyle(path,arc){
  let q=arc/path.period;if(path.closed)q=((q%path.patches.length)+path.patches.length)%path.patches.length;else q=Math.max(0,Math.min(path.patches.length-1e-10,q));
  const index=Math.floor(q),t=q-index,n=path.patches.length;
  const patch=i=>path.patches[path.closed?(i%n+n)%n:Math.max(0,Math.min(n-1,i))];
  if(t<.18){const q=(t+.18)/.36;return {a:patch(index-1),b:patch(index),va:1-.18*(1-q)**2,vb:.18*q*q,blend:smooth(q)};}
  if(t>.82){const q=(t-.82)/.36;return {a:patch(index),b:patch(index+1),va:1-.18*(1-q)**2,vb:.18*q*q,blend:smooth(q)};}
  return {a:patch(index),b:null,va:t,vb:0,blend:0};
}
function targetAt(path,arc,u){
  const s=pathStyle(path,arc),a=sampleCoastRaster(s.a.base,s.a.variant,clamp(u),s.va);
  return s.b?a+(sampleCoastRaster(s.b.base,s.b.variant,clamp(u),s.vb)-a)*s.blend:a;
}
function protection(w,gx,gz,catalog){
  const x=Math.floor(gx),y=Math.floor(gz);if(catalog.protectedCells[cell(w,x,y)])return 0;let value=1;
  for(let dz=-1;dz<=1;dz++)for(let dx=-1;dx<=1;dx++){
    const i=cell(w,x+dx,y+dz);if(i<0||!catalog.protectedCells[i])continue;
    const ex=Math.max(x+dx-gx,0,gx-(x+dx+1)),ez=Math.max(y+dz-gz,0,gz-(y+dz+1));
    value=Math.min(value,smooth(Math.hypot(ex,ez)/.14));
  }
  return value;
}
export function sampleCoast(w,wx,wz,natural,baseHeight=null){
  const C=w.circumference,W=w.columns,span=C/W,gx=((wx/C+.5)*W%W+W)%W,gz=(wz/C+.25)*W,catalog=coastCatalog(w),i=cell(w,Math.floor(gx),Math.floor(gz));
  if(!catalog.entries.has(i))return null;
  const protect=protection(w,gx,gz,catalog);if(protect<=0)return null;
  let closest=null,best=Infinity,along=0,side=1;
  for(const s of catalog.bins.get(i)??[]){
    const dx=wrap(gx-s.x,W),dz=gz-s.z,t=clamp((dx*s.dx+dz*s.dz)/(s.dx*s.dx+s.dz*s.dz));
    const distance=Math.hypot(dx-s.dx*t,dz-s.dz*t)*span;
    if(distance<best){best=distance;closest=s;along=s.arc+t*s.length;side=(dx-s.dx*t)*s.nx+(dz-s.dz*t)*s.nz>=0?1:-1;}
  }
  if(!closest||best>=closest.path.period*.5)return null;
  const macro=macroSample(w,gx,gz),path=closest.path,u=.5-side*best/path.period;
  const weight=protect*smooth(u/.12)*smooth((1-u)/.12),scale=coastHeightScale(w),target=targetAt(path,along,u)*scale,height=natural+(target-natural)*weight;
  const s=pathStyle(path,along),p=s.blend>.5&&s.b?s.b:s.a;
  return {height,kind:height<0?1:0,water:height<0?0:-Infinity,cell:i,base:p.base,variant:p.variant,geology:p.geology,u,v:along/path.period%1,weight,
    period:path.period,arc:along,normalMetersPerUnit:path.period,alongMetersPerUnit:path.period,strokeWidth:Math.max(8,Math.min(48,span*.001)),macroSlope:macro.slope,scale,_path:path};
}
function noise(x,y,seed){
  const ix=Math.floor(x),iy=Math.floor(y),u=smooth(x-ix),v=smooth(y-iy),h=(a,b)=>hash(Math.imul(a,374761393)^Math.imul(b,668265263)^seed)/2147483648-1;
  return (h(ix,iy)*(1-u)+h(ix+1,iy)*u)*(1-v)+(h(ix,iy+1)*(1-u)+h(ix+1,iy+1)*u)*v;
}
function styleColor(style,coast){
  const pair=kinds[style.geology].split('-'),keys=['mud','sand','rock'],weights=[0,0,0];
  if(pair.length===1)weights[keys.indexOf(pair[0])]=1;
  else{const t=clamp(.5+.27*noise(coast.u*3.7,coast.arc/coast.period*4.3,97)+.12*noise(coast.u*9.1,coast.arc/coast.period*10.7,103));weights[keys.indexOf(pair[0])]=1-t;weights[keys.indexOf(pair[1])]=t;}
  const colors=[[153,134,94],[225,209,156],[139,148,135]];return colors[0].map((_,j)=>colors.reduce((s,c,k)=>s+c[j]*weights[k],0));
}
export function coastStrokeWeight(coast){
  if(!coast||!coast._path||coast.height<0)return 0;
  const e=1/(COAST_RASTER_SIZE-1),path=coast._path;
  const du=(targetAt(path,coast.arc,coast.u+e)-targetAt(path,coast.arc,coast.u-e))/(2*e)*coast.scale;
  const dv=(targetAt(path,coast.arc+e*coast.period,coast.u)-targetAt(path,coast.arc-e*coast.period,coast.u))/(2*e)*coast.scale;
  const slope=Math.hypot(du*coast.weight-coast.macroSlope*coast.period*(1-coast.weight),dv*coast.weight);
  const distance=Math.abs(coast.height)*coast.period/Math.max(1e-6,slope);
  return coast.weight*(1-smooth((distance/coast.strokeWidth-.25)/.75));
}
export function coastTint(coast,rgb){
  const amount=coastStrokeWeight(coast)*.82;if(amount<=0)return rgb;
  const s=pathStyle(coast._path,coast.arc),a=styleColor(s.a,coast),b=s.b?styleColor(s.b,coast):a;
  return rgb.map((v,j)=>Math.round(v*(1-amount)+(a[j]+(b[j]-a[j])*s.blend)*amount));
}
