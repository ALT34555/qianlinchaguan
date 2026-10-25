const incoming=new WeakMap();
const center=(w,i)=>({wx:((i%w.columns+.5)/w.columns-.5)*w.circumference,wz:((Math.floor(i/w.columns)+.5)/w.rows*.5-.25)*w.circumference});

// 稀疏主支流表，共享节点切线
export function riverCenterline(w,cell,frame,target){
  let previous=incoming.get(w);
  if(!previous){
    previous=new Map();
    for(let i=0;i<w.height.length;i++){
      const r=w.receiver[i];if(r<0||!w.riverWidth[i])continue;
      const old=previous.get(r);
      if(old===undefined||w.catchment[i]>w.catchment[old])previous.set(r,i);
    }
    incoming.set(w,previous);
  }
  const length=Math.hypot(target.x,target.z);
  const tangent=node=>{
    const a=center(w,previous.get(node)??node),b=center(w,w.receiver[node]>=0?w.receiver[node]:node),p=frame.local(a.wx,a.wz),q=frame.local(b.wx,b.wz);
    const dx=q.x-p.x,dz=q.z-p.z,scale=length*.85/Math.max(1,Math.hypot(dx,dz));
    return {x:dx*scale,z:dz*scale};
  };
  const start=tangent(cell),end=tangent(w.receiver[cell]);
  return t=>{
    const t2=t*t,t3=t2*t,a=t3-2*t2+t,b=-2*t3+3*t2,c=t3-t2;
    return {x:start.x*a+target.x*b+end.x*c,z:start.z*a+target.z*b+end.z*c};
  };
}
