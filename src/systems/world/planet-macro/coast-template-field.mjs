import {coastVariantCoordinates} from './coast-variants.mjs';
export {coastVariantCoordinates} from './coast-variants.mjs';
import {templateField,lakeShoreFeatures} from './lake-templates.mjs';
import {pureGroups,decorDesigns} from './coast-template-shapes.mjs';
const clamp=v=>Math.max(0,Math.min(1,v)),smooth=v=>{const t=clamp(v);return t*t*(3-2*t);};
function noise(x,y,s){const ix=Math.floor(x),iy=Math.floor(y),u=smooth(x-ix),v=smooth(y-iy);const h=(a,b)=>{let n=Math.imul(a,374761393)^Math.imul(b,668265263)^Math.imul(s+1,1274126177);n=Math.imul(n^(n>>>13),1274126177);return ((n^(n>>>16))>>>0)/2147483648-1;};return (h(ix,iy)*(1-u)+h(ix+1,iy)*u)*(1-v)+(h(ix,iy+1)*(1-u)+h(ix+1,iy+1)*u)*v;}
function curve(ps){const samples=[];for(let i=0;i<ps.length-1;i++)for(let j=0;j<9;j++){const a=ps[Math.max(0,i-1)],b=ps[i],c=ps[i+1],d=ps[Math.min(ps.length-1,i+2)],t=j/9;samples.push(b.map((v,k)=>k===2?v+(c[k]-v)*t:.5*(2*v+(-a[k]+c[k])*t+(2*a[k]-5*v+4*c[k]-d[k])*t*t+(-a[k]+3*v-3*c[k]+d[k])*t*t*t)));}samples.push(ps.at(-1));return samples.slice(0,-1).map((a,i)=>[a,samples[i+1]]);}
function ribbon(lines,x,y){let f=-9;for(const [a,b] of lines){const dx=b[0]-a[0],dy=b[1]-a[1],t=clamp(((x-a[0])*dx+(y-a[1])*dy)/(dx*dx+dy*dy||1));f=Math.max(f,a[2]+(b[2]-a[2])*t-Math.hypot(x-a[0]-t*dx,y-a[1]-t*dy));}return f;}
function coastAt(points,y){
 let i=0;while(i<points.length-2&&y>points[i+1][0])i++;
 const t=clamp((y-points[i][0])/(points[i+1][0]-points[i][0]));
 const a=points[Math.max(0,i-1)][1],b=points[i][1],c=points[i+1][1],d=points[Math.min(points.length-1,i+2)][1];
 return .5*(2*b+(-a+c)*t+(2*a-5*b+4*c-d)*t*t+(-a+3*b-3*c+d)*t*t*t);
}

