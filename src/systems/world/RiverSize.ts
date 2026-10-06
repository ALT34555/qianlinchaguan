export interface RiverSize {capacity:1|2|4|6;blocks:number}
/** 宽度资格来自真实支流，沿干流继承。 */
export function mergedRiverSize(discharge:number,inlets:readonly RiverSize[]):RiverSize{
  let capacity:RiverSize['capacity']=discharge>=2500?2:1;
  for(const inlet of inlets)capacity=Math.max(capacity,inlet.capacity) as RiverSize['capacity'];
  const major=inlets.filter(i=>i.blocks>=2);
  if(major.length>=2)capacity=Math.max(capacity,4) as RiverSize['capacity'];
  if(major.length>=2&&major.some(i=>i.blocks>=3))capacity=6;
  const blocks=capacity===1?1:capacity===2?2:capacity===6&&discharge>=160000?6:
    capacity===6&&discharge>=60000?5:discharge>=20000?4:3;
  return {capacity,blocks};
}
