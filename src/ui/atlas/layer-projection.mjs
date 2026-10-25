import {TAU,LIMIT,wrap,merc,clamp,levelFor} from './tiles.mjs';

/** 反算屏幕坐标，每个视口复用 */
export function layerScreenGrid(state){
  const factor=Math.min(1,Math.sqrt((state.rasterBudget??600000)/(state.width*state.height)));
  const width=Math.max(1,Math.ceil(state.width*factor)),height=Math.max(1,Math.ceil(state.height*factor));
  const PointArray=levelFor(state)<10?Float32Array:Float64Array;
  const points=new PointArray(width*height*2);points.fill(-1);
  if(state.view!=='globe'){
    const xTable=new PointArray(width),mercLat=state.view==='mercator'?merc(state.lat):0,limit=state.view==='mercator'?LIMIT:Math.PI/2;
    for(let x=0;x<width;x++){
      const u=((x+.5)*state.width/width-state.width/2)/state.scale;
      let px=Math.min(1-Number.EPSILON,wrap(state.lon+u)/TAU+.5);
      if(px>=1)px=1-2**-24;
      xTable[x]=px;
    }
    for(let y=0;y<height;y++){
      const v=(state.height/2-(y+.5)*state.height/height)/state.scale;
      const lat=state.view==='mercator'?2*Math.atan(Math.exp(v+mercLat))-Math.PI/2:v+state.lat;
      if(Math.abs(lat)>limit)continue;
      let py=Math.min(.5-Number.EPSILON,.25-lat/TAU);
      if(py>=.5)py=.5-2**-25;
      const base=y*width*2;
      for(let x=0;x<width;x++){const i=base+x*2;points[i]=xTable[x];points[i+1]=py;}
    }
    return {width,height,points};
  }
  const cosLat=Math.cos(state.lat),sinLat=Math.sin(state.lat),uTable=new Float64Array(width);
  for(let x=0;x<width;x++)uTable[x]=((x+.5)*state.width/width-state.width/2)/state.scale;
  for(let y=0;y<height;y++){
    const v=(state.height/2-(y+.5)*state.height/height)/state.scale,v2=v*v,base=y*width*2;
    if(v2>1)continue;
    for(let x=0;x<width;x++){
      const u=uTable[x],r2=u*u+v2;if(r2>1)continue;
      const d=Math.sqrt(Math.max(0,1-r2));
      const lat=Math.asin(clamp(v*cosLat+d*sinLat,-1,1)),lon=wrap(state.lon+Math.atan2(u,d*cosLat-v*sinLat));
      const i=base+x*2;points[i]=Math.min(1-Number.EPSILON,lon/TAU+.5);points[i+1]=Math.min(.5-Number.EPSILON,.25-lat/TAU);
      if(points[i]>=1)points[i]=1-2**-24;
      if(points[i+1]>=.5)points[i+1]=.5-2**-25;
    }
  }
  return {width,height,points};
}

/** 每个层级共用像素到瓦片的索引 */
function tilePixelIndices(grid,z){
  grid.tileIndices??=new Map();
  if(grid.tileIndices.has(z))return grid.tileIndices.get(z);
  const n=2**z,counts=new Map(),points=grid.points,count=grid.width*grid.height;
  const keyAt=i=>Math.min(n/2-1,Math.floor(points[i*2+1]*n))*n+Math.floor(points[i*2]*n);
  if(n<=256){
    const sizes=new Uint32Array(n*n/2),offsets=new Uint32Array(sizes.length);
    let valid=0;
    for(let i=0;i<count;i++)if(points[i*2]>=0){sizes[keyAt(i)]++;valid++;}
    const indices=new Uint32Array(valid),result=new Map();
    let start=0;
    for(let key=0;key<sizes.length;key++)if(sizes[key]){
      offsets[key]=start;result.set(key,indices.subarray(start,start+sizes[key]));start+=sizes[key];
    }
    for(let i=0;i<count;i++)if(points[i*2]>=0){const key=keyAt(i);indices[offsets[key]++]=i;}
    grid.tileIndices.set(z,result);
    return result;
  }
  for(let i=0;i<count;i++)if(points[i*2]>=0){const key=keyAt(i);counts.set(key,(counts.get(key)??0)+1);}
  const result=new Map(),offsets=new Map();
  for(const [key,size] of counts){result.set(key,new Uint32Array(size));offsets.set(key,0);}
  for(let i=0;i<count;i++)if(points[i*2]>=0){const key=keyAt(i),at=offsets.get(key);result.get(key)[at]=i;offsets.set(key,at+1);}
  grid.tileIndices.set(z,result);
  return result;
}

/** 仅重投影新增或替换瓦片的屏幕像素 */
export function projectLayerPixels(tiles,grid,cache={}){
  const sources=new Map(tiles.map(tile=>[tile.key??`${tile.z}/${tile.x}/${tile.y}`,tile]));
  const count=grid.width*grid.height;
  let reset=cache.grid!==grid||!cache.pixels;
  if(!reset)for(const key of cache.sources.keys())if(!sources.has(key)){reset=true;break;}
  if(reset){
    cache.pixels=new Uint8ClampedArray(count*4);
    cache.levels=new Int8Array(count);cache.levels.fill(-1);
    cache.sources=new Map();cache.grid=grid;
  }
  const pixels=cache.pixels,points=grid.points,levels=cache.levels;
  for(const [key,tile] of [...sources].sort((a,b)=>a[1].z-b[1].z)){
    if(cache.sources.get(key)===(tile.revision??tile))continue;
    const n=2**tile.z,indices=tilePixelIndices(grid,tile.z).get(tile.y*n+tile.x);
    if(!indices)continue;
    const side=tile.side??tile.canvas.width-2;
    const data=tile.pixels??tile.canvas.getContext('2d').getImageData(1,1,side,side).data;
    for(const i of indices){
      if(levels[i]>tile.z)continue;
      const tx=points[i*2]*n,ty=points[i*2+1]*n;
      if(tile.nearest){
        const x=clamp(Math.floor((tx-tile.x)*side),0,side-1),y=clamp(Math.floor((ty-tile.y)*side),0,side-1);
        pixels.set(data.subarray((y*side+x)*4,(y*side+x)*4+4),i*4);levels[i]=tile.z;continue;
      }
      const px=clamp((tx-tile.x)*side-.5,0,side-1),py=clamp((ty-tile.y)*side-.5,0,side-1);
      const x0=Math.floor(px),y0=Math.floor(py),x1=Math.min(side-1,x0+1),y1=Math.min(side-1,y0+1);
      const fx=px-x0,fy=py-y0,a=(y0*side+x0)*4,b=(y0*side+x1)*4,c=(y1*side+x0)*4,d=(y1*side+x1)*4;
      const wa=(1-fx)*(1-fy)*data[a+3],wb=fx*(1-fy)*data[b+3],wc=(1-fx)*fy*data[c+3],wd=fx*fy*data[d+3];
      const alpha=wa+wb+wc+wd,o=i*4;pixels[o+3]=alpha;levels[i]=tile.z;
      for(let k=0;k<3;k++)pixels[o+k]=alpha?(wa*data[a+k]+wb*data[b+k]+wc*data[c+k]+wd*data[d+k])/alpha:0;
    }
  }
  cache.sources=new Map([...sources].map(([key,tile])=>[key,tile.revision??tile]));
  return pixels;
}
