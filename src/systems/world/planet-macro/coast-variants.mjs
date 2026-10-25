export function coastVariantCoordinates(variant,u,v){
  if(variant===0)return {u,v,gain:1};
  const clamp=x=>Math.max(0,Math.min(1,x));
  const along=v*(1-v),a=[0,.19,-.24,.31][variant],b=[0,-.28,.34,-.19][variant];
  const y=clamp(v+along*(a+b*(v-.5))),scale=[1,1.17,.84,1.06][variant];
  const offset=along*([0,.16,-.12,.2][variant]+[0,.23,.31,-.37][variant]*(v-.5));
  return {u:clamp(.5+(u-.5)*scale+offset),v:y,gain:[1,1.12,.87,1.04][variant]};
}
