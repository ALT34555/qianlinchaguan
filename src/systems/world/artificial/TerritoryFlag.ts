import { CHUNK_SIZE } from '../../../core/config';
import { hashString } from '../../../core/math/Random';
import { buildFlagAssembly } from '../flags/Flags';
import type { MeshData } from '../ChunkMesher';
import type { Territory } from './ArtificialState';

export function territoryFlag(t: Territory, height: number): MeshData {
  const flag = buildFlagAssembly({ flag: 'flag.rect.chizhi', pole: 'pole.ground.gaogan', mount: 'masthead',
    charge: { kind: 'none' }, trimWidth: .1, palette: { field: t.mainColor, trim: t.trimColor },
    shapeSeed: hashString(t.id), poleOptions: { height: 7 }, fly: 3, hoist: 2 });
  const { positions, colors, indices } = flag.geometry;
  for (let i = 0; i < positions.length; i += 3) {
    positions[i] += CHUNK_SIZE / 2 + .5;
    positions[i + 1] += height;
    positions[i + 2] += CHUNK_SIZE / 2 + .5;
  }
  return { positions, colors, indices };
}
