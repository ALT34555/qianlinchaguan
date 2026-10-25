import {islandRaster,ISLAND_RASTER_SIZE,ISLAND_RASTER_PEAKS} from './island-template-raster.mjs';
import {islandReliefPeak} from './coast-scale.mjs';
import {mainRiverField} from './river-continuity.mjs';

const catalogs=new WeakMap(),smooth=v=>{const t=Math.max(0,Math.min(1,v));return t*t*(3-2*t);};
function hash(v){v=Math.imul(v^(v>>>16),0x7feb352d);v=Math.imul(v^(v>>>15),0x846ca68b);return (v^(v>>>16))>>>0;}
function cellAt(w,x,y){return y<0||y>=w.rows?-1:y*w.columns+((x%w.columns)+w.columns)%w.columns;}

export function islandCoastCatalog(w){
  let c=catalogs.get(w);if(c)return c;
  c={islands:[],cells:new Map()};catalogs.set(w,c);
  const span=w.circumference/w.columns,riverWidths=mainRiverField(w).widths;
  for(const island of w.macroIslands??[]){
    const col=island.cell%w.columns,row=Math.floor(island.cell/w.columns);
    const cos=Math.cos(((row+.5)/w.rows-.5)*Math.PI);
    if(row<1||row>=w.rows-1||cos<.2||w.height[island.cell]<=0||w.water[island.cell]!==0)continue;
    let safe=true;
    for(let dz=-1;dz<=1;dz++)for(let dx=-1;dx<=1;dx++){
      const i=cellAt(w,col+dx,row+dz);
      if(i<0||w.water[i]===2||riverWidths[i]>0||((dx||dz)&&(w.height[i]>=0||w.water[i]!==1)))safe=false;
    }
    if(!safe)continue;
    const seed=hash(w.options.seed^Math.imul(island.cell+1,374761393));
    const angle=hash(seed^0x726f7461)/4294967296*Math.PI*2;
    const item={...island,variant:seed%20,wx:(col+.5)*span-w.circumference/2,wz:(row+.5)*span-w.circumference/4,
      cos,angle,sin:Math.sin(angle),rotCos:Math.cos(angle),span,peak:islandReliefPeak(w,island),
      delta:w.height[island.cell]-island.baseHeight};
    c.islands.push(item);
    for(let dz=-1;dz<=1;dz++)for(let dx=-1;dx<=1;dx++){
      const cell=cellAt(w,col+dx,row+dz);if(!c.cells.has(cell))c.cells.set(cell,[]);c.cells.get(cell).push(item);
    }
  }
  return c;
}

export function islandTemplateHeight(variant,u,v){
  if(u<0||v<0||u>1||v>1)return null;
  const size=ISLAND_RASTER_SIZE,last=size-1,x=u*last,z=v*last;
  const ix=Math.min(last-1,Math.floor(x)),iz=Math.min(last-1,Math.floor(z)),tx=x-ix,tz=z-iz;
  const grid=islandRaster(variant),i=iz*size+ix;
  return (grid[i]*(1-tx)+grid[i+1]*tx)*(1-tz)+(grid[i+size]*(1-tx)+grid[i+size+1]*tx)*tz;
}

export function sampleIslandCoast(w,wx,wz,natural){
  if(!w.macroIslands?.length)return null;
  const C=w.circumference,W=w.columns,span=C/W;
  const x=Math.floor((wx/C+.5)*W),y=Math.floor((wz/C+.25)*W);
  const entries=islandCoastCatalog(w).cells.get(cellAt(w,x,y));if(!entries)return null;
  for(const island of entries){
    const dx=((wx-island.wx+C/2)%C+C)%C-C/2,dz=wz-island.wz;
    if(Math.abs(dx)>=span||Math.abs(dz)>=span)continue;
    const weight=(1-Math.abs(dx)/span)*(1-Math.abs(dz)/span);
    const restored=natural-island.delta*weight,physicalSpan=span*.92;
    const u=.5+(dx*island.rotCos+dz*island.sin)/physicalSpan;
    const v=.5+(-dx*island.sin+dz*island.rotCos)/physicalSpan;
    const h=islandTemplateHeight(island.variant,u,v);
    let height=restored;
    if(h!==null){
      const scale=island.peak/ISLAND_RASTER_PEAKS[island.variant];
      const target=Math.max(-3000,h*scale);
      const blend=h>=0?1:1-smooth((-h-12)/60);
      height=restored+(target-restored)*blend;
    }
    return {height,kind:height<0?1:0,water:height<0?0:-Infinity,islandCell:island.cell,variant:island.variant};
  }
  return null;
}
