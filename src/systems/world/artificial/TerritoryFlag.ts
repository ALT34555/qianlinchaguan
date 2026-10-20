import { CHUNK_SIZE } from '../../../core/config';
import { buildDefaultFlag } from '../flags/DefaultFlag';
import { offsetFlagColliders } from '../flags/FlagPoleColliders';
import type { MeshData } from '../ChunkMesher';
import type { Territory } from './ArtificialState';

export function territoryFlag(t: Territory, height: number): MeshData & {colliders:Float32Array} {
  const flag = buildDefaultFlag(t.mainColor,t.trimColor);
  const { positions, colors, indices } = flag.geometry;
  for (let i = 0; i < positions.length; i += 3) {
    positions[i] += CHUNK_SIZE / 2 + .5;
    positions[i + 1] += height;
    positions[i + 2] += CHUNK_SIZE / 2 + .5;
  }
  return { positions, colors, indices,flagWind:flag.wind,
    colliders:offsetFlagColliders(flag.colliders,CHUNK_SIZE/2+.5,height,CHUNK_SIZE/2+.5) };
}
