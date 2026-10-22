import definitions from '../../../../content/data/world/blocks.json' with {type:'json'};

export const SURFACE_COVER_VERSION=1;
const clamp=v=>Math.max(0,Math.min(1,v)),mix=(a,b,t)=>a+(b-a)*t;
const smooth=(a,b,v)=>{const t=clamp((v-a)/(b-a));return t*t*(3-2*t);};
const blocks=new Map(definitions.filter(d=>d.id<100).map(d=>[d.key,{id:d.id,rgb:d.color?[1,3,5].map(i=>parseInt(d.color.slice(i,i+2),16)):[0,0,0]}]));
const palette=['stone','snow','sand','gravel','forest_soil','dirt','grass'].map(key=>blocks.get(key));
const caches=new WeakMap(),LIMIT=512,RASTER_LIMIT=128,STRIDE=14;
function hash(x,y,z,seed){let n=Math.imul(x,73856093)^Math.imul(y,19349663)^Math.imul(z,83492791)^seed;n=Math.imul(n^n>>>16,0x7feb352d);n=Math.imul(n^n>>>15,0x846ca68b);return ((n^n>>>16)>>>0)/2147483648-1;}
function cache(w){let c=caches.get(w);if(!c){c={items:new Map(),last:[],gradients:new Map(),raster:new Map(),rasterNodes:0,rasterHits:0,hits:0,misses:0,hashes:0};caches.set(w,c);}return c;}
export function clearCoverCache(w){caches.delete(w);}
export function coverCacheStats(w){const c=cache(w);return {entries:c.items.size,gradientEntries:c.gradients.size,limit:LIMIT,coefficientBytes:c.items.size*64,rasterTiles:c.raster.size,rasterLimit:RASTER_LIMIT,rasterBytes:c.raster.size*(81*STRIDE*8+81+128),rasterNodes:c.rasterNodes,rasterHits:c.rasterHits,hits:c.hits,misses:c.misses,hashes:c.hashes};}

// 插值节点坡度，避免格线跳变
export function coverGradient(w,context){
  const store=cache(w),{a,b,c,d,tx,ty}=context;
  let last=store.macro;
  if(!last||last.a!==a||last.b!==b||last.c!==c||last.d!==d){
    const node=i=>{
      if(store.gradients.has(i))return store.gradients.get(i);
      const x=i%w.columns,y=Math.floor(i/w.columns),span=w.circumference/w.columns;
      const at=(x,y)=>{if(y<0){y=-y-1;x+=w.columns/2;}if(y>=w.rows){y=w.rows*2-y-1;x+=w.columns/2;}return w.height[y*w.columns+(x+w.columns)%w.columns];};
      const cos=Math.max(.01,Math.cos(Math.PI/2-(y+.5)*Math.PI/w.rows)),gradient=[(at(x+1,y)-at(x-1,y))/(2*span*cos),(at(x,y+1)-at(x,y-1))/(2*span)];
      store.gradients.set(i,gradient);if(store.gradients.size>LIMIT)store.gradients.delete(store.gradients.keys().next().value);return gradient;
    };
    last=store.macro={a,b,c,d,values:[node(a),node(b),node(c),node(d)]};
  }
  const v=last.values,fade=context.pole>=0?context.pole:1;
  return {east:mix(mix(v[0][0],v[1][0],tx),mix(v[2][0],v[3][0],tx),ty)*fade,south:mix(mix(v[0][1],v[1][1],tx),mix(v[2][1],v[3][1],tx),ty)*fade};
}
export function coverSlope(w,context){const gradient=coverGradient(w,context);return Math.hypot(gradient.east,gradient.south);}

// 球面采样复用相邻噪声系数
function field(w,p,scale,layer){
  const x=p[0]*w.radius/scale,y=p[1]*w.radius/scale,z=p[2]*w.radius/scale,ix=Math.floor(x),iy=Math.floor(y),iz=Math.floor(z),c=cache(w);
  let tile=c.last[layer];
  if(tile&&tile.x===ix&&tile.y===iy&&tile.z===iz)c.hits++;
  else{
    const key=`${layer}:${ix}:${iy}:${iz}`;tile=c.items.get(key);
    if(tile){c.items.delete(key);c.items.set(key,tile);c.hits++;}
    else{
      const values=new Float64Array(8),seed=w.options.seed^(layer?0x67726f32:0x67726f31);
      for(let dz=0;dz<2;dz++)for(let dy=0;dy<2;dy++)for(let dx=0;dx<2;dx++)values[dz*4+dy*2+dx]=hash(ix+dx,iy+dy,iz+dz,seed);
      tile={x:ix,y:iy,z:iz,values};c.items.set(key,tile);c.misses++;c.hashes+=8;
      if(c.items.size>LIMIT){const oldest=c.items.keys().next().value,removed=c.items.get(oldest);c.items.delete(oldest);for(let k=0;k<c.last.length;k++)if(c.last[k]===removed)c.last[k]=null;}
    }
    c.last[layer]=tile;
  }
  const v=tile.values,tx=smooth(0,1,x-ix),ty=smooth(0,1,y-iy),tz=smooth(0,1,z-iz);
  return mix(mix(mix(v[0],v[1],tx),mix(v[2],v[3],tx),ty),mix(mix(v[4],v[5],tx),mix(v[6],v[7],tx),ty),tz);
}

