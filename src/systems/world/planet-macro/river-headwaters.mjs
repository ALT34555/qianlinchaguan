import {mainRiverWidth} from './river-continuity.mjs';
const outlets=new WeakMap();
export function lakeOutletSource(w,cell){
  let map=outlets.get(w);if(!map){map=new Map();for(const lake of w.lakes??[])if(lake.outlet>=0)map.set(lake.outlet,lake);outlets.set(w,map);}
  return map.get(cell)??null;
}
// 湖泊出流不视为新河源
export function isRiverHead(w,cell){
  if(!mainRiverWidth(w,cell)||w.receiver[cell]<0)return false;
  if(lakeOutletSource(w,cell))return false;
  const col=cell%w.columns,row=Math.floor(cell/w.columns);
  for(let dz=-1;dz<=1;dz++)for(let dx=-1;dx<=1;dx++){
    let x=col+dx,y=row+dz;
    if(y<0){y=-y-1;x+=w.columns/2;}
    if(y>=w.rows){y=w.rows*2-y-1;x+=w.columns/2;}
    const i=y*w.columns+(x%w.columns+w.columns)%w.columns;
    if(i!==cell&&mainRiverWidth(w,i)&&w.receiver[i]===cell)return false;
  }
  return true;
}
