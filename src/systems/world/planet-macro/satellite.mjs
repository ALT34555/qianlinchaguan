import {landTypeId} from './chunk-metadata.mjs';
import {landMaterialWeights} from './surface-cover.mjs';
const climate=['#144a74','#B0D8E8','#6B8E99','#1a6840','#1a6840','#96c24e'];
const material=['#bbb5ac','#80766e','#5e5314','#b2bbbe','#f6dead','#7a7374'];
const rgb=hex=>[1,3,5].map(i=>parseInt(hex.slice(i,i+2),16));
const climateRGB=climate.map(rgb),materialRGB=material.map(rgb),cache=new WeakMap();
const rainStops=[[200,.1],[400,.3],[800,.55],[1200,.75],[1600,.9]];
const smooth=(a,b,v)=>{const t=Math.max(0,Math.min(1,(v-a)/(b-a)));return t*t*(3-2*t);};
function rainWeight(rainfall){
  let a=.1;
  for(let k=1;k<rainStops.length;k++){
    const [low,x]=rainStops[k-1],[high,y]=rainStops[k];
    if(rainfall>=high){a=y;continue;}
    if(rainfall>low)a=x+(y-x)*smooth(low,high,rainfall);break;
  }
  return a;
}
export function climateWeight(code,rainfall){
  const land=Math.floor(Number(code)/100),surface=Math.floor(Number(code)/10)%10;
  if(land===0)return 1;
  let a=rainWeight(rainfall);
  if(land===1)a=Math.max(.5,a);
  if([1,3,4,5].includes(surface))a=Math.min(.6,a);
  return a;
}
export function satelliteColor(code,rainfall){
  const land=Math.floor(Number(code)/100),surface=Math.floor(Number(code)/10)%10;
  if(land===0)return [...climateRGB[0]];
  const a=climateWeight(code,rainfall),c=climateRGB[land],m=materialRGB[surface];
  return c.map((value,k)=>Math.round(value*a+m[k]*(1-a)));
}
// 分类保留编号，显示混合权重
export function continuousSatelliteColor(temperature,rainfall,evaporation,relief,slope,relative=0){
  const values=landMaterialWeights(temperature,rainfall/Math.max(150,evaporation),relief,slope,relative),c=[...climateRGB[1]],limits=[-8,8,18,24];
  for(let i=0;i<limits.length;i++){const blend=smooth(limits[i]-2,limits[i]+2,temperature);for(let k=0;k<3;k++)c[k]+=(climateRGB[i+2][k]-c[k])*blend;}
  const cold=1-smooth(-10,-6,temperature),rain=rainWeight(rainfall),out=[0,0,0];
  for(let material=1;material<=5;material++){
    const warm=material===2?rain:Math.min(.6,rain),frozen=material===2?Math.max(.5,rain):Math.min(.6,Math.max(.5,rain)),alpha=warm+(frozen-warm)*cold;
    for(let k=0;k<3;k++)out[k]+=values[material]*(c[k]*alpha+materialRGB[material][k]*(1-alpha));
  }
  return out;
}
export function continuousBiomeColor(temperature,rainfall,evaporation){
  const moisture=Math.max(0,Math.min(2,rainfall/Math.max(150,evaporation)))/2,arid=1-smooth(200,300,rainfall),cold=1-smooth(-10,-6,temperature);
  const out=[67+46*(1-moisture),109+28*moisture,66+17*(1-moisture)],desert=[184,155,96],ice=[195,209,203];
  for(let k=0;k<3;k++){out[k]+=(desert[k]-out[k])*arid;out[k]+=(ice[k]-out[k])*cold;}
  return out;
}
// 全览只读宏观数组，不生成区块
export function satelliteMap(world){
  if(cache.has(world))return cache.get(world);
  const start=performance.now(),w=world.columns,h=world.rows,span=world.circumference/w,pixels=new Uint8ClampedArray(w*h*4),codes=new Uint16Array(w*h);
  const at=(x,y)=>{if(y<0){y=-y-1;x+=w/2;}if(y>=h){y=h*2-y-1;x+=w/2;}return y*w+(x+w)%w;};
  for(let y=0;y<h;y++)for(let x=0;x<w;x++){
    const i=y*w+x,height=world.height[i];let code,color,shade=1;
    if(height<=0||world.water[i]===1)code=32;
    else if(world.water[i]===2)code=2;
    else{
      const left=world.height[at(x-1,y)],right=world.height[at(x+1,y)],up=world.height[at(x,y-1)],down=world.height[at(x,y+1)],cos=Math.max(.01,Math.cos((.5-(y+.5)/h)*Math.PI));
      const sx=(right-left)/(2*span*cos),sz=(down-up)/(2*span),slope=Math.max(Math.abs(sx),Math.abs(sz));
      let low=height,high=height,total=0;
      for(let dz=-1;dz<=1;dz++)for(let dx=-1;dx<=1;dx++){const value=world.height[at(x+dx,y+dz)];low=Math.min(low,value);high=Math.max(high,value);total+=value;}
      const relief=Math.min(high-low,slope*64*Math.SQRT2),relative=(height-(total-height)/8)*(32/span)**2;
      code=landTypeId(world.temperature[i],world.rainfall[i],world.evaporation[i],relief,slope,relative);
      color=continuousSatelliteColor(world.temperature[i],world.rainfall[i],world.evaporation[i],relief,slope,relative);
      // 混合近坡与宽坡明暗，详见同名文档
      const near=(left-right+.65*(up-down))/1000;
      const broad=(world.height[at(x-2,y)]-world.height[at(x+2,y)]+.65*(world.height[at(x,y-2)]-world.height[at(x,y+2)]))/2000;
      shade=Math.max(.38,Math.min(1.5,1+2.4*near+1.2*broad));
    }
    codes[i]=code;color??=satelliteColor(code,world.rainfall[i]);
    for(let k=0;k<3;k++)pixels[i*4+k]=color[k]*shade;pixels[i*4+3]=255;
  }
  const result={pixels,codes,elapsed:performance.now()-start,scope:'Macro approximation; no fine chunks generated'};cache.set(world,result);return result;
}
