import { COLLIDER_STRIDE, HARD_ROCK, HARD_TRUNK, SOFT_SHRUB, PLANT_TRIGGER } from './DecorationCollisionShapes';

interface Volume { data: Float32Array; offset: number; cells: string[]; mark: number }
export class DecorationCollisions {
  private readonly cells = new Map<string, Set<Volume>>();
  private readonly chunks = new Map<string, Volume[]>();
  private stamp = 0;

  set(cx: number, cz: number, data: Float32Array): void {
    this.remove(cx, cz);
    const volumes: Volume[] = [];
    for (let i = 0; i + COLLIDER_STRIDE <= data.length; i += COLLIDER_STRIDE) {
      const volume: Volume = { data, offset: i, cells: [], mark: 0 };
      for (let z = Math.floor((data[i + 2] - data[i + 4]) / 4); z <= Math.floor((data[i + 2] + data[i + 4]) / 4); z++) {
        for (let x = Math.floor((data[i] - data[i + 3]) / 4); x <= Math.floor((data[i] + data[i + 3]) / 4); x++) {
          const key = `${x},${z}`, cell = this.cells.get(key) ?? new Set<Volume>();
          cell.add(volume); this.cells.set(key, cell); volume.cells.push(key);
        }
      }
      volumes.push(volume);
    }
    if (volumes.length) this.chunks.set(`${cx},${cz}`, volumes);
  }

  remove(cx: number, cz: number): void {
    const key = `${cx},${cz}`;
    for (const volume of this.chunks.get(key) ?? []) for (const name of volume.cells) {
      const cell = this.cells.get(name)!; cell.delete(volume); if (!cell.size) this.cells.delete(name);
    }
    this.chunks.delete(key);
  }

  private visit(x: number, z: number, half: number, test: (data: Float32Array, offset: number) => boolean): boolean {
    const stamp = ++this.stamp;
    for (let gz = Math.floor((z - half) / 4); gz <= Math.floor((z + half) / 4); gz++) for (let gx = Math.floor((x - half) / 4); gx <= Math.floor((x + half) / 4); gx++) {
      for (const volume of this.cells.get(`${gx},${gz}`) ?? []) {
        if (volume.mark === stamp) continue;
        volume.mark = stamp;
        if (test(volume.data, volume.offset)) return true;
      }
    }
    return false;
  }

  private overlaps(d: Float32Array, i: number, x: number, z: number, half: number): boolean {
    const dx = Math.max(0, Math.abs(x - d[i]) - half), dz = Math.max(0, Math.abs(z - d[i + 2]) - half);
    return d[i + 6] === HARD_ROCK ? dx < d[i + 3] && dz < d[i + 4] : dx * dx + dz * dz < d[i + 3] ** 2;
  }

  blocked(x: number, y: number, z: number, height: number, half: number): boolean {
    return this.visit(x, z, half, (d, i) => (d[i + 6] === HARD_TRUNK || d[i + 6] === HARD_ROCK) && y < d[i + 5] - 1e-5 && y + height > d[i + 1] + 1e-5 && this.overlaps(d, i, x, z, half));
  }

  speedFactor(x: number, y: number, z: number, height: number, half: number): number {
    return this.visit(x, z, half, (d, i) => d[i + 6] === SOFT_SHRUB && y < d[i + 5] && y + height > d[i + 1] && this.overlaps(d, i, x, z, half)) ? .65 : 1;
  }

  landingHeight(x: number, z: number, from: number, to: number, half: number): number {
    let top = -Infinity;
    this.visit(x, z, half, (d, i) => {
      if ((d[i + 6] === HARD_TRUNK || d[i + 6] === HARD_ROCK) && d[i + 5] <= from + 1e-5 && d[i + 5] > to && this.overlaps(d, i, x, z, half)) top = Math.max(top, d[i + 5]);
      return false;
    });
    return top;
  }
  triggerCount(x: number, y: number, z: number, height: number, half: number): number {
    let count = 0;
    this.visit(x, z, half, (d, i) => {
      if (d[i + 6] === PLANT_TRIGGER && y < d[i + 5] && y + height > d[i + 1] && this.overlaps(d, i, x, z, half)) count++;
      return false;
    });
    return count;
  }

  clear(): void { this.cells.clear(); this.chunks.clear(); }
}