const palette={mud:0,sand:1,rock:2};
function createPure(g,j){
 const groups=pureGroups;
 const k=g*4+j,d=groups[g],pair=d.kind.split('-');
 const variant=pair.includes('rock')?[9,14,10,15][j]:pair.length===1&&pair[0]==='sand'?[16,17,19,23][j]:[9,13,10,15][j];
 const form={bits:[0,1,0,1],center:false,variant,phase:.71+k*.41,shoreFeatures:lakeShoreFeatures(variant,3951730217+k*811)};
 const weightsAt=(x,y)=>{
  const w=[0,0,0];if(pair.length===1){w[palette[pair[0]]]=1;return w;}
  const t=Math.max(.16,Math.min(.84,.5+.27*noise(x*3.7,y*4.3,k+97)+.12*noise(x*9.1,y*10.7,k+103)+.045*noise(x*24,y*27,k+107)));
  w[palette[pair[0]]]=1-t;w[palette[pair[1]]]=t;return w;
 };
 const coastAtY=y=>{
  const [m,s,r]=weightsAt(.43,y),rough=.007*m+.0015*s+.012*r;
  const lakeDetail=templateField(form,.5,y)+.28;
  return Math.max(.12,Math.min(.85,coastAt(d.profiles[j],y)+rough*(.7*noise(y*23,k*.23,k+31)+.3*noise(y*61,k*.41,k+53))-.017*(m+.18*s+r)*lakeDetail));
 };
 const sample=(x,y)=>{
  const weights=weightsAt(x,y),[m,s,r]=weights,f=coastAtY(y)-x,patch=.5+.5*noise(x*12,y*10,k+131);
  let height;
  if(f>0){
   const mud=(1-Math.exp(-f/.07))*(1.8+patch*3.5)+Math.max(0,.21-x)*11;
   const sand=(1-Math.exp(-f/.09))*(5+patch*8)+Math.max(0,.22-x)*20;
   const rock=(1-Math.exp(-f/.008))*(20+patch*36+Math.max(0,.4-x)*55)+Math.max(0,f-.04)*35;
   height=m*mud+s*sand+r*rock;
  }else{
   const mud=2.6*(1-Math.exp(f/.15))+Math.max(0,-f-.14)*29;
   const sand=18*(1-Math.exp(f/.23)),rock=55*(1-Math.exp(f/.035));
   height=-(m*mud+s*sand+r*rock);
  }
  return {height,weights};
 };
 return sample;
}
function createSingle(k){
 const designs=decorDesigns.slice(0,12),profiles=designs.map(d=>d.profile);
const d=designs[k],landLines=d.land.flatMap(curve),cutLines=d.cuts.flatMap(curve),form={bits:[0,1,0,1],center:false,variant:d.shore,phase:.83+k*.37,shoreFeatures:lakeShoreFeatures(d.shore,3951730217+k*811)};
const sample=(x,y)=>{
 const rough=d.kind==='sand'?.0018:d.kind==='mud'?.006:.011;
 const px=x+(d.kind==='sand'?.003:.008)*noise(x*17,y*17,17+k),py=y+.006*noise(x*13,y*13,39+k);
 const coast=coastAt(profiles[k],y)+.012*noise(y*7.4,k*.43,71);
 let f=coast-px;if(landLines.length)f=Math.max(f,ribbon(landLines,px,py));
 if(cutLines.length)f=Math.min(f,-ribbon(cutLines,px,py));
 f+=rough*(.7*noise(px*31,py*31,83+k)+.3*noise(px*73,py*73,101+k));
 const band=.055,u=clamp(.5-f/(2*band)),v=clamp(y);
 f-=band*(d.kind==='sand'?.08:.31)*(templateField(form,u,v)-(2*smooth(u)-1));
 const patch=.5+.5*noise(x*11,y*9,131+k);
 if(f>0){
  if(d.kind==='mud')return (1-Math.exp(-f/ .065))*(1.8+patch*3.5)+Math.max(0,.25-x)*11;
  if(d.kind==='sand')return (1-Math.exp(-f/.09))*(5+patch*8)+Math.max(0,.22-x)*20;
  const cliff=18+patch*36+Math.max(0,.4-x)*65;
  return (1-Math.exp(-f/.008))*cliff+Math.max(0,f-.04)*40;
 }
 const depth=d.kind==='mud'?2.6*(1-Math.exp(f/.15))+Math.max(0,-f-.14)*29:d.kind==='sand'?18*(1-Math.exp(f/.23)):55*(1-Math.exp(f/.035));
 return -depth;
};
 return sample;
}
function createHybrid(k){
 const designs=decorDesigns.slice(12);
 const d=designs[k],pair=d.kind.split('-'),lines=d.land.map(l=>({...l,segments:curve(l.p)})),cuts=d.cuts.flatMap(curve);
 const form={bits:[0,1,0,1],center:false,variant:d.shore,phase:.73+k*.47,shoreFeatures:lakeShoreFeatures(d.shore,3851730217+k*911)};
 const sample=(x,y)=>{
  const px=x+.006*noise(x*17,y*17,k+41),py=y+.004*noise(x*19,y*19,k+67),coast=coastAt(d.profile,y)+.01*noise(y*8,k*.37,83);
  let t=.5+.27*noise(x*3.7,y*4.3,k+97)+.12*noise(x*9.1,y*10.7,k+103)+.045*noise(x*24,y*27,k+107);
  if(d.foot)t+=.13*noise(x*5.1-y*1.2,y*6.3,k+109);
  t=Math.max(.16,Math.min(.84,t));
  let weights=[0,0,0];weights[palette[pair[0]]]=1-t;weights[palette[pair[1]]]=t;
  const baseField=coast-px;let f=baseField;
  for(const line of lines){const q=ribbon(line.segments,px,py);
   const amount=.23*smooth((q+.09)/.16)*smooth((q-baseField+.05)/.12);f=Math.max(f,q);
   weights=weights.map((v,i)=>v*(1-amount)+(i===palette[line.mat]?amount:0));
  }
  if(cuts.length)f=Math.min(f,-ribbon(cuts,px,py));
  const [m,s,r]=weights,rough=.005*m+.0015*s+.012*r;
  f+=rough*(.7*noise(px*31,py*31,k+109)+.3*noise(px*71,py*71,k+131));
  const u=clamp(.5-f/.11);
  f-=.055*(.23*m+.07*s+.34*r)*(templateField(form,u,clamp(y))-(2*smooth(u)-1));
  const patch=.5+.5*noise(x*12,y*10,k+151);
  let height;
  if(f>0){
   const mud=(1-Math.exp(-f/.07))*(2+patch*3.1)+Math.max(0,.2-x)*9;
   const sand=(1-Math.exp(-f/.08))*(6+patch*8)+Math.max(0,.2-x)*19;
   const rock=(1-Math.exp(-f/.008))*(23+patch*36+Math.max(0,.35-x)*62)+Math.max(0,f-.04)*35;
   height=m*mud+s*sand+r*rock;
  }else{
   const mud=2.6*(1-Math.exp(f/.16))+Math.max(0,-f-.13)*25;
   const sand=18*(1-Math.exp(f/.22)),rock=55*(1-Math.exp(f/.037));
   const near=m*mud+s*sand+r*rock,outer=smooth((-f-.07)/.34);
   height=-(near*(1-outer)+(34+patch*9)*outer);
  }
  return {height,weights};
 };
 return sample;
}

export const COAST_TEMPLATES=Object.freeze([
 ...pureGroups.flatMap(g=>g.names.map(name=>Object.freeze({name,kind:g.kind,family:'pure'}))),
 ...decorDesigns.map(d=>Object.freeze({name:d.name,kind:d.kind,family:'decor'}))
]);
const samplers=COAST_TEMPLATES.map((_,i)=>i<24?createPure(Math.floor(i/4),i%4):i<36?createSingle(i-24):createHybrid(i-36));
export function sampleCoastTemplate(base,variant,u,v){
 const p=coastVariantCoordinates(variant,u,v),sample=samplers[base](p.u,p.v);
 return typeof sample==='number'?{height:sample*p.gain}:{...sample,height:sample.height*p.gain};
}
