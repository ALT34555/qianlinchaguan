// 装饰注入：路径取自 decor.csv
import { loadCSV } from './csv';

const NS = 'http://www.w3.org/2000/svg';

interface DecorRow {
  id: string;
  viewBox?: string;
  strokeWidth?: string;
  fill?: string;
  repeat?: string;
  paths?: string;
}

let catalog = new Map<string, DecorRow>();

export async function initDecor(): Promise<void> {
  try {
    catalog = new Map(loadCSV('decor').map(row => [row.id, row as unknown as DecorRow]));
  } catch (error) {
    console.warn(error);
  }
  paintAll();
}

export function paintAll(scope: ParentNode = document): void {
  scope.querySelectorAll('[data-decor]').forEach(el => paint(el as HTMLElement));
}

function makePath(d: string, width?: string): SVGPathElement {
  const path = document.createElementNS(NS, 'path');
  path.setAttribute('d', d);
  if (width) path.setAttribute('stroke-width', width);
  return path;
}

function paint(host: HTMLElement): void {
  const item = catalog.get(host.dataset.decor || '');
  if (!item || host.dataset.painted === '1') return;
  const svg = document.createElementNS(NS, 'svg');
  svg.setAttribute('viewBox', item.viewBox || '0 0 24 24');
  svg.setAttribute('aria-hidden', 'true');
  svg.setAttribute('fill', item.fill || 'none');
  svg.classList.add('decor');
  const paths = (item.paths || '').split('|').filter(Boolean);
  const repeat = Number(item.repeat) || 0;
  const width = item.strokeWidth || '1.2';
  if (repeat > 1) {
    for (let i = 0; i < repeat; i++) {
      const g = document.createElementNS(NS, 'g');
      g.setAttribute('transform', `rotate(${((360 / repeat) * i).toFixed(2)} 12 12)`);
      paths.forEach(d => g.appendChild(makePath(d, width)));
      svg.appendChild(g);
    }
  } else {
    paths.forEach(d => svg.appendChild(makePath(d, width)));
  }
  host.classList.add('decor-host');
  host.replaceChildren(svg);
  host.dataset.painted = '1';
}
