import {type HeightSampler} from '../TerrainSurface';
import {waterHeightAt,waterVertexLevel} from '../WaterSurface';

export function patchWaterVertex(x:number,z:number,seed:number,bed:HeightSampler,water:HeightSampler):number {
  return waterVertexLevel(x,z,bed,water,seed,false,-.02);
}
export function patchWaterHeight(x:number,z:number,seed:number,bed:HeightSampler,water:HeightSampler):number {
  return waterHeightAt(x,z,seed,bed,water,false,undefined,-.02);
}
