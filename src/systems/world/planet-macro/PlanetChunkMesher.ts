import {buildChunkMeshes,type ChunkMeshes} from '../ChunkMesher';
import type {ChunkGenResult} from '../WorldContract';

export function buildPlanetChunkMeshes(chunk:ChunkGenResult,seed:number):ChunkMeshes {
  return buildChunkMeshes(chunk.cx,chunk.cz,chunk.heights,chunk.surfaces,seed,
    chunk.waterLevels,chunk.surfaceColors,chunk.waterField,undefined,-.02,false);
}
