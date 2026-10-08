import type { ArtificialWorld } from '../systems/world/artificial/ArtificialWorld';
import { claimKey } from '../systems/world/artificial/ArtificialState';

export function drawTerritories(ctx: CanvasRenderingContext2D, world: ArtificialWorld,
  project: (cx: number, cz: number) => [number, number] | null, width: number, height: number, labels: boolean): void {
  ctx.save(); ctx.lineWidth = 2.5; ctx.lineJoin = 'round';
  for (const c of world.claims.values()) {
    const t = world.territories.get(c.faction)!;
    const corners = [[c.cx, c.cz], [c.cx + 1, c.cz], [c.cx + 1, c.cz + 1], [c.cx, c.cz + 1]];
    const points = corners.map(([x, z]) => project(x, z));
    if (points.every(p => !p || p[0] < -10 || p[0] > width + 10 || p[1] < -10 || p[1] > height + 10)) continue;
    ctx.strokeStyle = t.mainColor;
    const neighbors = [[0, -1], [1, 0], [0, 1], [-1, 0]];
    for (let i = 0; i < 4; i++) {
      const [dx, dz] = neighbors[i], a = points[i], b = points[(i + 1) % 4];
      const other = world.claims.get(claimKey(world.generator.wrap(c.cx + dx), c.cz + dz));
      if (!a || !b || Math.abs(a[0] - b[0]) > width / 2 || other?.faction === c.faction) continue;
      ctx.beginPath(); ctx.moveTo(...a); ctx.lineTo(...b); ctx.stroke();
    }
    if (c.step === 0) {
      const p = project(c.cx + .5, c.cz + .5);
      if (p) {
        ctx.lineWidth = 2; ctx.strokeStyle = '#211c18'; ctx.fillStyle = t.mainColor;
        ctx.beginPath(); ctx.moveTo(p[0] - 5, p[1] + 7); ctx.lineTo(p[0] - 5, p[1] - 13); ctx.stroke();
        ctx.beginPath(); ctx.moveTo(p[0] - 4, p[1] - 13); ctx.lineTo(p[0] + 10, p[1] - 10);
        ctx.lineTo(p[0] + 6, p[1] - 6); ctx.lineTo(p[0] + 10, p[1] - 2); ctx.lineTo(p[0] - 4, p[1] - 4);
        ctx.closePath(); ctx.fill(); ctx.stroke(); ctx.lineWidth = 2.5;
        if (!labels) continue;
        ctx.font = 'bold 12px sans-serif'; ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
        ctx.lineWidth = 3; ctx.strokeStyle = '#211c18'; ctx.fillStyle = t.mainColor;
        const label = `Lv.${t.level}`;
        ctx.strokeText(label, p[0], p[1] + 17); ctx.fillText(label, p[0], p[1] + 17); ctx.lineWidth = 2.5;
      }
    }
  }
  ctx.restore();
}