function weights(temperature,aridity,exposure,out=[0,0,0,0,0,0]){
  const snow=1-smooth(-3,1,temperature),rock=(1-snow)*smooth(.48,.72,exposure),rest=1-snow-rock;
  const sand=rest*(1-smooth(.25,.55,aridity)),gravel=(rest-sand)*smooth(.25,.55,exposure)*(1-smooth(.65,1.1,aridity));
  out[0]=0;out[1]=rock;out[2]=rest-sand-gravel;out[3]=snow;out[4]=sand;out[5]=gravel;return out;
}
function dominant(values){let best=1;for(let i=2;i<values.length;i++)if(values[i]>values[best])best=i;return best;}
export function landMaterial(temperature,aridity,relief,slope,relative,exposure=.28){
  return dominant(landMaterialWeights(temperature,aridity,relief,slope,relative,exposure));
}
export function landMaterialWeights(temperature,aridity,relief,slope,relative,exposure=.28,out=[0,0,0,0,0,0]){return weights(temperature,aridity,exposure+slope*.7+clamp((relief-3)/20)*.18+clamp(relative/3)*.05,out);}

export function sampleCover(w,p,{temperature,rainfall,evaporation,waterAccess=0,uplift=0,slope=0}){
  const broad=field(w,p,512,0),patch=field(w,p,96,1),aridity=Math.max(rainfall/Math.max(150,evaporation),waterAccess*1.15)*(1+broad*.3+patch*.14);
  const exposure=.28+uplift*.25+broad*.4+patch*.25,values=weights(temperature+broad*1.5+patch,aridity,exposure+slope*.7);
  const soil=values[2],forest=smooth(.9,1.6,aridity)*smooth(-5,10,temperature),bare=1-smooth(.4,.95,aridity)+smooth(.2,.65,patch)*.3;
  const entries=[values[1],values[3],values[4],values[5],soil*forest,soil*(1-forest)*clamp(bare),soil*(1-forest)*(1-clamp(bare))];
  let best=-1,block=blocks.get('grass').id,r=0,g=0,b=0;
  for(let i=0;i<entries.length;i++){const def=palette[i],weight=entries[i];r+=def.rgb[0]*weight;g+=def.rgb[1]*weight;b+=def.rgb[2]*weight;if(weight>best){best=weight;block=def.id;}}
  return {surfaceBlock:block,surfaceColor:(Math.round(r)<<16)|(Math.round(g)<<8)|Math.round(b),groundMaterial:dominant(values),localAridity:aridity,exposure,coverWeights:entries};
}

// 覆盖节点按需生成，逐米仅插值
export function sampleCoverAt(w,wx,wz,sampler){
  const cx=Math.floor(wx/64),cz=Math.floor(wz/64),c=cache(w);
  let tile=c.lastRaster;
  if(!tile||tile.cx!==cx||tile.cz!==cz){
    const key=cx+','+cz;tile=c.raster.get(key);
    if(tile){c.raster.delete(key);c.raster.set(key,tile);}
    else{tile={cx,cz,values:new Float64Array(81*STRIDE),ready:new Uint8Array(81),blocks:new Uint8Array(64),materials:new Uint8Array(64)};c.raster.set(key,tile);if(c.raster.size>RASTER_LIMIT)c.raster.delete(c.raster.keys().next().value);}
    c.lastRaster=tile;
  }
  const x=(wx-cx*64)/8,z=(wz-cz*64)/8,ix=Math.floor(x),iz=Math.floor(z),tx=x-ix,tz=z-iz,v=tile.values;
  for(let dz=0;dz<2;dz++)for(let dx=0;dx<2;dx++){
    const index=(iz+dz)*9+ix+dx;
    if(tile.ready[index]){c.rasterHits++;continue;}
    const s=sampler(w,cx*64+(ix+dx)*8,cz*64+(iz+dz)*8),offset=index*STRIDE,color=s.surfaceColor;
    v.set(s.coverWeights,offset);v[offset+7]=(color>>>16)&255;v[offset+8]=(color>>>8)&255;v[offset+9]=color&255;v[offset+10]=s.localAridity;v[offset+11]=s.exposure;v[offset+12]=s.surfaceBlock;v[offset+13]=s.groundMaterial;tile.ready[index]=1;c.rasterNodes++;
  }
  const a=(iz*9+ix)*STRIDE,b=a+STRIDE,d=a+9*STRIDE,e=d+STRIDE;
  const read=k=>mix(mix(v[a+k],v[b+k],tx),mix(v[d+k],v[e+k],tx),tz);
  const cell=iz*8+ix;
  if(!tile.blocks[cell]){
    tile.blocks[cell]=v[a+12]===v[b+12]&&v[a+12]===v[d+12]&&v[a+12]===v[e+12]?v[a+12]:255;
    tile.materials[cell]=v[a+13]===v[b+13]&&v[a+13]===v[d+13]&&v[a+13]===v[e+13]?v[a+13]:255;
  }
  let best=-1,block=tile.blocks[cell],soil=0,material=tile.materials[cell],materialBest=-1;
  if(block===255||material===255){
    for(let k=0;k<7;k++){
      const weight=read(k);if(tile.blocks[cell]===255&&weight>best){best=weight;block=palette[k].id;}
      if(k>=4)soil+=weight;else if(weight>materialBest){materialBest=weight;if(tile.materials[cell]===255)material=[1,3,4,5][k];}
    }
    if(tile.materials[cell]===255&&soil>materialBest)material=2;
  }
  return {surfaceBlock:block,surfaceColor:(Math.round(read(7))<<16)|(Math.round(read(8))<<8)|Math.round(read(9)),groundMaterial:material,localAridity:read(10),exposure:read(11)};
}

export function finishCover(s){
  const wet=Number.isFinite(s.water)&&s.water>s.height;
  if(s.wetland>.08||wet){const def=blocks.get(s.wetland>.08?'peat':'mud');s.surfaceBlock=def.id;s.surfaceColor=(def.rgb[0]<<16)|(def.rgb[1]<<8)|def.rgb[2];}
  return s;
}
export function coverRGB(s){const value=s.surfaceColor;return [(value>>>16)&255,(value>>>8)&255,value&255];}
