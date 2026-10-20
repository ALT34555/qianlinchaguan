import {TileRenderer} from './tile-renderer.mjs';
let renderer,generation=0,revision=0;
self.onmessage=async({data:m})=>{
  if(m.kind==='world'){renderer=new TileRenderer(m.world);generation=m.generation;revision++;return;}
  if(m.generation!==generation)return;
  if(m.kind==='cancel'){revision++;return;}
  if(m.kind!=='tile')return;
  const job=++revision;
  try{
    let reported=0;
    const result=await renderer.render(m.tile,m.layer,()=>job!==revision,progress=>{
      if(job===revision&&performance.now()-reported>150){reported=performance.now();self.postMessage({kind:'progress',id:m.id,...progress});}
    },{focusId:m.focusId??null});
    if(result)self.postMessage({...m,kind:'tile',...result},[result.pixels.buffer]);
    else self.postMessage({...m,kind:'cancelled'});
  }catch(error){self.postMessage({...m,kind:'error',message:error.message});}
};
