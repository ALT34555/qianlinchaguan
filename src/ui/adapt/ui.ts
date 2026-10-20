// 通用交互：水波、开关、浮层、提示条、加载花
import { paintAll } from './decor';
import { initButtons } from './buttons';

const PETAL_D = 'M24 24C18.8 20.2 16.8 13.6 24 4.6C31.2 13.6 29.2 20.2 24 24Z';
const PETAL_ROT = [0, 72, 144, 216, 288];

const RING_TIP = 54;
const RING_VALLEY = 21;

const polar = (r: number, deg: number): string => {
  const rad = (deg - 90) * Math.PI / 180;
  return `${(60 + r * Math.cos(rad)).toFixed(2)} ${(60 + r * Math.sin(rad)).toFixed(2)}`;
};

function flowerRingPath(): string {
  let d = `M${polar(RING_VALLEY, 36)}`;
  for (let k = 0; k < 5; k++) {
    const a0 = k * 72 + 36, am = k * 72 + 72, a1 = k * 72 + 108;
    d += `C${polar(44, a0 + 11)} ${polar(51, am - 14)} ${polar(RING_TIP, am)}`;
    d += `C${polar(51, am + 14)} ${polar(44, a1 - 11)} ${polar(RING_VALLEY, a1)}`;
  }
  return `${d}Z`;
}

const RING_PATH = flowerRingPath();

export function flowerRingSVG(size = 128): string {
  return `<svg class="ring-svg" width="${size}" height="${size}" viewBox="0 0 120 120" aria-hidden="true">
      <path class="ring-track" d="${RING_PATH}"/>
      <path class="ring-fill" d="${RING_PATH}" pathLength="100"/>
      <circle class="ring-core" cx="60" cy="60" r="4"/>
    </svg>`;
}

export function loaderSVG(size = 96): SVGSVGElement {
  const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  svg.setAttribute('viewBox', '0 0 48 48');
  svg.setAttribute('width', String(size));
  svg.setAttribute('height', String(size));
  svg.setAttribute('aria-hidden', 'true');
  svg.classList.add('pl-svg');
  PETAL_ROT.forEach(rot => {
    const g = document.createElementNS('http://www.w3.org/2000/svg', 'g');
    g.setAttribute('transform', `rotate(${rot} 24 24)`);
    const path = document.createElementNS('http://www.w3.org/2000/svg', 'path');
    path.setAttribute('d', PETAL_D);
    path.classList.add('pl-petal');
    g.appendChild(path);
    svg.appendChild(g);
  });
  const core = document.createElementNS('http://www.w3.org/2000/svg', 'circle');
  core.setAttribute('cx', '24');
  core.setAttribute('cy', '24');
  core.setAttribute('r', '3.4');
  core.classList.add('pl-core');
  svg.appendChild(core);
  return svg;
}

export function setLoaderProgress(svg: SVGSVGElement, p: number): void {
  const pct = Math.max(0, Math.min(1, p)) * 100;
  svg.querySelectorAll<HTMLElement>('.pl-petal').forEach((petal, i) => {
    petal.style.setProperty('--s', Math.max(0, Math.min(1, (pct - i * 20) / 20)).toFixed(3));
  });
  svg.querySelector<HTMLElement>('.pl-core')!.style.setProperty('--s', Math.max(0, Math.min(1, (pct - 80) / 20)).toFixed(3));
}

export function showLoading({ duration = 2400, title = '山川正在生成' } = {}): Promise<void> {
  const root = document.getElementById('overlayRoot');
  if (!root) return Promise.resolve();
  const veil = document.createElement('div');
  veil.className = 'load-veil';
  veil.innerHTML = `
    <div class="load-card">
      <span class="eyebrow">LOADING</span>
      <div class="pl-slot"></div>
      <h3 class="load-title"></h3>
      <div class="bar load-bar"><i></i></div>
      <span class="load-pct mono">0%</span>
    </div>`;
  veil.querySelector('.load-title')!.textContent = title;
  const svg = loaderSVG();
  veil.querySelector('.pl-slot')!.appendChild(svg);
  root.appendChild(veil);

  const bar = veil.querySelector<HTMLElement>('.load-bar i')!;
  const pctText = veil.querySelector<HTMLElement>('.load-pct')!;
  const start = performance.now();

  return new Promise(resolve => {
    const tick = (now: number) => {
      const t = Math.min(1, (now - start) / duration);
      const eased = t < 0.5 ? 2 * t * t : 1 - (-2 * t + 2) ** 2 / 2;
      setLoaderProgress(svg, eased);
      bar.style.setProperty('--p', `${(eased * 100).toFixed(1)}%`);
      pctText.textContent = `${Math.round(eased * 100)}%`;
      if (t < 1) {
        requestAnimationFrame(tick);
      } else {
        veil.classList.add('is-done');
        setTimeout(() => { veil.remove(); resolve(); }, 460);
      }
    };
    requestAnimationFrame(tick);
  });
}

