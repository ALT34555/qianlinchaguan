import {project} from './tiles.mjs';

/** 基准线线宽固定5px */
export const BASELINE_WIDTH=5;
/** 朱砂实线，无虚线样式 */
const STROKE='rgba(194,51,63,.82)';
const DEGREE=Math.PI/180;

// 逐度采样，球背和日期线处断开
function strokeSamples(ctx,view,samples){
  let open=false,prevX=0;
  ctx.beginPath();
  for(const [lon,lat] of samples){
    const point=project(view,lon,lat);
    if(!point){open=false;continue;}
    if(open&&Math.abs(point.x-prevX)<=view.width/2)ctx.lineTo(point.x,point.y);
    else{ctx.moveTo(point.x,point.y);open=true;}
    prevX=point.x;
  }
  ctx.stroke();
}

/** 舆图基准线：本初子午线与赤道 */
export function drawBaselines(ctx,view){
  const equator=[],meridian=[];
  for(let lon=-180;lon<=180;lon++)equator.push([lon*DEGREE,0]);
  for(let lat=-90;lat<=90;lat++)meridian.push([0,lat*DEGREE]);
  ctx.save();
  ctx.strokeStyle=STROKE;ctx.lineWidth=BASELINE_WIDTH;
  ctx.lineCap='round';ctx.lineJoin='round';
  strokeSamples(ctx,view,equator);
  strokeSamples(ctx,view,meridian);
  ctx.restore();
}
