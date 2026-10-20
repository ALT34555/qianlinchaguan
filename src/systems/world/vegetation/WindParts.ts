import type { RGB } from '../../../core/math/Color';

export type PlantWindPart = 1 | 2 | 3;
const parts = new WeakMap<RGB, PlantWindPart>();

export function windColor(color: RGB, part: PlantWindPart): RGB {
  parts.set(color, part);
  return color;
}

export function colorWindPart(color: RGB): PlantWindPart {
  return parts.get(color) ?? 3;
}

export function paletteWindPart(key: string | undefined): PlantWindPart {
  if (key && /bark/.test(key)) return 2;
  if (key && /Culm|Stem|^stem$|^bambooJoint$/.test(key)) return 3;
  if (key && /foliage|canopy|conifer|larch|jungle|Frond|grassDry|waterweed|^bamboo$|^tea$|^mulberry$|crop|riceGreen|sorghum|witheredNeedle|^withered/.test(key)) return 1;
  return 3;
}
