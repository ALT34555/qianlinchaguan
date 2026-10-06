export type RiverCell=readonly [number,number];
/** 八方向直角由向量点积统一判定。 */
export function isRiverRightAngle(incoming:RiverCell,outgoing:RiverCell):boolean{
  return (incoming[0]!==0||incoming[1]!==0)&&(outgoing[0]!==0||outgoing[1]!==0)&&
    incoming[0]*outgoing[0]+incoming[1]*outgoing[1]===0;
}
/** 削去直角顶点，斜向直角补中间格。 */
export function riverTurnBridge(previous:RiverCell,corner:RiverCell,next:RiverCell):RiverCell[]|null{
  const incoming:RiverCell=[corner[0]-previous[0],corner[1]-previous[1]];
  const outgoing:RiverCell=[next[0]-corner[0],next[1]-corner[1]];
  if(!isRiverRightAngle(incoming,outgoing))return null;
  if(Math.max(Math.abs(next[0]-previous[0]),Math.abs(next[1]-previous[1]))===1)return [previous,next];
  return [previous,[(previous[0]+next[0])/2,(previous[1]+next[1])/2],next];
}
