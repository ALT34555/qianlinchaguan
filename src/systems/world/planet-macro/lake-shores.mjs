import {lakeTemplateCatalog,sampleTemplateLake,lakeTemplateTile,templateCell} from './lake-templates.mjs';
import {templateLakeOutlet} from './lake-template-outlets.mjs';
import {applyLakeOutlet} from './lake-landscapes.mjs';
const smooth=v=>{const t=Math.max(0,Math.min(1,v));return t*t*(3-2*t);};
const index=templateCell;
export function shoreContext(w,wx,wz){
  const x=(wx/w.circumference+.5)*w.columns-.5,y=(wz/w.circumference+.25)*w.columns-.5;
  const ix=Math.floor(x),iy=Math.floor(y),span=w.circumference/w.columns;
  const distance=Math.min(Math.abs(wz+w.circumference/4),Math.abs(wz-w.circumference/4));
  return {a:index(w,ix,iy),b:index(w,ix+1,iy),c:index(w,ix,iy+1),d:index(w,ix+1,iy+1),tx:x-ix,ty:y-iy,
    pole:distance<span?smooth(distance/span):-1,poleId:wz<0?0:1};
}

export function sampleLakeShore(w,wx,wz,naturalHeight,base=null,context=null,sampler=null,details=true,landscapes=details){
  const s=sampleTemplateLake(w,wx,wz,naturalHeight);
  if(!s){
    if(!landscapes)return null;
    const cell=templateCell(w,Math.floor((wx/w.circumference+.5)*w.columns),Math.floor((wz/w.circumference+.25)*w.columns));
    for(const f of lakeTemplateTile(w,cell)){
      const outlet=templateLakeOutlet(w,f.id).outlet;if(!outlet)continue;
      const out=applyLakeOutlet(w,{height:naturalHeight,water:-Infinity,kind:0},wx,wz,outlet);if(out.lakeOutlet!==undefined)return out;
    }
    return null;
  }
  const out={height:s.height,water:s.water,kind:s.kind,salinity:s.kind?s.salinity:null,saltLakeId:s.kind?s.saltLakeId:null,lakeTemplateId:s.id};
  if(Math.abs(s.height-s.lake.level)<6)out.lakeShore={salinity:s.salinity,patch:s.patch,slope:.04,band:1-smooth(Math.abs(s.height-s.lake.level)/6)};
  return landscapes?applyLakeOutlet(w,out,wx,wz,templateLakeOutlet(w,s.id).outlet):out;
}
export function lakeLandscapeForCell(w,cell){
  const id=w.lakeId[cell]>=0?w.lakeId[cell]:lakeTemplateTile(w,cell)[0]?.id;
  return id===undefined?null:templateLakeOutlet(w,id);
}
export function lakeShoreStats(w){
  const c=lakeTemplateCatalog(w);return {step:w.circumference/w.columns,sampled:0,buildMs:0,visualEntries:c.tiles.size,visualBytes:c.tiles.size*32,templateQueries:c.queries,
    models:[...c.lakes.values()].map(l=>({id:l.id,level:l.level,originalLevel:l.originalLevel,tileCount:l.cells.length,bytes:l.cells.length*4}))};
}
