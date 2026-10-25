// CSV 随包内联，不再运行时取文件
import tokensRaw from '../../../content/data/ui/tokens.csv?raw';
import decorRaw from '../../../content/data/ui/decor.csv?raw';
import paletteRaw from '../../../content/data/ui/palette.csv?raw';

export type CsvRow = Record<string, string>;

const SOURCES: Record<string, string> = { tokens: tokensRaw, decor: decorRaw, palette: paletteRaw };

export function parseCSV(text: string): CsvRow[] {
  const source = text.replace(/^\uFEFF/, '');
  const rows: string[][] = [];
  let row: string[] = [], cell = '', quoted = false;
  for (let i = 0; i < source.length; i++) {
    const ch = source[i];
    if (quoted) {
      if (ch === '"') {
        if (source[i + 1] === '"') { cell += '"'; i++; } else quoted = false;
      } else cell += ch;
    } else if (ch === '"') quoted = true;
    else if (ch === ',') { row.push(cell); cell = ''; }
    else if (ch === '\n') { row.push(cell); rows.push(row); row = []; cell = ''; }
    else if (ch !== '\r') cell += ch;
  }
  if (cell || row.length) { row.push(cell); rows.push(row); }
  const head = rows.shift()!.map(h => h.trim());
  return rows
    .filter(r => r.some(c => c.trim() !== ''))
    .map(r => Object.fromEntries(head.map((h, i) => [h, (r[i] ?? '').trim()])));
}

export function loadCSV(name: keyof typeof SOURCES): CsvRow[] {
  const text = SOURCES[name];
  if (text === undefined) throw new Error(`未知 CSV：${name}`);
  return parseCSV(text);
}
