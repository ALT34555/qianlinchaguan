const clamp=(v,a,b)=>Math.max(a,Math.min(b,v));

// 两季热量输送，共用山体屏障
export function thermalTransport(g,o,land,peaks,fraction,windE,windS){
  const {w,h,n}=g,offset=new Float32Array(n),base=new Float32Array(n),amplitude=new Float32Array(n);
  let air=new Float32Array(n),next=new Float32Array(n);
  const up=new Int32Array(n*4),weights=new Float32Array(n*4),passage=new Float32Array(n);
  const index=(x,y)=>{if(y<0){y=-y-1;x+=w/2;}if(y>=h){y=2*h-y-1;x+=w/2;}return y*w+(x%w+w)%w;};
  for(let y=0;y<h;y++){
    const lat=Math.PI/2-(y+.5)*Math.PI/h;
    for(let x=0;x<w;x++){
      const i=y*w+x;base[i]=o.poleTemperature+(o.equatorTemperature-o.poleTemperature)*Math.cos(lat)**1.4;
      amplitude[i]=(5+12*fraction[i])*Math.abs(Math.sin(lat))**.7;
    }
  }
  for(const phase of [-1,1]){
    for(let y=0;y<h;y++)for(let x=0;x<w;x++){
      const i=y*w+x,hemisphere=y<h/2?1:-1,winter=phase*hemisphere<0;
      const dx=clamp(windE[i]*(winter?.3:.8),-1.3,1.3),dy=clamp(windS[i]*.3+hemisphere*(winter?.95:-.35),-1.3,1.3);
      const sx=x-dx,sy=y-dy,ix=Math.floor(sx),iy=Math.floor(sy),tx=sx-ix,ty=sy-iy,k=i*4;
      const ids=[index(ix,iy),index(ix+1,iy),index(ix,iy+1),index(ix+1,iy+1)];
      up.set(ids,k);weights.set([(1-tx)*(1-ty),tx*(1-ty),(1-tx)*ty,tx*ty],k);
      let barrier=0;
      for(let d=0;d<4;d++)barrier+=Math.max(0,Math.max(peaks[i],peaks[ids[d]])-Math.min(land[i],land[ids[d]]))*weights[k+d];
      passage[i]=Math.exp(-barrier/(winter?650:1300));
      air[i]=base[i]+phase*hemisphere*amplitude[i];
    }
    for(let pass=0;pass<16;pass++){
      for(let y=0;y<h;y++)for(let x=0;x<w;x++){
        const i=y*w+x,k=i*4,hemisphere=y<h/2?1:-1;
        const local=base[i]+phase*hemisphere*amplitude[i];
        let source=0;for(let d=0;d<4;d++)source+=air[up[k+d]]*weights[k+d];
        const carry=.7*fraction[i]*passage[i];
        next[i]=local+(source-local)*carry;
      }
      [air,next]=[next,air];
    }
    for(let i=0;i<n;i++)offset[i]+=(air[i]-base[i])*.5;
  }
  for(let i=0;i<n;i++)offset[i]=clamp(offset[i],-6,6);
  return offset;
}
