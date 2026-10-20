export function padTilePixels(pixels,side){
  const size=side+2,padded=new Uint8ClampedArray(size*size*4);
  for(let y=0;y<side;y++){
    const row=(y+1)*size*4;padded.set(pixels.subarray(y*side*4,(y+1)*side*4),row+4);
    padded.set(pixels.subarray(y*side*4,y*side*4+4),row);
    padded.set(pixels.subarray((y+1)*side*4-4,(y+1)*side*4),row+(size-1)*4);
  }
  padded.set(padded.subarray(size*4,size*8),0);
  padded.set(padded.subarray(side*size*4,(side+1)*size*4),(size-1)*size*4);
  return padded;
}
export function expandTriangle(points,padding=.75){
  const [a,b,c]=points,area=(b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x),sign=Math.sign(area);
  if(Math.abs(area)<1e-8)return points;
  return points.map((p,i)=>{
    const previous=points[(i+2)%3],next=points[(i+1)%3],dx=p.x-previous.x,dy=p.y-previous.y,ex=next.x-p.x,ey=next.y-p.y;
    const length=Math.hypot(dx,dy),other=Math.hypot(ex,ey),nx=sign*dy/length,ny=-sign*dx/length,mx=sign*ey/other,my=-sign*ex/other;
    const gain=padding/Math.max(.05,1+nx*mx+ny*my),ox=(nx+mx)*gain,oy=(ny+my)*gain,limit=Math.min(1,padding*4/Math.max(1e-9,Math.hypot(ox,oy)));
    return {x:p.x+ox*limit,y:p.y+oy*limit};
  });
}
