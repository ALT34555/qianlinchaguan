import {TAU,LIMIT,clamp,project} from './tiles.mjs';

export function exploredPolygons(state,exploration,size){
  const polygons=[],step=TAU/size;
  for(const [cx,cz] of exploration.coordinates()){
    const lon=(cx+.5)*step,lat=-(cz+.5)*step,center=project(state,lon,lat);
    if(!center)continue;
    let points;
    if(state.view==='globe'){
      points=[[cx,-cz],[cx+1,-cz],[cx+1,-cz-1],[cx,-cz-1]].map(([x,y])=>project(state,x*step,y*step));
      if(points.some(p=>!p))continue;
    }else{
      const limit=state.view==='mercator'?LIMIT:Math.PI/2;
      const north=project(state,lon,clamp(-cz*step,-limit,limit));
      const south=project(state,lon,clamp(-(cz+1)*step,-limit,limit));
      const left=center.x-step*state.scale/2,right=left+step*state.scale;
      points=[{x:left,y:north.y},{x:right,y:north.y},{x:right,y:south.y},{x:left,y:south.y}];
    }
    if(points.every(p=>p.x<0)||points.every(p=>p.x>state.width)||points.every(p=>p.y<0)||points.every(p=>p.y>state.height))continue;
    polygons.push(points);
  }
  return polygons;
}

export function clipExplored(ctx,state,exploration,size){
  ctx.beginPath();
  for(const points of exploredPolygons(state,exploration,size)){
    ctx.moveTo(points[0].x,points[0].y);
    for(const p of points.slice(1))ctx.lineTo(p.x,p.y);
    ctx.closePath();
  }
  ctx.clip();
}
