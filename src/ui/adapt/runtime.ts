// 1018 视觉层运行时：令牌、装饰、交互与配色状态
import { loadCSV } from './csv';
import { initDecor } from './decor';
import { initTheme, state, resolvedTheme, setThemeMode, toggleTheme, update, reset as resetTheme } from './theme';
import { initUI } from './ui';
import { nearestColor, siblingColors, normalizeHex, type PaletteEntry } from './color';

export { paintAll } from './decor';
export { toast, openDialog, openDrawer, showLoading, flowerRingSVG, setLoaderProgress, loaderSVG } from './ui';
export { state as themeState, resolvedTheme, setThemeMode, toggleTheme, update as updateTheme, resetTheme };
export type { PaletteEntry };

interface TokenRow { group: string; token: string; light: string; dark: string; note: string }

let tokens: TokenRow[] = [];
let palette: PaletteEntry[] = [];

export async function initVisuals(): Promise<void> {
  try {
    tokens = loadCSV('tokens') as unknown as TokenRow[];
  } catch (error) {
    console.warn('[ui] tokens.csv 未载入', error);
  }
  try {
    palette = loadCSV('palette')
      .map(row => ({ name: row['中文名'] ?? row.name ?? '', pinyin: row['英文/拼音'] ?? row.pinyin ?? '', hex: row.hex ?? '' }))
      .filter(item => item.hex);
  } catch (error) {
    console.warn('[ui] palette.csv 未载入', error);
  }
  await initTheme(tokens.map(({ token, light, dark }) => ({ token, light, dark })));
  await initDecor();
  initUI();
}

export function tokenTable(): TokenRow[] { return tokens; }
export function traditionalPalette(): PaletteEntry[] { return palette; }

/** 传统色名，找不到用 HEX */
export function colorName(hex: string): string {
  const normalized = normalizeHex(hex);
  if (!normalized || !palette.length) return normalized ?? hex;
  return nearestColor(palette, normalized)?.name ?? normalized;
}

/** 同色系预设，供取色器下方色块使用 */
export function colorPresets(hex: string, count = 6): PaletteEntry[] {
  const normalized = normalizeHex(hex);
  if (!normalized || !palette.length) return [];
  return siblingColors(palette, normalized, count);
}
