// 宏观河源只检查相邻格
export function isRiverHead(w,cell){
  if(!w.riverWidth[cell]||w.receiver[cell]<0)return false;
  const col=cell%w.columns,row=Math.floor(cell/w.columns);
  for(let dz=-1;dz<=1;dz++)for(let dx=-1;dx<=1;dx++){
    let x=col+dx,y=row+dz;
    if(y<0){y=-y-1;x+=w.columns/2;}
    if(y>=w.rows){y=w.rows*2-y-1;x+=w.columns/2;}
    const i=y*w.columns+(x%w.columns+w.columns)%w.columns;
    if(i!==cell&&w.riverWidth[i]&&w.receiver[i]===cell)return false;
  }
  return true;
}
