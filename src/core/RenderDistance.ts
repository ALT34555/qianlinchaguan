export const MIN_RENDER_DISTANCE = 2;
export const MAX_RENDER_DISTANCE = 16;

export function renderChunkOffsets(radius: number): [number, number][] {
  const offsets: [number, number][] = [];
  for (let dz = -radius; dz <= radius; dz++) {
    for (let dx = -radius; dx <= radius; dx++) {
      if (dx * dx + dz * dz <= (radius + .5) ** 2) offsets.push([dx, dz]);
    }
  }
  return offsets.sort((a, b) => a[0] ** 2 + a[1] ** 2 - b[0] ** 2 - b[1] ** 2);
}
