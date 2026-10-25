export const TERRAIN_GRID = 2;
export const TERRAIN_ORIGIN = .5;
export function terrainVertexHeight(x, z, height) {
  return Math.fround(height(x - TERRAIN_ORIGIN, z - TERRAIN_ORIGIN));
}
function hash2(x,z,seed) {
  let h=Math.imul(x|0,0x27d4eb2d)^Math.imul(z|0,0x165667b1)^Math.imul(seed|0,0x9e3779b1);
  h=Math.imul(h^(h>>>15),0x85ebca6b);h=Math.imul(h^(h>>>13),0xc2b2ae35);h^=h>>>16;
  return (h>>>0)/4294967296;
}
export function terrainDiagonal(x, z, seed) {
  const periodic=v=>((Math.floor((v-TERRAIN_ORIGIN)/TERRAIN_GRID)%32)+32)%32;
  return hash2(periodic(x),periodic(z),seed)>.5;
}

export function triangleHeight(u, v, a, b, c, d, flip) {
  if (flip) return u + v <= 1 ? a + (d - a) * u + (b - a) * v
    : c + (b - c) * (1 - u) + (d - c) * (1 - v);
  return v >= u ? a + (c - b) * u + (b - a) * v : a + (d - a) * u + (c - d) * v;
}

export const TERRAIN_DETAIL_DROP = .1;
function edgeDetail(a,b,mid) {
  return mid<(a+b)/2-TERRAIN_DETAIL_DROP||mid<Math.min(a,b);
}
export function terrainCell(x,z,seed,height) {
  const a=terrainVertexHeight(x,z,height),b=terrainVertexHeight(x,z+2,height),c=terrainVertexHeight(x+2,z+2,height),d=terrainVertexHeight(x+2,z,height);
  const flip=terrainDiagonal(x,z,seed),edges=[
    terrainVertexHeight(x+1,z,height),terrainVertexHeight(x,z+1,height),
    terrainVertexHeight(x+2,z+1,height),terrainVertexHeight(x+1,z+2,height)];
  const coarse=[(a+d)/2,(a+b)/2,(d+c)/2,(b+c)/2];
  const center=terrainVertexHeight(x+1,z+1,height),coarseCenter=triangleHeight(.5,.5,a,b,c,d,flip);
  const pairs=[[a,d],[a,b],[d,c],[b,c]],detail=edges.map((h,i)=>edgeDetail(...pairs[i],h));
  const refined=detail.some(Boolean)||center<coarseCenter-TERRAIN_DETAIL_DROP||center<Math.min(a,b,c,d);
  const e=edges.map((h,i)=>detail[i]?h:coarse[i]);
  return {refined,flip,heights:[a,e[0],d,e[1],refined?center:coarseCenter,e[2],b,e[3],c]};
}
function edgeHeight(t,a,b,mid) {
  if(!edgeDetail(a,b,mid))return a+(b-a)*t;
  return t<=.5?a+(mid-a)*t*2:mid+(b-mid)*(t-.5)*2;
}
export function terrainHeightAt(x,z,seed,height) {
  const ix=Math.floor((x-TERRAIN_ORIGIN)/TERRAIN_GRID)*TERRAIN_GRID+TERRAIN_ORIGIN;
  const iz=Math.floor((z-TERRAIN_ORIGIN)/TERRAIN_GRID)*TERRAIN_GRID+TERRAIN_ORIGIN;
  const u=(x-ix)/TERRAIN_GRID,v=(z-iz)/TERRAIN_GRID,a=terrainVertexHeight(ix,iz,height);
  if(u===0&&v===0)return a;
  if(u===0)return edgeHeight(v,a,terrainVertexHeight(ix,iz+2,height),terrainVertexHeight(ix,iz+1,height));
  if(v===0)return edgeHeight(u,a,terrainVertexHeight(ix+2,iz,height),terrainVertexHeight(ix+1,iz,height));
  const cell=terrainCell(ix,iz,seed,height),h=cell.heights;
  if(!cell.refined)return triangleHeight(u,v,h[0],h[6],h[8],h[2],cell.flip);
  const sx=Math.min(1,Math.floor(u*2)),sz=Math.min(1,Math.floor(v*2)),i=sz*3+sx;
  return triangleHeight(u*2-sx,v*2-sz,h[i],h[i+3],h[i+4],h[i+1],cell.flip);
}
