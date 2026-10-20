// 共享下游代价形成连续汇流走廊
export function convergeDrainage(g,height,spill,rank,order,receiver,catchment,potential,resistance){
  catchment.set(g.areas);
  for(let at=order.length-1;at>=0;at--){
    const i=order[at],r=receiver[i];
    if(r>=0)catchment[r]+=catchment[i];
  }
  for(const i of order){
    if(height[i]<=0||receiver[i]<0)continue;
    let steepest=0,originalLength=1;
    for(let d=0;d<8;d++){
      const j=g.neighbors[i*8+d];
      if(j===receiver[i])originalLength=g.lengths[i*8+d];
      if(rank[j]>=rank[i]||spill[j]>spill[i])continue;
      steepest=Math.max(steepest,(spill[i]-spill[j])/Math.max(1,g.lengths[i*8+d]));
    }
    const friction=resistance(i),gentle=steepest<.006;
    if(steepest<=0||spill[i]-height[i]>=8){potential[i]=potential[receiver[i]]+originalLength*friction;continue;}
    let best=receiver[i],score=Infinity;
    for(let d=0;d<8;d++){
      const j=g.neighbors[i*8+d];
      if(rank[j]>=rank[i]||spill[j]>spill[i])continue;
      const grade=(spill[i]-spill[j])/Math.max(1,g.lengths[i*8+d]);
      if(grade<steepest*(gentle?.008:.08))continue;
      const attraction=Math.log2(1+catchment[j]/Math.max(1e-9,g.areas[j]));
      const penalty=1+(1-grade/steepest)*(gentle?.35:2);
      let turn=0;
      const r=receiver[j];
      if(g.w&&r>=0){
        const wrap=v=>v>g.w/2?v-g.w:v<-g.w/2?v+g.w:v;
        const ax=wrap(j%g.w-i%g.w),ay=Math.floor(j/g.w)-Math.floor(i/g.w);
        const bx=wrap(r%g.w-j%g.w),by=Math.floor(r/g.w)-Math.floor(j/g.w);
        turn=1-(ax*bx+ay*by)/Math.max(1e-9,Math.hypot(ax,ay)*Math.hypot(bx,by));
      }
      const candidate=potential[j]+g.lengths[i*8+d]*friction*penalty*(1+turn*.25)/(1+attraction*.35);
      if(candidate<score){score=candidate;best=j;}
    }
    receiver[i]=best;
    potential[i]=score;
  }
  catchment.fill(0);
  potential.fill(0);
}
