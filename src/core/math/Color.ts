import { clamp, clamp01 } from './Scalar.ts';

export type RGB = [number, number, number];

export interface HexOptions {
  allowShort?: boolean;
  allowBare?: boolean;
}

export function normalizeHex(value: unknown, options: HexOptions = {}): string | null {
  if (typeof value !== 'string') return null;
  const hex = value.startsWith('#') ? value.slice(1) : options.allowBare ? value : '';
  if (/^[0-9a-fA-F]{6}$/.test(hex)) return '#' + hex;
  if (options.allowShort !== false && /^[0-9a-fA-F]{3}$/.test(hex)) {
    return '#' + [...hex].map(c => c + c).join('');
  }
  return null;
}

export function validateHex(value: unknown, options: HexOptions = {}): value is string {
  return normalizeHex(value, options) !== null;
}

/** 拆解带 # 的六位色值 */
export function parseHexRgb(hex: string): RGB {
  const v = parseInt(hex.slice(1), 16);
  return [(v >> 16) & 255, (v >> 8) & 255, v & 255];
}

export function hexToRgb(hex: string | undefined, fallback: readonly [number, number, number] = [127, 127, 127]): RGB {
  const normalized = normalizeHex(String(hex ?? '').trim(), { allowShort: false, allowBare: true });
  return normalized === null ? [fallback[0], fallback[1], fallback[2]] : parseHexRgb(normalized);
}

export function clampColor(v: number): number {
  return clamp(v, 0, 255);
}

export function clampByte(v: number): number {
  return clampColor(Math.round(v));
}

export function deepen(c: RGB, depth: number): RGB {
  const k = clamp01(1 - depth);
  return [c[0] * k, c[1] * k, c[2] * k];
}

export function lighten(c: RGB, amount: number): RGB {
  const k = clamp01(amount);
  return [c[0] + (255 - c[0]) * k, c[1] + (255 - c[1]) * k, c[2] + (255 - c[2]) * k];
}

export function mix(a: RGB, b: RGB, t: number): RGB {
  const k = clamp01(t);
  return [a[0] + (b[0] - a[0]) * k, a[1] + (b[1] - a[1]) * k, a[2] + (b[2] - a[2]) * k];
}
