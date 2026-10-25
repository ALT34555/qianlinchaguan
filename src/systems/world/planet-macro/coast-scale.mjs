export function coastHeightScale(world){
  return world.heightScale??(world.heightCap??3800)/3800;
}
export function islandReliefPeak(world,island){
  const g=coastHeightScale(world),cap=world.heightCap??3800*g,original=Math.max(1,world.height[island.cell]);
  const seedHeight=Math.max(0,Math.min(1,original/(160*g)));
  const activity=Math.pow(Math.max(0,Math.min(9,world.options.activity??5))/9,1.504);
  const uplift=Math.max(0,Math.min(1,world.uplift?.[island.cell]??0));
  const fraction=.01+.035*seedHeight+activity*(.04+.16*uplift)*(.35+.65*seedHeight);
  return Math.min(cap*.3,Math.max(original,cap*fraction));
}
