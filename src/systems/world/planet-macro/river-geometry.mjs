export function macroRiverSegment(columns,rows,circumference,cell,receiver,referenceX=0){
  const wrap=v=>((v+circumference/2)%circumference+circumference)%circumference-circumference/2;
  const a={x:((cell%columns+.5)/columns-.5)*circumference,z:(Math.floor(cell/columns)+.5)/rows*circumference/2-circumference/4};
  const b={x:((receiver%columns+.5)/columns-.5)*circumference,z:(Math.floor(receiver/columns)+.5)/rows*circumference/2-circumference/4};
  a.x=referenceX+wrap(a.x-referenceX);b.x=a.x+wrap(b.x-a.x);
  return {a,b};
}

export function clipRiverSegment(a,b,x0,z0,x1,z1){
  const dx=b.x-a.x,dz=b.z-a.z;let lo=0,hi=1;
  for(const [p,q] of [[-dx,a.x-x0],[dx,x1-a.x],[-dz,a.z-z0],[dz,z1-a.z]]){
    if(Math.abs(p)<1e-12){if(q<0)return null;}
    else{const t=q/p;if(p<0)lo=Math.max(lo,t);else hi=Math.min(hi,t);}
  }
  if(lo>hi)return null;
  return [lo,hi].map(t=>({...a,x:a.x+dx*t,z:a.z+dz*t,level:a.level+(b.level-a.level)*t}));
}
