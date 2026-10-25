export const TAU=Math.PI*2, LIMIT=85.05112878*Math.PI/180, TILE=128;
export function atlasCamera(v,width,height){
  const size=v.generator.generation.planet.equatorChunks,cell=v.baseCellSize*v.scale;
  return {width,height,view:v.isGlobe?'globe':v.isMercator?'mercator':'projection',
    lon:(v.centerCx+(v.isGlobe?.5:-v.panX/cell))/size*TAU,
    lat:-(v.isGlobe?v.centerCz+.5:v.worldZ(v.mapZ(v.centerCz)-v.panY/cell))/size*TAU,
    scale:v.isGlobe?v.globeRadius:cell*size/TAU};
}
export const clamp=(v,a,b)=>Math.max(a,Math.min(b,v));
export const wrap=v=>((v+Math.PI)%TAU+TAU)%TAU-Math.PI;
export const merc=lat=>Math.log(Math.tan(Math.PI/4+clamp(lat,-LIMIT,LIMIT)/2));
export function project(view,lon,lat,clip=true){
  const {width:w,height:h,scale:s}=view,dl=wrap(lon-view.lon);
  if(view.view==='globe'){
    if(clip&&Math.sin(lat)*Math.sin(view.lat)+Math.cos(lat)*Math.cos(view.lat)*Math.cos(dl)<0)return null;
    return {x:w/2+s*Math.cos(lat)*Math.sin(dl),y:h/2-s*(Math.cos(view.lat)*Math.sin(lat)-Math.sin(view.lat)*Math.cos(lat)*Math.cos(dl))};
  }
  if(view.view==='mercator'&&Math.abs(lat)>LIMIT)return null;
  return {x:w/2+dl*s,y:h/2-(view.view==='mercator'?merc(lat)-merc(view.lat):lat-view.lat)*s};
}
export function unproject(view,x,y){
  const u=(x-view.width/2)/view.scale,v=(view.height/2-y)/view.scale;
  if(view.view==='globe'){
    if(u*u+v*v>1)return null;
    const d=Math.sqrt(Math.max(0,1-u*u-v*v));
    return {lat:Math.asin(clamp(v*Math.cos(view.lat)+d*Math.sin(view.lat),-1,1)),lon:wrap(view.lon+Math.atan2(u,d*Math.cos(view.lat)-v*Math.sin(view.lat)))};
  }
  const lat=view.view==='mercator'?2*Math.atan(Math.exp(v+merc(view.lat)))-Math.PI/2:v+view.lat;
  return Math.abs(lat)>(view.view==='mercator'?LIMIT:Math.PI/2)?null:{lat,lon:wrap(view.lon+u)};
}
export function tileKey(z,x,y){return `${z}/${x}/${y}`;}
function globeTileInViewport(view,lon,north,span){
  const south=north-span,dl=lon-view.lon,meridians=[dl,dl+span],latitudes=[south,north];
  for(let k=Math.ceil(dl/(Math.PI/2));k*(Math.PI/2)<dl+span;k++)meridians.push(k*Math.PI/2);
  if(south<0&&north>0)latitudes.push(0);
  let minX=Infinity,maxX=-Infinity,minY=Infinity,maxY=-Infinity;
  const sin=Math.sin(view.lat),cos=Math.cos(view.lat);
  for(const angle of meridians){
    const c=Math.cos(angle),a=cos,b=-sin*c,peak=Math.atan2(a,b),samples=[...latitudes];
    for(let k=-1;k<=1;k++){const lat=peak+k*Math.PI;if(lat>south&&lat<north)samples.push(lat);}
    for(const lat of samples){
      const x=Math.cos(lat)*Math.sin(angle),y=-(cos*Math.sin(lat)-sin*Math.cos(lat)*c);
      minX=Math.min(minX,x);maxX=Math.max(maxX,x);minY=Math.min(minY,y);maxY=Math.max(maxY,y);
    }
  }
  const margin=2;
  return view.width/2+maxX*view.scale>=-margin&&view.width/2+minX*view.scale<=view.width+margin
    &&view.height/2+maxY*view.scale>=-margin&&view.height/2+minY*view.scale<=view.height+margin;
}

export function visibleTiles(view,z,limit=Infinity){
  const n=2**z,span=TAU/n,points=[];
  for(let y=0;y<=12;y++)for(let x=0;x<=12;x++){
    const p=unproject(view,view.width*x/12,view.height*y/12);if(p)points.push(p);
  }
  if(view.view==='globe')for(let i=0;i<64;i++){
    const a=i*TAU/64,x=view.width/2+Math.cos(a)*view.scale*.999999,y=view.height/2+Math.sin(a)*view.scale*.999999;
    if(x>=0&&x<=view.width&&y>=0&&y<=view.height){const p=unproject(view,x,y);if(p)points.push(p);}
  }
  let poleVisible=false;
  if(view.view==='globe')for(const lat of [-Math.PI/2,Math.PI/2]){
    const p=project(view,view.lon,lat);
    if(p&&p.x>=0&&p.x<=view.width&&p.y>=0&&p.y<=view.height){poleVisible=true;points.push({lon:view.lon,lat});}
  }
  if(!points.length)return [];
  const lons=points.map(p=>view.lon+wrap(p.lon-view.lon)),lats=points.map(p=>p.lat);
  const x0=poleVisible?0:Math.floor((Math.min(...lons)+Math.PI)/span)-1,x1=poleVisible?n-1:Math.min(x0+n-1,Math.floor((Math.max(...lons)+Math.PI)/span)+1);
  const y0=clamp(Math.floor((Math.PI/2-Math.max(...lats))/span)-1,0,n/2-1),y1=clamp(Math.floor((Math.PI/2-Math.min(...lats))/span)+1,0,n/2-1),tiles=[];
  for(let y=y0;y<=y1;y++)for(let x=x0;x<=x1;x++){
    const tx=(x%n+n)%n;
    if(view.view==='globe'){
      const edge=Math.max(0,Math.abs(wrap((tx+.5)*span-Math.PI-view.lon))-span/2);
      const a=Math.sin(view.lat),b=Math.cos(view.lat)*Math.cos(edge);
      const north=Math.PI/2-y*span,south=north-span,peak=Math.atan2(a,b);
      const dot=lat=>a*Math.sin(lat)+b*Math.cos(lat);
      const visibility=Math.max(dot(north),dot(south),peak>=south&&peak<=north?dot(peak):-1);
      if(visibility<1e-10)continue;
      if(!globeTileInViewport(view,tx*span-Math.PI,north,span))continue;
    }
    const p=project(view,(tx+.5)*span-Math.PI,Math.PI/2-(y+.5)*span,false);
    tiles.push({z,x:tx,y,key:tileKey(z,tx,y),distance:p?Math.hypot(p.x-view.width/2,p.y-view.height/2):Infinity});
    if(tiles.length>limit)return tiles;
  }
  return tiles.sort((a,b)=>a.distance-b.distance);
}
export function levelFor(view){
  const stretch=view.view==='mercator'?1/Math.max(.04,Math.cos(view.lat)):1;
  return clamp(Math.ceil(Math.log2(TAU*view.scale*stretch/TILE)),2,22);
}
export function detailLevel(meters){return meters>2000?'全球':meters>128?'区域':meters>4?'地形':'区块';}
