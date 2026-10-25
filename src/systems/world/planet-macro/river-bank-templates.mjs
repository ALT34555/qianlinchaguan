import {LAKE_CORNER_TEMPLATES,lakeShoreFeatures,templateField,templateLakeBed} from './lake-templates.mjs';

export const RIVER_BANK_SPAN=64;
const clamp=v=>Math.max(0,Math.min(1,v));
const smooth=v=>{const t=clamp(v);return t*t*(3-2*t);};
const shoalCurves=Object.freeze([[.28,.35,-.5],[-.32,.2,.7],[.08,-.6,1.2],[-.15,.8,-1.1]].map(Object.freeze));
export const RIVER_BANK_TEMPLATES=Object.freeze([
  ['舒展弧岸',16],['偏弯长湾',17],['收尖曲岬',18],['浅弧缓岸',19],
  ['舒缓双曲岸',20],['偏心深湾',21],['斜展曲岸',22],['长弧回岸',23],
  ['宽浅滩岸',4],['弧形浅滩岸',5],['双湾浅滩岸',6],['沙洲浅滩岸',7],
  ['弯道内滩',9],['错列边滩',10],['顺流附岸滩',11],['汊槽长滩',13],
].map(([name,lakeVariant])=>Object.freeze({name,lakeVariant})));
export const RIVER_BANK_CORNERS=Object.freeze(LAKE_CORNER_TEMPLATES.map(({name},cornerVariant)=>Object.freeze({name,cornerVariant})));

export function createRiverBankTemplate({variant=0,cornerVariant=null,mask=10,seed=0}={}){
  if(!Number.isInteger(variant)||variant<0||variant>=RIVER_BANK_TEMPLATES.length)throw new RangeError('河岸模板编号越界');
  if(!Number.isInteger(mask)||mask<0||mask>15)throw new RangeError('河岸四角编号越界');
  if(cornerVariant!==null&&(!Number.isInteger(cornerVariant)||cornerVariant<0||cornerVariant>=RIVER_BANK_CORNERS.length))throw new RangeError('河岸转角编号越界');
  const lakeVariant=RIVER_BANK_TEMPLATES[variant].lakeVariant;
  return Object.freeze({bits:Object.freeze([0,1,2,3].map(k=>(mask>>k)&1)),center:false,variant:lakeVariant,riverVariant:variant,
    ...(cornerVariant===null?{}:{cornerVariant}),centerVariant:0,phase:((seed>>>0)%628)/100,
    shoreFeatures:Object.freeze(lakeShoreFeatures(lakeVariant,seed>>>0).map(f=>Object.freeze([...f])))});
}

function flowBank(form,u,v,depth,bankHeight,curvature,flowDirection){
  const b=form.bits,gx=b[1]+b[3]-b[0]-b[2],gz=b[2]+b[3]-b[0]-b[1];
  const along=((v-.5)*gx-(u-.5)*gz)/(Math.hypot(gx,gz)||1)*flowDirection+.025*Math.sin(form.phase);
  const envelope=256*(u*(1-u)*v*(1-v))**2;
  const lobe=(center,up,down)=>Math.exp(-(((along-center)/(along<center?up:down))**2));
  const variant=form.riverVariant-12,inner=Math.max(0,curvature),outer=Math.max(0,-curvature);
  const profiles=[lobe(-.02,.2,.3),.8*lobe(-.22,.12,.2)+.5*lobe(.24,.16,.1),
    lobe(.06,.28,.18),lobe(-.04,.29,.33)];
  const deposit=profiles[variant]*(.2+.8*inner);
  const a=smooth(u),c=smooth(v);
  const base=2*((b[0]*(1-a)+b[1]*a)*(1-c)+(b[2]*(1-a)+b[3]*a)*c)-1;
  const field=base+envelope*(-.16-[.72,.64,.28,.4][variant]*deposit+.3*outer*profiles[variant]);
  const chute=variant===3?lobe(.13,.24,.16)*Math.exp(-(((field-.38)/.13)**2)):0;
  const wet=field>0;
  const shallow=clamp(2.4*envelope*deposit)*(1-smooth((field-.35)/.65))*(1-outer)*(1-.85*chute);
  const height=wet?-depth*smooth(field*4)*(1-.9*shallow):bankHeight*smooth(-field);
  return {field,height,wet};
}

export function sampleRiverBankTemplate(form,x,z,{depth=3,bankHeight=2,curvature=.65,flowDirection=1}={}){
  if(!Number.isFinite(x)||!Number.isFinite(z)||x<0||z<0||x>64||z>64)throw new RangeError('河岸坐标须在0至64格内');
  if(!Number.isFinite(depth)||depth<=0||!Number.isFinite(bankHeight)||bankHeight<=0)throw new RangeError('河岸高差须大于零');
  if(!Number.isFinite(curvature)||Math.abs(curvature)>1||![1,-1].includes(flowDirection))throw new RangeError('弯向须为-1至1，流向须为正负1');
  const u=x/RIVER_BANK_SPAN,v=z/RIVER_BANK_SPAN;
  if(form.riverVariant>=12&&form.cornerVariant===undefined&&[3,5,10,12].includes(form.bits.reduce((m,b,i)=>m|(b<<i),0)))return flowBank(form,u,v,depth,bankHeight,curvature,flowDirection);
  let field=templateField(form,u,v);
  const curve=shoalCurves[form.riverVariant-8],b=form.bits;
  if(curve&&form.cornerVariant===undefined&&b.some(n=>n!==b[0])){
    const gx=b[1]+b[3]-b[0]-b[2],gz=b[2]+b[3]-b[0]-b[1];
    const along=((v-.5)*gx-(u-.5)*gz)/(Math.hypot(gx,gz)||1);
    field+=256*(u*(1-u)*v*(1-v))**2*(curve[0]+curve[1]*along+curve[2]*along*along);
  }
  const height=field>0?templateLakeBed(form,u,v,depth,field,.5):bankHeight*smooth(-field);
  return {field,height,wet:field>0};
}

// 双岸组合保留中央通水带
export function sampleRiverChannelTemplate(left,right,x,z,{width=24,depth=3,bankHeight=2,curvature=.65,flowDirection=1}={}){
  if(!Number.isFinite(width)||width<8||width>40)throw new RangeError('示例河宽须在8至40格内');
  if(!Number.isFinite(x)||!Number.isFinite(z)||x<0||z<0||x>64||z>64)throw new RangeError('河道坐标须在0至64格内');
  const a=sampleRiverBankTemplate(left,clamp((x-(32-width/2))/32+.5)*64,z,{depth,bankHeight,curvature,flowDirection});
  const b=sampleRiverBankTemplate(right,clamp(((32+width/2)-x)/32+.5)*64,z,{depth,bankHeight,curvature:-curvature,flowDirection});
  const core=1-smooth((Math.abs(x-32)-width*.2)/(width*.3));
  const height=Math.min(Math.max(a.height,b.height),-depth*core+(1-core)*bankHeight);
  return {height,wet:height<0};
}
