import {TAU,clamp,wrap,unproject} from './tiles.mjs';

export function globeAnchor(point,x,y,radius){
  const u=x/radius,v=-y/radius,c=Math.cos(point.lat);
  if(u*u+v*v>1||Math.abs(u)>c)return null;
  const dl=Math.asin(clamp(u/c,-1,1)),a=Math.sin(point.lat),b=c*Math.cos(dl),r=Math.hypot(a,b);
  if(Math.abs(v)>r)return null;
  const lat=Math.atan2(a,b)-Math.asin(clamp(v/r,-1,1));
  return Math.abs(lat)>Math.PI/2?null:{lon:wrap(point.lon-dl),lat};
}
export function zoomGameAtlas(v,factor,clientX,clientY){
  if(!v.isPlanet||v.exploration||v.survivalPreview)return false;
  v.deferRefinement();
  const rect=v.canvas.getBoundingClientRect(),width=v.canvas.clientWidth,height=v.canvas.clientHeight;
  const x=clientX===undefined?width/2:clientX-rect.left,y=clientY===undefined?height/2:clientY-rect.top,size=v.generator.generation.planet.equatorChunks;
  v.labAtlasFocus={x,y};
  const cell=v.baseCellSize*v.scale,isGlobe=v.isGlobe;
  const state={width,height,view:isGlobe?'globe':v.isMercator?'mercator':'projection',lon:(v.centerCx+(isGlobe?.5:-v.panX/cell))/size*TAU,lat:-(isGlobe?v.centerCz+.5:v.worldZ(v.mapZ(v.centerCz)-v.panY/cell))/size*TAU,scale:isGlobe?v.globeRadius:cell*size/TAU};
  const point=unproject(state,x,y);if(!point)return true;
  const fitRadius=Math.min(width,height)*.43,newRadius=state.scale*factor;
  if(isGlobe){
    v.scale=clamp(v.scale*factor,v.overviewScale,8);
    if(v.scale>=v.overviewScale*5){
      const radius=v.globeRadius;v.planetView=v.preferredOverview==='mercator'?'mercator':'projection';
      v.scale=Math.min(8,radius*TAU/size/v.baseCellSize);placeFlat(v,point,x-width/2,y-height/2,size);
    }else{
      const center=globeAnchor(point,x-width/2,y-height/2,v.globeRadius);
      if(center){v.centerCx=center.lon/TAU*size-.5;v.centerCz=-center.lat/TAU*size-.5;}
    }
  }else if(v.preferredOverview==='globe'&&factor<1&&newRadius<fitRadius*2){
    v.planetView='globe';v.panX=v.panY=0;v.scale=v.overviewScale*Math.max(1,newRadius/fitRadius);
    const center=globeAnchor(point,x-width/2,y-height/2,v.globeRadius)??point;
    v.centerCx=center.lon/TAU*size-.5;v.centerCz=-center.lat/TAU*size-.5;
  }else{
    v.scale=clamp(v.scale*factor,v.projectionMinScale,8);placeFlat(v,point,x-width/2,y-height/2,size);
  }
  v.requestRender();return true;
}
function placeFlat(v,point,x,y,size){
  v.centerCx=point.lon/TAU*size;v.centerCz=-point.lat/TAU*size;
  v.panX=x;v.panY=y;
}
