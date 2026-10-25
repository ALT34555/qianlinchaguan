// 等高距档位表与等高带阴影
import {TAU,TILE} from './tiles.mjs';

export const MAX_CONTOUR_LEVEL=22;
export const LEVEL_UNITS=Object.freeze({
  small:Object.freeze([240,240,180,128,96,64,48,32,32,24,24,16,16,12,12,8,8,4]),
  medium:Object.freeze([560,480,480,360,360,320,320,240,240,120,120,100,100,64,32,16,8,4]),
  large:Object.freeze([1024,960,720,512,400,320,320,256,256,128,128,64,64,32,32,16,8,4]),
});

/** 星球规模按宏观网格列数分档 */
export const contourSize=columns=>columns<=256?'small':columns<=512?'medium':'large';

/** 当前放大档独立取值，L21起固定4米 */
export function contourUnit(level,columns=1024){
  const at=Math.max(4,Math.min(21,Math.round(Number.isFinite(level)?level:4)));
  return LEVEL_UNITS[contourSize(columns)][at-4];
}

/** 等高线最小像素间隔，过密留底色 */
export const MIN_SEPARATION_PX=6;

/** 该档每像素覆盖米数 */
export const metersPerPixel=(circumference,level)=>circumference*TAU/2**level/TILE;

/** 相邻像素跨级且间距达标才描线 */
export function drawsContour(height,next,unit,metersPerPixelValue){
  if(!(unit>0)||height<-9000||next<-9000)return false;
  if(Math.floor(height/unit)===Math.floor(next/unit))return false;
  const slope=Math.abs(next-height)/Math.max(1e-9,metersPerPixelValue);
  return slope>0&&unit/slope>=MIN_SEPARATION_PX;
}

/** 等高带位置(0计曲线,1上一级,水下-1) */
export function bandPosition(height,unit){
  if(!(unit>0)||!(height>0))return -1;
  const cells=height/unit;
  return cells-Math.floor(cells);
}

/** 等高线外侧低处渐变阴影 */
export function terraceShade(band){
  if(band<0)return 1;
  const ramp=Math.min(1,(1-band)*2.6);
  return .84+.24*ramp;
}
