// 地势着色三模式与等高距，详见同名文档
import {TAU} from './tiles.mjs';

/** 像素索引版本 */
export const RELIEF_VERSION=2;
/** 四档：无光影、拟真光照、高度图、渐变等高线 */
export const RELIEF_MODES=['none','sun','height','contour'];
export function normalizeReliefMode(value){
  if(value==='sun'||value==='height'||value==='contour'||value==='none')return value;
  // 高度图contour兼容交由存档层处理
  return 'none';
}
export function normalizeSunHours(value){
  const n=Number(value);if(!Number.isFinite(n))return 10.5;
  const t=((n%24)+24)%24;return t===0?24:Math.round(t*60)/60;
}

const DEG=Math.PI/180;
const clamp01=v=>v<0?0:v>1?1:v;
const ramp=stops=>{
  const table=new Uint8Array(768);
  for(let i=0;i<256;i++){
    const t=i/255;let k=0;
    while(k<stops.length-2&&t>stops[k+1][0])k++;
    const [p0,c0]=stops[k],[p1,c1]=stops[k+1],q=clamp01((t-p0)/(p1-p0||1));
    for(let j=0;j<3;j++)table[i*3+j]=Math.round(c0[j]+(c1[j]-c0[j])*q);
  }
  return table;
};
// 等高线色带：低地绿，中段橄榄土黄，高处赭褐到石灰
export const LAND=ramp([[0,[224,232,210]],[.18,[196,214,172]],[.36,[160,193,136]],[.52,[132,170,116]],
  [.68,[180,186,128]],[.80,[196,172,122]],[.88,[182,148,108]],[.95,[168,152,140]],[1,[224,224,216]]]);
export const ROCK=ramp([[0,[171,155,135]],[.5,[162,154,144]],[1,[214,214,206]]]);
export const WATER=ramp([[0,[40,80,132]],[.16,[46,102,152]],[.34,[52,124,164]],[.52,[60,146,176]],[.72,[76,168,182]],[1,[116,198,198]]]);
export const ICE=ramp([[0,[138,176,198]],[.5,[170,204,222]],[1,[240,248,251]]]);
export const SNOW=ramp([[0,[232,236,231]],[1,[247,251,252]]]);
export const NIGHT=ramp([[0,[10,15,29]],[.3,[20,30,51]],[.6,[44,60,92]],[.85,[92,108,134]],[1,[168,176,188]]]);
export const TINT=ramp([[0,[255,150,86]],[.5,[255,192,134]],[1,[255,218,180]]]);
export const LIGHT_DAY=[252,251,246],LIGHT_NIGHT=[158,176,214];

/** 色带取色：按世界高程对数归一，平原与高峰都有层次 */
export const RELIEF_FULL=3400, RELIEF_BASE=120;
export function elevationRampValue(height){
  if(!(height>0))return 0;
  const t=Math.log1p(height/RELIEF_BASE)/Math.log1p(RELIEF_FULL/RELIEF_BASE);
  return Math.max(1,Math.min(255,Math.round(t*255)));
}
/** 积雪：高于 2℃ 无雪，低于 -6℃ 全雪 */
export const snowValue=t=>Math.max(0,Math.min(255,Math.round(Math.max(0,Math.min(1,(2-t)/8))*255)));
export const depthValue=(level,height,full)=>Math.max(0,Math.min(255,Math.round(clamp01((level-height)/full)*255)));
export const DEPTH_SEA=1200,DEPTH_LAKE=60;

/** 图层辅助色：气候带、海水基色、温度带 */
export const CLIMATE_RGB=[[231,171,100],[170,199,108],[104,182,152],[124,174,196],[198,219,234]];
export const SEA_RGB=[52,96,120];
export const TEMPERATURE=ramp([[0,[52,92,168]],[.25,[92,152,196]],[.5,[186,206,170]],[.72,[226,190,120]],[1,[196,92,72]]]);
/** 温度-20~40℃映射到0..255 */
export function temperatureRGB(t){
  const v=Math.max(0,Math.min(255,Math.round((t+20)/60*255)))*3;
  return [TEMPERATURE[v],TEMPERATURE[v+1],TEMPERATURE[v+2]];
}

/** 昼夜图层的乘色：白=不变，夜里偏冷偏暗 */
export function daylightPixel(light){
  const rgb=[255,255,255];
  applyDaylight(rgb,light.day,light.dusk);
  return [Math.round(rgb[0]),Math.round(rgb[1]),Math.round(rgb[2])];
}

