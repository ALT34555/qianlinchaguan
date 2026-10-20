export interface ScalableGeometry {
  positions: Float32Array;
  radius: number;
  height: number;
  bottom?: number;
  colliders?:Float32Array;
}

export function scaleGeometry(geo: ScalableGeometry, scale: number): void {
  if (scale === 1) return;
  for (let i = 0; i < geo.positions.length; i++) geo.positions[i] *= scale;
  geo.radius *= scale;
  geo.height *= scale;
  if (geo.bottom !== undefined) geo.bottom *= scale;
  if(geo.colliders)for(let i=0;i<geo.colliders.length;i++)if(i%7!==6)geo.colliders[i]*=scale;
}
