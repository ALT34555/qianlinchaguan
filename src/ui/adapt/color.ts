// 颜色换算：HEX/RGB/LAB/HSL
export function hex2rgb(hex: string): [number, number, number] {
  let h = hex.replace('#', '').trim();
  if (h.length === 3) h = h.split('').map(c => c + c).join('');
  const n = parseInt(h, 16);
  return [(n >> 16) & 255, (n >> 8) & 255, n & 255];
}

export function rgb2hex([r, g, b]: [number, number, number]): string {
  return '#' + [r, g, b].map(v => Math.max(0, Math.min(255, Math.round(v))).toString(16).padStart(2, '0')).join('').toUpperCase();
}

export function isHex(value: string): boolean {
  return /^#?[0-9a-f]{6}$/i.test(value.trim());
}

export function normalizeHex(value: string): string | null {
  return isHex(value) ? rgb2hex(hex2rgb(value)) : null;
}

const lin = (c: number): number => (c /= 255) <= 0.04045 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4;

export function relativeLuminance(hex: string): number {
  const [r, g, b] = hex2rgb(hex);
  return 0.2126 * lin(r) + 0.7152 * lin(g) + 0.0722 * lin(b);
}

export function contrastRatio(a: string, b: string): number {
  const x = relativeLuminance(a), y = relativeLuminance(b);
  return (Math.max(x, y) + 0.05) / (Math.min(x, y) + 0.05);
}

export function mixHex(a: string, b: string, t: number): string {
  const p = hex2rgb(a), q = hex2rgb(b);
  return rgb2hex(p.map((v, i) => v + (q[i] - v) * t) as [number, number, number]);
}

export function rgb2lab([r, g, b]: [number, number, number]): [number, number, number] {
  const R = lin(r), G = lin(g), B = lin(b);
  const f = (t: number) => (t > 0.008856 ? Math.cbrt(t) : 7.787 * t + 16 / 116);
  const x = f((R * 0.4124 + G * 0.3576 + B * 0.1805) / 0.95047);
  const y = f(R * 0.2126 + G * 0.7152 + B * 0.0722);
  const z = f((R * 0.0193 + G * 0.1192 + B * 0.9505) / 1.08883);
  return [116 * y - 16, 500 * (x - y), 200 * (y - z)];
}

export function deltaE(a: [number, number, number], b: [number, number, number]): number {
  const p = rgb2lab(a), q = rgb2lab(b);
  return Math.hypot(p[0] - q[0], p[1] - q[1], p[2] - q[2]);
}

export function hex2hsl(hex: string): [number, number, number] {
  const [r, g, b] = hex2rgb(hex).map(v => v / 255) as [number, number, number];
  const max = Math.max(r, g, b), min = Math.min(r, g, b);
  const l = (max + min) / 2;
  const d = max - min;
  let h = 0, s = 0;
  if (d) {
    s = l > 0.5 ? d / (2 - max - min) : d / (max + min);
    if (max === r) h = ((g - b) / d + (g < b ? 6 : 0)) / 6;
    else if (max === g) h = ((b - r) / d + 2) / 6;
    else h = ((r - g) / d + 4) / 6;
  }
  return [h * 360, s * 100, l * 100];
}

export interface PaletteEntry { name: string; hex: string; pinyin?: string }

export function nearestColor(palette: PaletteEntry[], hex: string): PaletteEntry | null {
  const rgb = hex2rgb(hex);
  let best: PaletteEntry | null = null, bestD = Infinity;
  for (const item of palette) {
    const d = deltaE(rgb, hex2rgb(item.hex));
    if (d < bestD) { bestD = d; best = item; }
  }
  return best;
}

function oklabToRgb(L: number, a: number, b: number): [number, number, number] {
  const l_ = L + 0.3963377774 * a + 0.2158037573 * b;
  const m_ = L - 0.1055613458 * a - 0.0638541728 * b;
  const s_ = L - 0.0894841775 * a - 1.2914855480 * b;
  const l = l_ ** 3, m = m_ ** 3, s = s_ ** 3;
  const r = 4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s;
  const g = -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s;
  const bb = -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s;
  const enc = (x: number): number => {
    const v = Math.max(0, Math.min(1, x));
    return (v <= 0.0031308 ? v * 12.92 : 1.055 * v ** (1 / 2.4) - 0.055) * 255;
  };
  return [enc(r), enc(g), enc(bb)];
}

export function parseCssColor(value: string): { hex: string; alpha: number } | null {
  if (!value) return null;
  let match: RegExpMatchArray | null;
  if ((match = value.match(/^rgba?\(([^)]+)\)$/))) {
    const parts = match[1].split(/[\s,/]+/).filter(Boolean).map(Number);
    const alpha = parts[3] === undefined ? 1 : parts[3];
    return { hex: rgb2hex(parts as [number, number, number]), alpha };
  }
  if ((match = value.match(/^oklab\(([^)]+)\)$/))) {
    const parts = match[1].split(/[\s/]+/).filter(Boolean).map(Number);
    const alpha = parts[3] === undefined ? 1 : parts[3];
    return { hex: rgb2hex(oklabToRgb(parts[0], parts[1], parts[2])), alpha };
  }
  if ((match = value.match(/^color\(srgb\s+([^)]+)\)$/))) {
    const parts = match[1].split(/[\s/]+/).filter(Boolean).map(Number);
    const alpha = parts[3] === undefined ? 1 : parts[3];
    return { hex: rgb2hex([parts[0] * 255, parts[1] * 255, parts[2] * 255]), alpha };
  }
  return null;
}

export function siblingColors(palette: PaletteEntry[], hex: string, count = 6): PaletteEntry[] {
  const [hue, sat] = hex2hsl(hex);
  const pick = (spread: number) => palette.filter(item => {
    const [h, s] = hex2hsl(item.hex);
    const diff = Math.min(Math.abs(h - hue), 360 - Math.abs(h - hue));
    return diff <= spread && s >= Math.min(10, sat * 0.4);
  });
  let pool = pick(18);
  if (pool.length < count) pool = pick(34);
  if (pool.length < count) pool = palette;
  const sorted = [...pool].sort((a, b) => hex2hsl(a.hex)[2] - hex2hsl(b.hex)[2]);
  const out: PaletteEntry[] = [];
  for (let i = 0; i < count; i++) {
    out.push(sorted[Math.round((i * (sorted.length - 1)) / (count - 1))]);
  }
  return [...new Map(out.map(c => [c.hex, c])).values()];
}