/** 色带底图：积雪按气温渐隐，不切断色带 */
export function flatColor(kind,rampValue,out,snow,depth){
  if(kind){
    const o=depth*3;
    out[0]=WATER[o];out[1]=WATER[o+1];out[2]=WATER[o+2];
    return out;
  }
  const o=rampValue*3,s=snow/255;
  if(s>0){
    out[0]=LAND[o]+(SNOW[765]-LAND[o])*s;
    out[1]=LAND[o+1]+(SNOW[766]-LAND[o+1])*s;
    out[2]=LAND[o+2]+(SNOW[767]-LAND[o+2])*s;
  }else{
    out[0]=LAND[o];out[1]=LAND[o+1];out[2]=LAND[o+2];
  }
  return out;
}

/** 太阳与本地方向(0正午,0.5子夜) */
export function sunState(hours,latitude,longitude,world){
  const w=world??{},num=(x,d)=>Number.isFinite(Number(x))?Number(x):d;
  const tilt=Math.min(60,Math.abs(num(w.axialTilt,23.44)))*DEG,prograde=w.prograde!==false;
  const sunLon=(prograde?-1:1)*((hours-12)/24)*TAU;
  const lat=Math.max(-89.9,Math.min(89.9,latitude))*DEG,lon=longitude*DEG;
  const sp=Math.sin(lat),cp=Math.cos(lat);
  const up=[cp*Math.cos(lon),sp,cp*Math.sin(lon)];
  const sun=[Math.cos(tilt)*Math.cos(sunLon),-Math.sin(tilt),Math.cos(tilt)*Math.sin(sunLon)];
  const height=up[0]*sun[0]+up[1]*sun[1]+up[2]*sun[2];
  const ix=up[0]-sun[0]*height,iy=up[1]-sun[1]*height,iz=up[2]-sun[2]*height;
  const scale=Math.sqrt(Math.max(1e-6,1-height*height))/(Math.hypot(ix,iy,iz)||1);
  const east=[-Math.sin(lon),0,Math.cos(lon)],north=[-sp*Math.cos(lon),cp,-sp*Math.sin(lon)];
  return {height,sunLon,tilt,
    east:(sun[0]*east[0]+sun[2]*east[2])*scale,
    north:(sun[0]*north[0]+sun[1]*north[1]+sun[2]*north[2])*scale};
}

/** 全球昼夜夹角(0极夜->1正午)
 * 逐行sin/cos纬度，逐列cos时角
 */
export function dayProfile(hours,world){
  const w=world??{},num=(x,d)=>Number.isFinite(Number(x))?Number(x):d;
  const tilt=Math.min(60,Math.abs(num(w.axialTilt,23.44)))*DEG,prograde=w.prograde!==false;
  return {sinDecl:Math.sin(-tilt),cosDecl:Math.cos(tilt),sunLon:(prograde?-1:1)*((hours-12)/24)*TAU};
}

/** 某点的日照系数与黄昏暖度；经纬度以弧度给出 */
export function dayFactor(profile,latitude,longitude){
  const lat=Math.max(-89.9*DEG,Math.min(89.9*DEG,latitude));
  const height=Math.sin(lat)*profile.sinDecl+Math.cos(lat)*profile.cosDecl*Math.cos(longitude-profile.sunLon);
  const day=clamp01((height+.12)/.3);
  const dusk=Math.max(0,1-Math.abs(height)/.24)*(1-day*.45);
  return {height,day,dusk};
}

/** 把昼夜与暖色乘到已合成的像素上，水面同样受光 */
export function applyDaylight(rgb,day,dusk){
  for(let k=0;k<3;k++){
    const tone=(LIGHT_NIGHT[k]+(LIGHT_DAY[k]-LIGHT_NIGHT[k])*day)/255;
    const warm=TINT[765+k]/255;
    rgb[k]=clamp01(rgb[k]*(.24+.76*day)*(tone+(warm-tone)*dusk)/255)*255;
  }
  return rgb;
}

/** 拟真光照：坡向与太阳点积 */
export function shadeRGB(rgb,sun,slopeEast,slopeNorth){
  if(!sun)return rgb;
  const dh=slopeEast||0,dn=slopeNorth||0;
  const dot=(-dh*sun.east-dn*sun.north+sun.height)/Math.sqrt(1+dh*dh+dn*dn);
  const day=clamp01((sun.height+.12)/.3),dusk=Math.max(0,1-Math.abs(sun.height)/.24)*(1-day*.45);
  // 背光到环境光下限，迎光到1
  const ambient=.12+.12*day;
  const light=Math.min(1,ambient+.80*clamp01(dot));
  for(let k=0;k<3;k++){
    const tone=(LIGHT_NIGHT[k]+(LIGHT_DAY[k]-LIGHT_NIGHT[k])*day)/255;
    const warm=TINT[765+k]/255;
    rgb[k]=clamp01(rgb[k]/255*light*(tone+(warm-tone)*dusk))*255;
  }
  return rgb;
}
