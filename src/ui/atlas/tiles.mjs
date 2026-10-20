export const TAU=Math.PI*2, LIMIT=85.05112878*Math.PI/180, TILE=128;
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
export function visibleTiles(view,z){
  const n=2**z,span=TAU/n,points=[];
  for(let y=0;y<=12;y++)for(let x=0;x<=12;x++){
    const p=unproject(view,view.width*x/12,view.height*y/12);if(p)points.push(p);
  }
  if(view.view==='globe')for(let i=0;i<64;i++){
    const a=i*TAU/64,x=view.width/2+Math.cos(a)*view.scale*.999999,y=view.height/2+Math.sin(a)*view.scale*.999999;
    if(x>=0&&x<=view.width&&y>=0&&y<=view.height){const p=unproject(view,x,y);if(p)points.push(p);}
  }
  if(view.view==='globe')for(const lat of [-Math.PI/2,Math.PI/2]){
    const p=project(view,view.lon,lat);
    if(p&&p.x>=0&&p.x<=view.width&&p.y>=0&&p.y<=view.height)points.push({lon:view.lon,lat});
  }
  if(!points.length)return [];
  const lons=points.map(p=>view.lon+wrap(p.lon-view.lon)),lats=points.map(p=>p.lat);
  const x0=Math.floor((Math.min(...lons)+Math.PI)/span),x1=Math.min(x0+n-1,Math.floor((Math.max(...lons)+Math.PI)/span));
  const y0=clamp(Math.floor((Math.PI/2-Math.max(...lats))/span),0,n/2-1),y1=clamp(Math.floor((Math.PI/2-Math.min(...lats))/span),0,n/2-1),tiles=[];
  for(let y=y0;y<=y1;y++)for(let x=x0;x<=x1;x++){
    const tx=(x%n+n)%n,p=project(view,(tx+.5)*span-Math.PI,Math.PI/2-(y+.5)*span,false);
    tiles.push({z,x:tx,y,key:tileKey(z,tx,y),distance:p?Math.hypot(p.x-view.width/2,p.y-view.height/2):Infinity});
  }
  return tiles.sort((a,b)=>a.distance-b.distance);
}
export function levelFor(view){
  const stretch=view.view==='mercator'?1/Math.max(.04,Math.cos(view.lat)):1;
  return clamp(Math.ceil(Math.log2(TAU*view.scale*stretch/TILE)),2,22);
}
export function detailLevel(meters){return meters>2000?'全球':meters>128?'区域':meters>4?'地形':'区块';}
