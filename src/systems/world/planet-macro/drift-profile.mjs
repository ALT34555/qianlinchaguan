const clamp01=v=>Math.max(0,Math.min(1,v));

// 板块活动度 → 漂移剖面，a=activity/9
export function driftProfile(activity){
  const a=clamp01(activity/9);
  return {
    span:0.05+0.027*a+0.473*a*a,
    speed:0.10+0.41*a+0.74*a*a,
    dispersion:0.15+0.36*a,
    beltWidth:0.090-0.055*a,
    upliftScale:1100+900*a,
    riftDepth:250+1068*a+182*a*a,
    steps:2+2.14*a+5.86*a*a,
  };
}
