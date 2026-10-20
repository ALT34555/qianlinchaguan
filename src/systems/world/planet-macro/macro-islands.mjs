// 原海床抽取后统一抬升，允许连片
export function raiseMacroIslands(g,options,height,boundary){
  let seed=options.seed^0x6d69736c;
  const random=()=>{let t=seed+=0x6d2b79f5;t=Math.imul(t^t>>>15,t|1);t^=t+Math.imul(t^t>>>7,t|61);return((t^t>>>14)>>>0)/4294967296;};
  const islands=[];
  for(let cell=0;cell<height.length;cell++){
    const baseHeight=height[cell];if(baseHeight>= -16)continue;
    let eligible=true,nearCollision=false;
    for(let d=0;d<8;d++){
      const next=g.neighbors[cell*8+d];
      if(height[next]>= -16){eligible=false;break;}
      if(boundary[next]===1)nearCollision=true;
    }
    if(!eligible)continue;
    const probability=boundary[cell]===1?.03:nearCollision?.004:0;
    if(!probability||random()>=probability)continue;
    islands.push({id:islands.length,cell,baseHeight,peakHeight:32+random()*128,selectionProbability:probability});
  }
  for(const island of islands){height[island.cell]=island.peakHeight;island.peakHeight=height[island.cell];}
  return islands;
}
