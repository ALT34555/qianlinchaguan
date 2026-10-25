import {CloudNoise,clamp,smooth,DAY_MS} from './model.mjs';
export class RainField{
  constructor(world){this.world=world;this.noise=new CloudNoise(world.options.seed^0x636c6f75);this.version=0;}
  adopt(snapshot){
    this.snapshot=snapshot;const w=this.world;
    this.levels=[{width:w.columns,height:w.rows,rain:snapshot.rain,water:snapshot.water,temperature:w.temperature}];
    while(this.levels.at(-1).width>4){
      const a=this.levels.at(-1),width=a.width/2,height=Math.max(1,a.height/2),n=width*height,b={width,height,rain:new Float32Array(n),water:new Float32Array(n),temperature:new Float32Array(n)};
      for(let y=0;y<height;y++){const wa=Math.cos((.5-(y*2+.5)/a.height)*Math.PI),wb=Math.cos((.5-(Math.min(a.height-1,y*2+1)+.5)/a.height)*Math.PI);
      for(let x=0;x<width;x++)for(const field of ['rain','water','temperature']){
        const i=y*width+x,j=y*2*a.width+x*2,k=Math.min(a.height-1,y*2+1)*a.width+x*2;b[field][i]=((a[field][j]+a[field][j+1])*wa+(a[field][k]+a[field][k+1])*wb)/(2*(wa+wb));
      }
      }
      this.levels.push(b);
    }
    this.version++;
  }
  read(lon,lat,level=0){
    const a=this.levels[Math.min(this.levels.length-1,Math.max(0,level))],x=(lon/(Math.PI*2)+.5)*a.width-.5,y=(.5-lat/Math.PI)*a.height-.5;
    const ix=Math.floor(x),iy=Math.floor(y),u=x-ix,v=y-iy,index=(cx,cy)=>{cy=Math.max(0,Math.min(a.height-1,cy));return cy*a.width+((cx%a.width)+a.width)%a.width;};
    const A=index(ix,iy),B=index(ix+1,iy),C=index(ix,iy+1),D=index(ix+1,iy+1),sample=f=>(a[f][A]*(1-u)+a[f][B]*u)*(1-v)+(a[f][C]*(1-u)+a[f][D]*u)*v;
    return {rain:sample('rain'),water:sample('water'),temperature:sample('temperature')};
  }
  sample(lon,lat,ms,footprint=0,temperature){
    if(!this.snapshot)return {intensity:0,cloudCover:0,precipitation:'none',id:1,rainMm:0,cloudWaterMm:0,temperature:temperature??15};
    const w=this.world,span=w.circumference/w.columns,lod=Math.max(0,Math.log2(Math.max(1,footprint/span))),low=Math.floor(lod),blend=lod-low,a=this.read(lon,lat,low),b=blend?this.read(lon,lat,low+1):a;
    const rain=a.rain*(1-blend)+b.rain*blend,water=a.water*(1-blend)+b.water*blend,temp=temperature??(a.temperature*(1-blend)+b.temperature*blend);
    const local=this.noise.cloud(lon/(Math.PI*2)*w.circumference,-lat/(Math.PI*2)*w.circumference,ms/DAY_MS,w.circumference,w.columns);
    const average=smooth(span/6,span*1.5,footprint),mask=local*(1-average)+.43*average;
    const intensity=clamp(rain/12)*mask,cloudCover=clamp(water/12*.55+mask*.45+(rain>0?.2:0));
    const precipitation=intensity<.025?'none':temp>2?'rain':temp>0?'mixed':'snow';
    const id=precipitation==='none'?(cloudCover>.65?3:cloudCover>.4?2:1):precipitation==='mixed'?200:precipitation==='snow'?(intensity<.55?201:202):intensity<.25?100:intensity<.6?101:102;
    return {intensity:precipitation==='none'?0:intensity,cloudCover,precipitation,id,rainMm:rain,cloudWaterMm:water,temperature:temp};
  }
}
