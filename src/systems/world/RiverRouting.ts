export const STRAIGHT_RIVER_PERTURBATION=.3;
/** 扰动概率随温度由0.2升至0.4。 */
export function riverPerturbationProbability(temperature:number):number{
  return .2+.2*Math.max(0,Math.min(1,temperature/28));
}
type Cell=readonly [number,number];
/** 规范坐标哈希，与查询顺序无关。 */
export function riverRouteRandom(seed:number,x:number,z:number,salt:number):number{
  let n=(seed^Math.imul(x,0x45d9f3b)^Math.imul(z,0x119de1f3)^Math.imul(salt,0x27d4eb2d))>>>0;
  n=Math.imul(n^(n>>>16),0x7feb352d);n=Math.imul(n^(n>>>15),0x846ca68b);
  return ((n^(n>>>16))>>>0)/4294967296;
}

/** 直段改为三步缓弯，两端保持。 */
export function riverDirectionRoute(a:Cell,b:Cell,random:(salt:number)=>number,
  allowed:(previous:Cell,detour:Cell,next:Cell)=>boolean,
  probability:number|((cell:Cell)=>number)=STRAIGHT_RIVER_PERTURBATION):Cell[]{
  const count=Math.max(Math.abs(b[0]-a[0]),Math.abs(b[1]-a[1]));
  if(!count)return [a];
  const points:Cell[]=Array.from({length:count+1},(_,i)=>[
    Math.round(a[0]+(b[0]-a[0])*i/count),Math.round(a[1]+(b[1]-a[1])*i/count)]);
  if(count<6||a[0]!==b[0]&&a[1]!==b[1])return points;
  const dx=Math.sign(b[0]-a[0]),dz=Math.sign(b[1]-a[1]);
  // 保护粗节点与汇流口。
  for(let i=Math.min(4,count-4);i<=count-4;i+=8){
    const chance=typeof probability==='number'?probability:probability(points[i]);
    if(random(i)>=chance)continue;
    const side=random(i+1)<.5?-1:1,original=points[i],following=points[i+1];
    for(const sign of [side,-side]){
      const first:Cell=[original[0]-dz*sign,original[1]+dx*sign];
      const second:Cell=[following[0]-dz*sign,following[1]+dx*sign];
      if(!allowed(points[i-1],first,second)||!allowed(first,second,points[i+2]))continue;
      points[i]=first;points[i+1]=second;break;
    }
  }
  return points;
}
