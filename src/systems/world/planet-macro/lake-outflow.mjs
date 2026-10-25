import {lakeOutletSource} from './river-headwaters.mjs';
import {sampleTemplateLake,lakeTemplateCatalog} from './lake-templates.mjs';
export function lakeOutflowGeometry(w,cell,first){return buildLakeOutflow(w,cell,first,null)?.points.slice(0,-1)??[];}
export function buildLakeOutflow(w,cell,first,_sample,downstreamLevel=first.level){
  const lake=lakeOutletSource(w,cell);if(!lake)return null;
  const C=w.circumference,span=C/w.columns,col=lake.exitCell%w.columns,row=Math.floor(lake.exitCell/w.columns);
  const start={wx:(col+.5)*span-C/2,wz:(row+.5)*span-C/4},dx=first.wx-start.wx,dz=first.wz-start.wz;
  const x=dx-Math.round(dx/C)*C,gate=Math.abs(x)>=Math.abs(dz)?{wx:start.wx+Math.sign(x)*span/2,wz:start.wz}:{wx:start.wx,wz:start.wz+Math.sign(dz)*span/2};
  const level=lakeTemplateCatalog(w).lakes.get(lake.id).level,positions=[];let lastLake=0;
  for(let k=0;k<=16;k++){
    const t=k/16,a=t<=.5?start:gate,b=t<=.5?gate:first,u=t<=.5?t*2:(t-.5)*2;
    const p={wx:a.wx+(b.wx-a.wx)*u,wz:a.wz+(b.wz-a.wz)*u};
    if(sampleTemplateLake(w,p.wx,p.wz)?.kind===2)lastLake=k;positions.push(p);
  }
  const endLevel=lastLake===16?level:Math.min(level,first.level,downstreamLevel);
  const points=positions.map((p,k)=>({...p,level:k<=lastLake?level:level+(endLevel-level)*(k-lastLake)/Math.max(1,16-lastLake),halfWidth:first.halfWidth,depth:first.depth??2}));
  return {points,level,endLevel,lakeId:lake.id,visited:0,template:true,gate};
}
