import {lakeShoreVariation} from './lake-landscapes.mjs';
const caches=new WeakMap(),clamp=v=>Math.max(0,Math.min(1,v)),smooth=v=>{const t=clamp(v);return t*t*(3-2*t);};
export const LAKE_SHORE_TEMPLATES=Object.freeze([
  {name:'缓弯岸',bend:0,shoal:0},{name:'曲湾岸',bend:.35,shoal:0},
  {name:'岬角岸',bend:-.3,shoal:0},{name:'深湾岸',bend:.6,shoal:0},
  {name:'宽浅滩岸',bend:.18,shoal:.95},{name:'弧形浅滩岸',bend:-.45,shoal:.88},
  {name:'双湾浅滩岸',bend:.5,shoal:.82},{name:'沙洲浅滩岸',bend:-.6,shoal:.92},
  {name:'斑驳水洼岸',bend:.12,shoal:.7,features:[[-.27,-.5,.13,.4,2.4],[-.2,-.43,.06,.15,.9],[.22,-.33,.035,.11,1.5]]},
  {name:'冲刷回湾岸',bend:.05,shoal:.65,features:[[.02,-.22,.27,.58,1.5],[.02,-.25,.15,.25,-1.8]]},
  {name:'双岬分汊岸',bend:-.12,shoal:.8,features:[[-.2,-.2,.16,.5,1.4],[-.2,-.22,.07,.2,-1.7],[.2,-.2,.16,.5,1.4],[.2,-.22,.07,.2,-1.7]]},
  {name:'碎洲浅湾岸',bend:.22,shoal:.9,features:[[-.22,.35,.065,.16,-1.2],[.02,.46,.08,.2,-1.4],[.25,.35,.07,.18,-1.2]]},
  {name:'散落残潭岸',bend:-.18,shoal:.7,features:[[-.29,-.3,.026,.13,1.5],[-.16,-.65,.11,.35,2.6],[-.08,-.52,.05,.17,1.1],[.3,-.32,.018,.09,1.6]]},
  {name:'长汊沙洲岸',bend:.08,shoal:.85,features:[[0,-.18,.36,.46,1.4],[0,-.22,.26,.17,-1.8],[.28,.08,.055,.3,.8]]},
  {name:'锯湾碎滩岸',bend:.28,shoal:.9,features:[[-.27,-.14,.08,.45,.9],[-.09,.16,.065,.42,-1],[.1,-.16,.075,.5,1.1],[.28,.18,.06,.4,-1]]},
  {name:'复合斑驳岸',bend:-.08,shoal:.8,features:[[-.12,-.2,.23,.55,1.5],[-.12,-.24,.12,.22,-1.9],[.27,-.35,.06,.19,1.2],[.2,.3,.07,.2,-1.2]]},
  {name:'舒展弧岸',bend:0,shoal:.5,curve:[.22,.18,.4,0,0]},
  {name:'偏弯长湾',bend:0,shoal:.65,curve:[-.25,.7,.5,-.4,0]},
  {name:'收尖曲岬',bend:0,shoal:.25,curve:[.38,-.42,-1.6,.5,0]},
  {name:'浅弧缓岸',bend:0,shoal:.8,curve:[-.12,-.15,.65,.2,0]},
  {name:'舒缓双曲岸',bend:0,shoal:.45,curve:[.02,.35,.1,-.6,.18]},
  {name:'偏心深湾',bend:0,shoal:.35,curve:[-.38,-.6,1.2,.4,0]},
  {name:'斜展曲岸',bend:0,shoal:.6,curve:[.08,1.1,-.25,-1.1,0]},
  {name:'长弧回岸',bend:0,shoal:.75,curve:[.16,-.35,-.5,.8,-.22]},
].map(t=>Object.freeze({...t,curve:Object.freeze(t.curve??[]),features:Object.freeze((t.features??[]).map(f=>Object.freeze(f)))})));
export const LAKE_CENTER_TEMPLATES=Object.freeze([
  {name:'开阔深水',depth:1,islands:[]},
  {name:'平缓湖床',depth:.68,islands:[]},
  {name:'水下浅台',depth:.4,islands:[]},
  {name:'偏心单岛',depth:1,islands:[[.7,.5,.13,.17]]},
  {name:'错落双岛',depth:.85,islands:[[.29,.36,.12,.15],[.73,.66,.1,.13]]},
  {name:'湖心群岛',depth:.75,islands:[[.28,.3,.1,.12],[.72,.34,.12,.1],[.65,.74,.13,.1]]},
  {name:'弯月伴岛',depth:.8,ribbon:5,islands:[[.27,.46,.075,.11],[.32,.34,.09,.09],[.44,.29,.11,.08],[.57,.31,.1,.08],[.66,.38,.09,.09],[.72,.48,.065,.1],[.76,.69,.045,.06]]},
  {name:'斜脊长岛',depth:.85,islands:[[.37,.44,.065,.27],[.58,.73,.035,.06]]},
  {name:'冲沟孤岛',depth:.75,islands:[[.3,.69,.15,.19],[.62,.8,.035,.035]],cuts:[[.3,.69,.52,.61,.045]]},
  {name:'裂岬弧洲',depth:.8,ribbon:4,islands:[[.25,.34,.09,.12],[.35,.27,.12,.09],[.49,.26,.14,.08],[.61,.3,.12,.09],[.7,.38,.09,.12],[.8,.54,.04,.035],[.85,.67,.055,.035]],cuts:[[.39,.28,.39,.1,.03],[.6,.32,.75,.18,.03]]},
  {name:'斜贯深槽',depth:.5,islands:[],basins:[[.3,.67,.16,.18,.9],[.44,.51,.17,.18,1],[.59,.35,.16,.18,.9]]},
  {name:'双潭水势',depth:.5,islands:[],basins:[[.32,.35,.19,.23,1.1],[.67,.67,.2,.22,1.2]]},
  {name:'回弧深湾',depth:.5,islands:[],basins:[[.26,.38,.14,.19,1],[.29,.59,.15,.19,1],[.46,.72,.19,.14,1.1],[.67,.63,.17,.16,1]]},
  {name:'偏心长潭',depth:.5,islands:[],basins:[[.36,.55,.13,.32,1.4],[.52,.4,.14,.19,.75]]},
].map(t=>Object.freeze({...t,...Object.fromEntries(['islands','cuts','basins'].map(k=>[k,Object.freeze((t[k]??[]).map(i=>Object.freeze(i)))]))})));
export const LAKE_CORNER_TEMPLATES=Object.freeze([
  {name:'回钩转角',bend:.14,shoal:.7,features:[[-.18,-.2,.19,.52,1.5],[-.18,-.24,.09,.22,-1.8],[.22,-.22,.065,.35,1]]},
  {name:'斜汊转角',bend:-.2,shoal:.85,features:[[0,-.22,.32,.5,1.6],[0,-.27,.21,.18,-1.9],[.24,.3,.05,.23,-1.1]]},
  {name:'双岬转角',bend:.08,shoal:.75,features:[[-.2,-.2,.15,.5,1.6],[-.2,-.23,.065,.21,-1.9],[.18,-.18,.16,.5,1.5],[.18,-.22,.07,.2,-1.8]]},
  {name:'绕洲转角',bend:-.12,shoal:.9,features:[[0,-.18,.34,.6,1.7],[0,-.24,.23,.25,-2],[.23,.32,.06,.18,-1.2]]},
  {name:'碎岬转角',bend:.22,shoal:.8,features:[[-.26,.3,.065,.24,-1.2],[0,.35,.08,.25,-1.4],[.23,-.22,.1,.4,1.1]]},
  {name:'蚀湾转角',bend:-.18,shoal:.7,features:[[-.24,-.12,.11,.4,1.2],[0,-.16,.12,.45,1.3],[.24,-.14,.1,.4,1.1]]},
  {name:'串汊转角',bend:.1,shoal:.85,features:[[-.24,-.2,.15,.5,1.5],[-.24,-.23,.06,.2,-1.8],[.08,-.2,.17,.5,1.6],[.08,-.22,.07,.2,-1.8]]},
  {name:'穿洲转角',bend:-.08,shoal:.8,features:[[-.05,-.2,.28,.55,1.6],[-.05,-.24,.16,.23,-1.9],[.28,-.35,.055,.2,1.2],[-.26,.35,.06,.18,-1.2]]},
].map(t=>Object.freeze({...t,features:Object.freeze(t.features.map(f=>Object.freeze(f)))})));
const centerGroups=[false,true].map(island=>LAKE_CENTER_TEMPLATES.flatMap((t,i)=>Boolean(t.islands.length)===island?[i]:[]));
function hash(seed){let n=seed>>>0;n=Math.imul(n^(n>>>16),0x7feb352d);n=Math.imul(n^(n>>>15),0x846ca68b);return (n^(n>>>16))>>>0;}
const islandChances=Object.freeze([.02,.05,.08,.1,.12,.14,.16,.18,.19,.2]);
export function lakeIslandChance(activity=5){return islandChances[Math.max(0,Math.min(9,Math.round(activity)))];}
export function lakeIslandEnabled(seed,activity=5){return hash(seed)/4294967296<lakeIslandChance(activity);}
export function lakeCenterVariant(seed,islands){const group=centerGroups[Number(islands)];return group[hash(seed^0x9e3779b9)%group.length];}
export function lakeShoreFeatures(variant,seed){
  const features=LAKE_SHORE_TEMPLATES[variant].features;
  if(variant!==8&&variant!==12)return features;
  return features.map((f,i)=>{
    const a=hash(seed^Math.imul(i+1,0x9e3779b9))/4294967296,b=hash(seed^Math.imul(i+1,0x85ebca6b))/4294967296;
    return [f[0]+(a-.5)*.08,f[1]+(b-.5)*.1,f[2]*(.7+a*.6),f[3]*(.7+b*.6),f[4]];
  });
}
function impactRadius(x,z){return .7*(Math.abs(x+.2*z)+Math.abs(z))+.3*Math.hypot(x,z);}
export function templateCell(w,x,y){
  y=((y%(w.rows*2))+w.rows*2)%(w.rows*2);if(y>=w.rows){y=w.rows*2-y-1;x+=w.columns/2;}
  return y*w.columns+(x%w.columns+w.columns)%w.columns;
}
export function lakeTemplateCatalog(w){
  let c=caches.get(w);if(c)return c;
  c={lakes:new Map(),tiles:new Map(),queries:0};
  for(let i=0;i<w.water.length;i++)if(w.water[i]===2){
    const id=w.lakeId[i];if(!c.lakes.has(id))c.lakes.set(id,{id,level:Math.fround(w.lakes[id].level),originalLevel:w.lakes[id].level,salinity:w.lakes[id].salinity??'fresh',cells:[],depth:(Math.imul(id+1,31)^w.options.seed)>>>0});
    c.lakes.get(id).cells.push(i);
  }
  for(const lake of c.lakes.values()){
    lake.depth=6+(lake.depth%110)/10;
  }
  caches.set(w,c);return c;
}
function corner(w,id,x,y){
  let own=false,foreign=false;
  for(let dy=-1;dy<=0;dy++)for(let dx=-1;dx<=0;dx++){const i=templateCell(w,x+dx,y+dy);if(w.water[i]===1)foreign=true;if(w.water[i]===2){if(w.lakeId[i]===id)own=true;else foreign=true;}}
  return own&&!foreign?1:0;
}
export function lakeTemplateTile(w,cell){
  const c=lakeTemplateCatalog(w);if(c.tiles.has(cell))return c.tiles.get(cell);
  const col=cell%w.columns,row=Math.floor(cell/w.columns),ids=new Set();
  for(let dz=-1;dz<=1;dz++)for(let dx=-1;dx<=1;dx++){const i=templateCell(w,col+dx,row+dz);if(w.water[i]===2)ids.add(w.lakeId[i]);}
  const forms=[...ids].map(id=>{const bits=[corner(w,id,col,row),corner(w,id,col+1,row),corner(w,id,col,row+1),corner(w,id,col+1,row+1)],seed=(Math.imul(cell+1,374761393)^w.options.seed^id)>>>0;
    const variant=seed%LAKE_SHORE_TEMPLATES.length;
    const center=w.water[cell]===2&&w.lakeId[cell]===id,pureWater=center&&bits.every(v=>v===1);
    const islands=pureWater&&lakeIslandEnabled(seed,w.options.activity??5);
    return {id,bits,center,variant,shoreFeatures:lakeShoreFeatures(variant,seed),cornerVariant:(seed>>>12)%LAKE_CORNER_TEMPLATES.length,centerVariant:lakeCenterVariant(seed,islands),phase:seed%628/100};});
  c.tiles.set(cell,forms);if(c.tiles.size>4096)c.tiles.delete(c.tiles.keys().next().value);return forms;
}
export function templateField(form,x,z){
  const u=smooth(x),v=smooth(z),b=form.bits,base=((b[0]*(1-u)+b[1]*u)*(1-v)+(b[2]*(1-u)+b[3]*u)*v)*2-1;
  const envelope=256*(x*(1-x)*z*(1-z))**2;
  const shore=shoreTemplate(form),frequency=form.variant===6&&!isCorner(form)?4:2;
  const gx=b[1]+b[3]-b[0]-b[2],gz=b[2]+b[3]-b[0]-b[1],length=Math.hypot(gx,gz)||1;
  const along=((z-.5)*gx-(x-.5)*gz)/length;
  const curve=shore.curve;
  const bend=curve?.length?(curve[0]+curve[1]*along+curve[2]*along*along+curve[3]*along**3+curve[4]*Math.sin(along*Math.PI*2+form.phase*.2))*smooth(Math.hypot(x-.5,z-.5)/.18):shore.bend*Math.sin(x*Math.PI*frequency+form.phase)*Math.sin(z*Math.PI);
  const centerLift=form.center?(isCorner(form)&&b.reduce((s,v)=>s+v,0)===3?.25:1.25):-.28;
  const field=base+envelope*(centerLift+bend);
  if(!shore.features.length||b.every(v=>v===b[0]))return field;
  const band=1-smooth((Math.abs(field)-.45)/.65);
  const center=form.center?smooth(Math.hypot(x-.5,z-.5)/.18):1;
  const ponds=!isCorner(form)&&(form.variant===8||form.variant===12);
  let detail=ponds ? .025*Math.sin(along*13+form.phase)*Math.sin(field*7-form.phase) : .12*Math.sin(along*47+form.phase)*Math.sin(field*13-form.phase);
  const features=isCorner(form)?shore.features:form.shoreFeatures??shore.features;
  for(let i=0;i<features.length;i++){
    const [s,n,rs,rn,strength]=features[i];
    let radius=impactRadius((along-s)/rs,(field-n)/rn);
    if(ponds){
      const a=(along-s)/rs,d=(field-n)/rn,warped=d+.26*Math.sin(a*2.4+form.phase+i);
      radius=.8*Math.hypot(a+.25*Math.sin(d*3+i),warped)+.2*impactRadius(a,warped);
    }
    if(strength<0&&n<0)radius=Math.abs((along-s)/(rs*(.6+.4*clamp((n-field)/.6))))+Math.max(0,(field-n)/rn);
    detail+=strength*(1-smooth((radius-.25)/.75));
  }
  return field+envelope*band*center*detail;
}
function isCorner(form){const count=form.bits.reduce((sum,v)=>sum+v,0);return form.cornerVariant!==undefined&&(count===1||count===3);}
function shoreTemplate(form){return isCorner(form)?LAKE_CORNER_TEMPLATES[form.cornerVariant]:LAKE_SHORE_TEMPLATES[form.variant];}
export function templateLakeBed(form,x,z,depth,field,patch){
  const shore=shoreTemplate(form),center=LAKE_CENTER_TEMPLATES[form.centerVariant??0];
  const envelope=256*(x*(1-x)*z*(1-z))**2,inside=smooth(field*4);
  const shelf=shore.shoal*envelope*(1-smooth((field-.18)/.9));
  let bed=-depth*inside;
  bed=bed*(1-shelf)-Math.min(depth,.35+patch*.55)*inside*shelf;
  if(form.center){
    bed*=1-(1-center.depth)*envelope;
    const angle=form.phase,dx=x-.5,dz=z-.5;
    const u=dx*Math.cos(angle)-dz*Math.sin(angle)+.5,v=dx*Math.sin(angle)+dz*Math.cos(angle)+.5;
    for(const [cx,cz,rx,rz,strength] of center.basins){
      const radius=Math.hypot((u-cx)/rx,(v-cz)/rz);
      bed*=1+envelope*strength*(1-smooth(radius));
    }
    let islandMask=0;
    for(const [cx,cz,rx,rz] of center.islands){
      const radius=impactRadius((u-cx)/rx,(v-cz)/rz);
      const island=1-smooth((radius-.3)/.7);
      islandMask=Math.max(islandMask,island);
    }
    for(let k=1;k<=(center.ribbon??0);k++){
      const a=center.islands[k-1],b=center.islands[k],dx=b[0]-a[0],dz=b[1]-a[1];
      const t=clamp(((u-a[0])*dx+(v-a[1])*dz)/(dx*dx+dz*dz));
      const radius=Math.hypot(u-a[0]-t*dx,v-a[1]-t*dz)/Math.min(a[2],a[3],b[2],b[3]);
      islandMask=Math.max(islandMask,1-smooth((radius-.45)/.55));
    }
    for(const [ax,az,bx,bz,width] of center.cuts){
      const dx=bx-ax,dz=bz-az,length=Math.hypot(dx,dz),along=((u-ax)*dx+(v-az)*dz)/length;
      const across=((u-ax)*dz-(v-az)*dx)/length;
      const radius=Math.abs(across)/(width*(.4+.8*clamp(along/length)))+Math.max(0,-along/width,(along-length)/width);
      islandMask*=smooth((radius-.4)/.6);
    }
    if((form.centerVariant??0)>=6)islandMask*=smooth((Math.hypot(x-.5,z-.5)-.06)/.09);
    islandMask*=smooth(field/.5)*smooth(Math.min(x,1-x,z,1-z)/.08);
    bed+=(depth+2.5)*islandMask;
  }
  return bed;
}
export function sampleTemplateLake(w,wx,wz,natural=0){
  const span=w.circumference/w.columns,gx=(wx/w.circumference+.5)*w.columns,gz=(wz/w.circumference+.25)*w.columns,col=Math.floor(gx),row=Math.floor(gz),x=gx-col,z=gz-row;
  const c=lakeTemplateCatalog(w);c.queries++;let best=null;
  for(const form of lakeTemplateTile(w,templateCell(w,col,row))){
    const field=templateField(form,x,z);if(field<=-.85||best&&field<=best.field)continue;
    const lake=c.lakes.get(form.id),patch=lakeShoreVariation(w,wx,wz,8);
    let height;
    if(field>=0)height=lake.level+templateLakeBed(form,x,z,lake.depth,field,patch);
    else {const target=lake.level+Math.max(4,span*.002)*smooth(-field),weight=1-smooth((-field-.25)/.6);height=natural+(target-natural)*weight;}
    best={height,water:field>0&&height<lake.level?lake.level:-Infinity,kind:field>0&&height<lake.level?2:0,field,patch,lake,id:lake.id,salinity:lake.salinity,saltLakeId:lake.salinity==='salt'?lake.id:null};
  }
  return best;
}
