// 舆图图层定义与状态，详见同名文档
export const LAYER_IDS=['civilization','chunks','baseline','daylight','rain','contour','climate','temperature','height','ice','depth'];

/** 图层顺序即合成顺序：列表在前 = 画在上层 */
export const LAYER_DEFS=[
  {id:'civilization',opacity:true,slider:null},
  {id:'chunks',opacity:false,slider:null},
  {id:'baseline',opacity:false,slider:null},
  {id:'daylight',opacity:true,slider:null},
  {id:'rain',opacity:true,slider:null},
  {id:'contour',opacity:false,slider:null},
  {id:'climate',opacity:true,slider:null},
  {id:'temperature',opacity:true,slider:null},
  {id:'height',opacity:true,slider:null},
  {id:'ice',opacity:false,slider:null},
  {id:'depth',opacity:false,slider:null},
];

const clamp01=v=>v<0?0:v>1?1:v;
const byId=id=>LAYER_DEFS.find(def=>def.id===id);

/** 默认仅开昼夜光照（50%）与冰层 */
export function defaultLayers(){
  return [
    {id:'civilization',on:false,opacity:1},
    {id:'chunks',on:false,opacity:1},
    {id:'baseline',on:false,opacity:1},
    {id:'daylight',on:true,opacity:.5},
    {id:'rain',on:false,opacity:.55},
    {id:'contour',on:false,opacity:1},
    {id:'climate',on:false,opacity:.5},
    {id:'temperature',on:false,opacity:.45},
    {id:'height',on:false,opacity:.5},
    {id:'ice',on:true,opacity:1},
    {id:'depth',on:false,opacity:1},
  ];
}

/** 清理无效项，补齐缺失图层 */
export function normalizeLayers(value){
  const source=Array.isArray(value)?value:null;
  if(!source)return defaultLayers();
  const defaults=new Map(defaultLayers().map(l=>[l.id,l]));
  const seen=new Set(),out=[];
  for(const raw of source){
    if(!raw||typeof raw!=='object')continue;
    const id=raw.id;
    if(!byId(id)||seen.has(id))continue;
    seen.add(id);
    const fallback=defaults.get(id);
    out.push({id,on:raw.on===undefined?fallback.on:!!raw.on,
      opacity:clamp01(Number.isFinite(raw.opacity)?raw.opacity:fallback.opacity)});
  }
  for(const [id,fallback] of defaults)if(!seen.has(id)){
    // 文明层补在顶层
    if(id==='civilization')out.unshift({...fallback});else out.push({...fallback});
  }
  return out;
}

/** 等高线密度：1 最稀、5 最密，映射到一屏目标层数 */
export const DENSITY_RANGE=[8,12,16,22,30];
export function layersKey(layers,density){
  return layers.map(l=>`${l.id}:${l.on?1:0}:${(l.on?l.opacity:0).toFixed(2)}`).join('|')+`|d${density}`;
}
export const clampDensity=value=>{
  const n=Number(value);return Number.isFinite(n)?Math.max(0,Math.min(DENSITY_RANGE.length-1,Math.round(n))):2;
};
/** 一屏目标层数 */
export const contourLevelsFor=density=>DENSITY_RANGE[clampDensity(density)];