export function initUI(): void {
  initButtons();
  document.addEventListener('pointerdown', event => {
    const btn = (event.target as HTMLElement).closest<HTMLButtonElement>('button[data-ui-button]');
    if (btn && !btn.disabled && ['primary', 'ghost', 'quiet', 'danger'].includes(btn.dataset.uiButton!)) ripple(btn, event as PointerEvent);
  });
}

function ripple(btn: HTMLElement, event: PointerEvent): void {
  const rect = btn.getBoundingClientRect();
  const size = Math.max(rect.width, rect.height) * 2.2;
  const span = document.createElement('span');
  span.className = 'ripple';
  span.style.width = span.style.height = `${size}px`;
  span.style.left = `${event.clientX - rect.left - size / 2}px`;
  span.style.top = `${event.clientY - rect.top - size / 2}px`;
  btn.appendChild(span);
  setTimeout(() => span.remove(), 660);
}

export function toast(text: string, tone: '' | 'gold' = ''): void {
  const host = document.getElementById('toasts');
  if (!host) return;
  const el = document.createElement('div');
  el.className = `toast${tone === 'gold' ? ' toast--gold' : ''}`;
  el.innerHTML = `<span data-decor="${tone === 'gold' ? 'flower' : 'leaf'}"></span><span></span>`;
  el.lastElementChild!.textContent = text;
  host.appendChild(el);
  paintAll(el);
  setTimeout(() => {
    el.classList.add('is-out');
    el.addEventListener('animationend', () => el.remove(), { once: true });
  }, 2600);
}

export function openDialog({ title, body, confirm = '确定', cancel = '取消', tone = '' }: {
  title: string; body: string; confirm?: string; cancel?: string; tone?: '' | 'danger';
}): () => void {
  const root = document.getElementById('overlayRoot');
  if (!root) return () => {};
  const scrim = document.createElement('div');
  scrim.className = 'scrim';
  scrim.innerHTML = `
    <div class="dialog" role="dialog" aria-modal="true">
      <span class="dialog-mark" data-decor="flower" aria-hidden="true"></span>
      <h3></h3>
      <p></p>
      <div class="dialog-foot">
        <button class="btn btn--quiet" data-close></button>
        <button class="btn ${tone === 'danger' ? 'btn--danger' : 'btn--primary'}" data-ok></button>
      </div>
    </div>`;
  scrim.querySelector('h3')!.textContent = title;
  scrim.querySelector('p')!.textContent = body;
  scrim.querySelector('[data-close]')!.textContent = cancel;
  scrim.querySelector('[data-ok]')!.textContent = confirm;
  root.appendChild(scrim);
  paintAll(scrim);

  const close = () => {
    scrim.classList.add('is-closing');
    setTimeout(() => scrim.remove(), 220);
    document.removeEventListener('keydown', onKey);
  };
  const onKey = (event: KeyboardEvent) => { if (event.key === 'Escape') close(); };
  document.addEventListener('keydown', onKey);
  scrim.addEventListener('click', event => { if (event.target === scrim) close(); });
  scrim.querySelector('[data-close]')!.addEventListener('click', close);
  scrim.querySelector('[data-ok]')!.addEventListener('click', () => { close(); toast('已确认'); });
  return close;
}

export function openDrawer(id: string): (() => void) | undefined {
  const drawer = document.getElementById(id);
  if (!drawer) return undefined;
  const root = document.getElementById('overlayRoot');
  if (!root) return undefined;
  const scrim = document.createElement('div');
  scrim.className = 'scrim';
  scrim.style.placeItems = 'stretch';
  root.appendChild(scrim);
  requestAnimationFrame(() => {
    drawer.classList.add('is-open');
    drawer.setAttribute('aria-hidden', 'false');
  });

  const close = () => {
    drawer.classList.remove('is-open');
    drawer.setAttribute('aria-hidden', 'true');
    scrim.classList.add('is-closing');
    setTimeout(() => scrim.remove(), 220);
    document.removeEventListener('keydown', onKey);
  };
  const onKey = (event: KeyboardEvent) => { if (event.key === 'Escape') close(); };
  document.addEventListener('keydown', onKey);
  scrim.addEventListener('click', close);
  drawer.querySelectorAll('[data-drawer-close]').forEach(btn => btn.addEventListener('click', close));
  return close;
}
