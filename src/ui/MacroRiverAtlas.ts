import type {MacroRiverOverview} from '../systems/world/WorldContract';
import {macroRiverSegment} from '../systems/world/planet-macro/river-geometry.mjs';
const indexes=new WeakMap<MacroRiverOverview,number[]>();
function riverCells(network:MacroRiverOverview):number[] {
  let cells=indexes.get(network);if(cells)return cells;
  cells=[];for(let i=0;i<network.receiver.length;i++)if(network.widths[i]>0&&network.receiver[i]>=0&&network.water[i]!==1&&network.water[i]!==2)cells.push(i);
  cells.sort((a,b)=>network.discharge[b]-network.discharge[a]||a-b);indexes.set(network,cells);return cells;
}
export function rasterMacroRivers(data:Uint8ClampedArray,width:number,height:number,network:MacroRiverOverview,highlight=false):number {
  const cells=riverCells(network),density=Math.max(.035,.16*96/width),count=Math.min(cells.length,Math.max(200,Math.ceil(width*height*density)));
  if(!count)return 0;
  const inner=new Float32Array(width*height),outer=new Float32Array(width*height),maximum=Math.max(1,network.discharge[cells[0]]);
  const stroke=(x0:number,y0:number,x1:number,y1:number,r:number)=>{
    const dx=x1-x0,dy=y1-y0,length=dx*dx+dy*dy;
    for(let y=Math.max(0,Math.floor(Math.min(y0,y1)-r-1));y<=Math.min(height-1,Math.ceil(Math.max(y0,y1)+r+1));y++)
      for(let x=Math.floor(Math.min(x0,x1)-r-1);x<=Math.ceil(Math.max(x0,x1)+r+1);x++){
        const t=length?Math.max(0,Math.min(1,((x-x0)*dx+(y-y0)*dy)/length)):0,d=Math.hypot(x-x0-t*dx,y-y0-t*dy),i=y*width+(x%width+width)%width;
        inner[i]=Math.max(inner[i],Math.min(1,Math.max(0,r+.5-d)));
        outer[i]=Math.max(outer[i],Math.min(1,Math.max(0,r+.9-d)));
      }
  };
  for(let k=0;k<count;k++){
    const i=cells[k],j=network.receiver[i],a=i%network.columns,b=j%network.columns,ay=Math.floor(i/network.columns),by=Math.floor(j/network.columns);
    const segment=macroRiverSegment(network.columns,network.rows,network.columns,i,j,a+.5-network.columns/2);
    const x0=(segment.a.x/network.columns+.5)*width-.5,y0=(segment.a.z/network.columns+.25)*height*2-.5;
    let x1=(segment.b.x/network.columns+.5)*width-.5;const y1=(segment.b.z/network.columns+.25)*height*2-.5;
    const radius=.35+.45*Math.sqrt(Math.max(0,network.discharge[i])/maximum);
    const longitudeGap=Math.abs(a-b),wrappedGap=Math.min(longitudeGap,network.columns-longitudeGap);
    if(wrappedGap>network.columns/4&&((ay===0&&by===0)||(ay===network.rows-1&&by===network.rows-1))){
      const edge=ay===0?-.5:height-.5;stroke(x0,y0,x0,edge,radius);stroke(x1,y1,x1,edge,radius);continue;
    }
    if(x1-x0>width/2)x1-=width;else if(x1-x0< -width/2)x1+=width;
    stroke(x0,y0,x1,y1,radius);
  }
  const color=highlight?[210,65,48]:[25,111,155],halo=[221,238,228];let pixels=0;
  for(let i=0;i<inner.length;i++){
    if(!outer[i])continue;pixels++;
    for(let c=0;c<3;c++){const at=i*4+c,base=data[at]*(1-outer[i]*.65)+halo[c]*outer[i]*.65;data[at]=base*(1-inner[i])+color[c]*inner[i];}
  }
  return pixels;
}
