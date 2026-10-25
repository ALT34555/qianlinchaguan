import {lakeTemplateCatalog,sampleTemplateLake,templateCell} from './lake-templates.mjs';
const caches=new WeakMap();
export function templateLakeOutlet(w,id){
  let c=caches.get(w);if(!c){c=new Map();caches.set(w,c);}if(c.has(id))return c.get(id);
  const lake=lakeTemplateCatalog(w).lakes.get(id),source=w.lakes[id];let result={status:'no-safe-bank',outlet:null};
  if(source.outlet>=0)result={status:'existing',outlet:null};
  else if(lake.salinity==='salt')result={status:'salt-shore',outlet:null};
  else{
    let edge=null;
    for(const cell of lake.cells)for(const [dx,dz] of [[-1,0],[1,0],[0,-1],[0,1]]){
      const col=cell%w.columns,row=Math.floor(cell/w.columns),n=templateCell(w,col+dx,row+dz);
      if(w.water[n]===1||w.water[n]===2)continue;
      if(!edge||w.height[n]<edge.height)edge={cell,col,row,dx,dz,height:w.height[n]};
    }
    if(edge){
      const span=w.circumference/w.columns,{col,row,dx,dz}=edge,C=w.circumference;
      const center={wx:(col+.5)*span-C/2,wz:(row+.5)*span-C/4};let a=center,gate=null;
      for(let k=1;k<=32;k++){
        const b={wx:center.wx+dx*span*k/16,wz:center.wz+dz*span*k/16},s=sampleTemplateLake(w,b.wx,b.wz);
        if(!s||s.field<=0){const sa=sampleTemplateLake(w,a.wx,a.wz),t=(sa?.field??0)/Math.max(1e-9,(sa?.field??0)-(s?.field??-1));gate={wx:a.wx+(b.wx-a.wx)*t,wz:a.wz+(b.wz-a.wz)*t};break;}a=b;
      }
      if(gate){
        const cos=Math.max(.05,Math.cos(gate.wz/C*Math.PI*2)),drop=.8,length=112,points=[];
        for(let k=0;k<=16;k++){const t=k/16;points.push({wx:gate.wx+dx*(t*length-12)/cos,wz:gate.wz+dz*(t*length-12),level:lake.level-drop*t*t*(3-2*t),halfWidth:3+1.5*t,depth:.55});}
        const end=points.at(-1),cell=templateCell(w,Math.floor((gate.wx/C+.5)*w.columns),Math.floor((gate.wz/C+.25)*w.columns));
        const outlet={id:'lake-outlet:'+id,cell,ownerCell:edge.cell,lakeId:id,salinity:lake.salinity,level:lake.level,drop,fall:false,cos,points,
          pond:{wx:end.wx,wz:end.wz,level:end.level,radiusX:20,radiusZ:14,depth:1.4},bounds:{x0:col*span-C/2,z0:row*span-C/4,span,cos}};
        result={status:'local',outlet};
      }
    }
  }
  c.set(id,result);return result;
}
