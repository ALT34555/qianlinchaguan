export const DAY_MS=86400000,THRESHOLD=12;
export const clamp=(v,a=0,b=1)=>Math.max(a,Math.min(b,v));
export const smooth=(a,b,x)=>{const t=clamp((x-a)/(b-a));return t*t*(3-2*t);};
export const gameDay=(ms,offset=540)=>Math.floor(ms/DAY_MS+offset/1440);
export function hash(seed,x,y){let n=Math.imul(x,73856093)^Math.imul(y,19349663)^seed;n=Math.imul(n^n>>>16,0x7feb352d);n=Math.imul(n^n>>>15,0x846ca68b);return ((n^n>>>16)>>>0)/4294967296;}
export class CloudNoise{
  constructor(seed){this.texture=new Float32Array(128*128);for(let y=0;y<128;y++)for(let x=0;x<128;x++)this.texture[y*128+x]=hash(seed,x,y);}
  at(x,y,period=128){
    const ix=Math.floor(x),iy=Math.floor(y),fx=x-ix,fy=y-iy,u=fx*fx*(3-2*fx),v=fy*fy*(3-2*fy),t=this.texture;
    const mask=period-1,a=(iy&mask)*128+(ix&mask),b=(iy&mask)*128+((ix+1)&mask),c=((iy+1)&mask)*128+(ix&mask),d=((iy+1)&mask)*128+((ix+1)&mask);
    return (t[a]*(1-u)+t[b]*u)*(1-v)+(t[c]*(1-u)+t[d]*u)*v;
  }
  cloud(wx,wz,days,circumference,columns){
    const f=Math.max(1,Math.round(columns*3/128))*128/circumference;
    const u=wx*f-days*1.7,v=wz*f-days*.55;
    const dx=(this.at(u*.5+41,v*.5+17)-.5)*1.6,dz=(this.at(u*.5+73,v*.5+59)-.5)*1.6;
    const broad=this.at(u+dx,v+dz),fine=this.at(wx*f*2+dx+days*.8+37,wz*f*2+dz-days*.7+71);
    const pole=clamp(Math.cos(wz/circumference*Math.PI*2)*4);
    return smooth(.40,.65,.5+((broad*.75+fine*.25)-.5)*pole);
  }
}
export class DailyRainModel{
  constructor(config,day,saved){
    this.config=config;this.noise=new CloudNoise(config.seed^0x7261696e);this.day=day-1;
    const n=config.columns*config.rows;
    this.water=new Float32Array(n);this.rain=new Float32Array(n);this.phase=new Uint8Array(n);
    this.updates=0;
    if(saved){
      validateSnapshot(config,saved);this.day=saved.day;this.water.set(saved.water);this.rain.set(saved.rain);this.phase.set(saved.phase);
    }else for(let y=0;y<config.rows;y++)for(let x=0;x<config.columns;x++){
      const i=y*config.columns+x;if(config.rainfall[i]>0)this.water[i]=THRESHOLD*this.noise.at((x+.5)/8+day*.13+41,(y+.5)/8+17,Math.min(128,config.columns/8));
    }
  }
  stepRows(day,start,end){
    const c=this.config,period=Math.min(128,c.columns/8),frequency=period/c.columns;
    for(let y=start;y<end;y++)for(let x=0;x<c.columns;x++){
      const i=y*c.columns+x,a=Math.max(0,c.rainfall[i])/365;
      const wet=this.noise.at((x+.5)*frequency-day*.7,(y+.5)*frequency-day*.23,period)>.5,input=wet?a*2:0;
      let water=this.water[i]+input,phase=this.phase[i];if(water>=THRESHOLD)phase=1;
      const rain=phase?Math.min(water,Math.max(4,a*4)):0;water=Math.max(0,water-rain);
      this.water[i]=water;this.rain[i]=rain;this.phase[i]=water>1e-6?phase:0;
    }
  }
  step(day){if(day!==this.day+1)throw Error('降水日序必须连续');this.stepRows(day,0,this.config.rows);this.day=day;this.updates++;}
  snapshot(){return {version:1,fingerprint:this.config.fingerprint,columns:this.config.columns,rows:this.config.rows,seed:this.config.seed,day:this.day,water:this.water.slice(),rain:this.rain.slice(),phase:this.phase.slice()};}
}
export function validateSnapshot(c,s){
  const n=c.columns*c.rows;
  if(s.version!==1||s.fingerprint!==c.fingerprint||s.columns!==c.columns||s.rows!==c.rows||s.seed!==c.seed||!Number.isSafeInteger(s.day))throw Error('降水存档与世界不匹配');
  if(!(s.water instanceof Float32Array)||!(s.rain instanceof Float32Array)||!(s.phase instanceof Uint8Array)||s.water.length!==n||s.rain.length!==n||s.phase.length!==n)throw Error('降水存档长度错误');
  for(let i=0;i<n;i++)if(!Number.isFinite(s.water[i])||s.water[i]<0||s.water[i]>THRESHOLD+Math.max(0,c.rainfall[i])/365*2+.01||!Number.isFinite(s.rain[i])||s.rain[i]<0||s.rain[i]>Math.max(4,Math.max(0,c.rainfall[i])/365*4)+.01||s.phase[i]>1)throw Error('降水存档数据错误');
}
export function encodeRain(s){
  const {water,rain,phase,...meta}=s,json=new TextEncoder().encode(JSON.stringify(meta)),start=Math.ceil((8+json.length)/4)*4,n=water.length,bytes=new Uint8Array(start+n*9);
  bytes.set([80,82,65,49]);new DataView(bytes.buffer).setUint32(4,json.length,true);bytes.set(json,8);
  bytes.set(new Uint8Array(water.buffer,water.byteOffset,water.byteLength),start);bytes.set(new Uint8Array(rain.buffer,rain.byteOffset,rain.byteLength),start+n*4);bytes.set(phase,start+n*8);return bytes;
}
export function decodeRain(input){
  const bytes=input instanceof Uint8Array?input:new Uint8Array(input),v=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);
  if(bytes.length<8||bytes[0]!==80||bytes[1]!==82||bytes[2]!==65||bytes[3]!==49)throw Error('未知降水存档');
  const length=v.getUint32(4,true),start=Math.ceil((8+length)/4)*4;if(length>4096||start>bytes.length)throw Error('降水存档头错误');
  const meta=JSON.parse(new TextDecoder().decode(bytes.subarray(8,8+length))),n=meta.columns*meta.rows;
  if(!Number.isSafeInteger(n)||n<1||n>524288||bytes.length!==start+n*9)throw Error('降水存档大小错误');
  const copy=bytes.slice().buffer;return {...meta,water:new Float32Array(copy,start,n),rain:new Float32Array(copy,start+n*4,n),phase:new Uint8Array(copy,start+n*8,n)};
}
