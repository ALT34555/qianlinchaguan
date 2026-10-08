import { smooth, snowLine, SNOW_MARGIN } from './TerrainLayers';

/** 来水与急流延后冻结，严寒仍能封冻。 */
export function riverFreezeAmount(level: number, temperature: number, discharge: number, width: number, grade: number): number {
  const large = smooth((discharge - 500) / 7500) * smooth((width - 16) / 48);
  const rapid = smooth(Math.max(0, grade) / .5);
  const resistance = large * 128 + rapid * 64;
  return smooth((level - snowLine(temperature) + SNOW_MARGIN - resistance) / (SNOW_MARGIN * 2));
}

/** 浅岸先结冰，深槽最后封冻。 */
export function riverIceCover(frozen: number, channel: number): number {
  return smooth((frozen - (.08 + .72 * channel)) / .2);
}

/** 床面保护在两倍半径内连续退场。 */
export function riverBedProtection(distance: number, radius: number): number {
  return Math.exp(-((distance / radius) ** 2)) * (1 - smooth((distance - radius) / radius));
}
