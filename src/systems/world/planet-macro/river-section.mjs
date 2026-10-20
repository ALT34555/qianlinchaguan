// 只下切，不额外造岸
export function riverSection(distance,radius,depth,level,natural){
  const width=Math.max(1,radius),r=distance/width;
  if(r>=1)return null;
  const weight=(1-r*r)**2,targetBed=level-Math.max(0,depth),height=natural-Math.max(0,natural-targetBed)*weight;
  return {height,wet:height<level};
}
