import {templateField,lakeShoreFeatures} from './lake-templates.mjs';
import {shapes} from './island-template-shapes.mjs';
const p=(x,y,w)=>[x,y,w];

function noise(x,y,seed){
  const ix=Math.floor(x),iy=Math.floor(y),u=x-ix,v=y-iy,s=u*u*(3-2*u),t=v*v*(3-2*v);
  const h=(a,b)=>{let n=Math.imul(a,374761393)^Math.imul(b,668265263)^Math.imul(seed+1,1274126177);n=Math.imul(n^(n>>>13),1274126177);return ((n^(n>>>16))>>>0)/2147483648-1;};
  return (h(ix,iy)*(1-s)+h(ix+1,iy)*s)*(1-t)+(h(ix,iy+1)*(1-s)+h(ix+1,iy+1)*s)*t;
}
const clamp=v=>Math.max(0,Math.min(1,v));
const smooth=v=>{const t=clamp(v);return t*t*(3-2*t);};

function inlandHeight(shape,x,y,land,seed){
  const px=x+.037*noise(x*11,y*11,seed+31)+.011*noise(x*37,y*37,seed+37);
  const py=y+.029*noise(x*14,y*14,seed+47)+.009*noise(x*43,y*43,seed+53);
  let peak=0,sum=0;
  for(let j=0;j<shape.peaks.length;j++){
    const [cx,cy,r,h]=shape.peaks[j],a=(seed*.83+j*2.17),dx=(px-cx)/r,dy=(py-cy)/r;
    const u=(dx*Math.cos(a)+dy*Math.sin(a))*(.67+(seed+j*3)%7*.11);
    const v=(-dx*Math.sin(a)+dy*Math.cos(a))*(.65+(seed*3+j)%5*.17);
    const weather=.16*noise(x*31,y*31,seed+j*13)+.09*noise(x*61,y*61,seed+j*17);
    const distance=.47*Math.abs(u+.31*v)+.44*Math.abs(v-.17*u)+.29*Math.hypot(u,v)+weather;
    const crest=.66+.34*(1-Math.abs(noise(x*23+j,y*27-j,seed+j*29)));
    const hill=h*(1-smooth(distance))*crest;peak=Math.max(peak,hill);sum+=hill;
  }
  const erosion=.7+.22*noise(x*19,y*19,seed+71)+.08*noise(x*43,y*43,seed+83);
  const lowland=4+8*(noise(x*8,y*8,seed+101)+1);
  const beach=.013+.009*(noise(x*11,y*11,seed+113)+1);
  const drainage=1-.43*smooth((.14-Math.abs(noise(x*17,y*19,seed+127)))/.14);
  return smooth(land/beach)*(lowland+(peak+sum*.08)*erosion*drainage);
}

function curve(points){
  if(points.length===2)return [{a:points[0],b:points[1],s:0,l:Math.hypot(points[1][0]-points[0][0],points[1][1]-points[0][1])}];
  const samples=[];
  for(let i=0;i<points.length-1;i++)for(let j=0;j<8;j++){
    const a=points[Math.max(0,i-1)],b=points[i],c=points[i+1],d=points[Math.min(points.length-1,i+2)],t=j/8;
    samples.push(b.map((v,k)=>k===2? v+(c[k]-v)*t : .5*((2*v)+(-a[k]+c[k])*t+(2*a[k]-5*v+4*c[k]-d[k])*t*t+(-a[k]+3*v-3*c[k]+d[k])*t*t*t)));
  }
  samples.push(points.at(-1));let s=0;
  return samples.slice(0,-1).map((a,i)=>{const b=samples[i+1],l=Math.hypot(b[0]-a[0],b[1]-a[1]),seg={a,b,s,l};s+=l;return seg;});
}
function ridge(segments,x,y,seed,rough=true){
  let best=-10,along=0;
  for(const {a,b,s,l} of segments){
    const dx=b[0]-a[0],dy=b[1]-a[1],t=Math.max(0,Math.min(1,((x-a[0])*dx+(y-a[1])*dy)/(l*l||1)));
    const width=a[2]+(b[2]-a[2])*t;
    const d=width-Math.hypot(x-a[0]-dx*t,y-a[1]-dy*t);
    if(d>best){best=d;along=s+t*l;}
  }
  const variation=.6*noise(x*21,y*21,seed|0)+.27*noise(x*49,y*49,(seed|0)+7)+.13*noise(x*111,y*111,(seed|0)+19);
  return {d:best+(rough?.008*variation:0),along};
}

function boundaryField(segments,x,y){
  let inside=false,best=10,along=0;
  for(const {a,b,s,l} of segments){
    if((a[1]>y)!==(b[1]>y)&&x<(b[0]-a[0])*(y-a[1])/(b[1]-a[1])+a[0])inside=!inside;
    const dx=b[0]-a[0],dy=b[1]-a[1],t=Math.max(0,Math.min(1,((x-a[0])*dx+(y-a[1])*dy)/(l*l||1)));
    const d=Math.hypot(x-a[0]-dx*t,y-a[1]-dy*t);
    if(d<best){best=d;along=s+t*l;}
  }
  return {d:best*(inside?1:-1),along};
}

const variants=[13,9,10,14,15,11,9,13,15,10,14,15,13,11,14,15,13,9,14,10];
if(variants.length!==shapes.length)throw Error('岛形与岸段配置数量不匹配');
const forms=variants.map((variant,i)=>({bits:[0,1,0,1],center:false,variant,phase:.7+i*.43,shoreFeatures:lakeShoreFeatures(variant,3951730217+i*1777)}));

const prepared=shapes.map((shape,k)=>{
 const boundary=shape.boundary?.map(([x,y])=>p(x,y,0));
 return {shape,segments:shape.paths.flatMap(curve),cuts:(shape.cuts??[]).flatMap(curve),form:forms[k],perimeter:boundary?curve([boundary.at(-1),...boundary,boundary[0],boundary[1]]).slice(8,-8):null};
});
export function sampleIslandTemplate(k,x,y){
 const {shape,segments,cuts,form,perimeter}=prepared[k];
    const px=x+.019*noise(x*7,y*7,k+137)+.006*noise(x*23,y*23,k+151);
    const py=y+.023*noise(x*6,y*6,k+167)+.005*noise(x*29,y*29,k+179);
    let {d,along}=ridge(segments,px,py,k+191);
    if(perimeter){const f=boundaryField(perimeter,px,py);if(f.d>d){d=f.d;along=f.along;}}
    d+=shape.rough*(.68*noise(px*29,py*29,k+211)+.32*noise(px*73,py*73,k+223));
    const band=.039+(k%5)*.004,u=clamp(.5-d/(2*band));
    const v=((along*(3.4+k*.137)+noise(px*9,py*9,k+239)*.29+k*.173)%1+1)%1;
    const base=2*smooth(u)-1;
    const detail=templateField(form,u,v)-base;
    let land=d-band*.72*detail;
    if(cuts.length)land=Math.min(land,-ridge(cuts,px,py,k,false).d);
    const height=land>0?inlandHeight(shape,x,y,land,k+251):land*700;
 return {height,land};
}
