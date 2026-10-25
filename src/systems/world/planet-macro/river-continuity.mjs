import {macroBandWidth} from './river-dimensions.mjs';
const states=new WeakMap();
export function mainRiverField(w){
  const cached=states.get(w);if(cached)return cached;
  const widths=w.riverWidth.slice(),added=[],mouths=new Map();
  const mouth=cell=>{const sea=w.receiver[cell];if(sea>=0&&w.water[sea]===1)mouths.set(cell,{landCell:cell,seaCell:sea});};
  for(let i=0;i<widths.length;i++){
    if(!w.riverWidth[i])continue;
    mouth(i);
    let cell=w.receiver[i],steps=0;
    while(cell>=0&&steps++<widths.length){
      if(widths[cell]||w.water[cell]===1||w.water[cell]===2||!(w.height[cell]>0)||!(w.flow[cell]>0))break;
      widths[cell]=macroBandWidth(w.flow[cell])/2;added.push(cell);mouth(cell);cell=w.receiver[cell];
    }
  }
  const value={widths,added,mouths};states.set(w,value);return value;
}
export function mainRiverWidth(w,cell){return mainRiverField(w).widths[cell]??0;}
