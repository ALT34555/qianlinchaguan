// 通用交互：水波、开关、浮层、提示条、载入遮罩
import { paintAll } from './decor';
import { initButtons } from './buttons';

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

export interface LoadingOptions {
  title?: string;
  /** 0..1；缺省时反复扫向 90% */
  progress?: () => number;
  /** 加载结束后的回退淡出，单位毫秒 */
  linger?: number;
}

export interface LoadingHandle {
  set(title: string, progress?: () => number): void;
  done(): Promise<void>;
  cancel(): void;
}

const easeOutCubic = (t: number): number => 1 - (1 - t) ** 3;

/** 世界载入遮罩：右侧推入，环形填充按真实进度走 */
export function showLoading({ title = '', progress, linger = 420 }: LoadingOptions = {}): LoadingHandle {
  const veil = document.createElement('div');
  veil.className = 'load-veil load-veil--slide';
  veil.setAttribute('role', 'status');
  veil.setAttribute('aria-live', 'polite');
  veil.innerHTML = `
    <div class="load-card">
      <span class="eyebrow">LOADING</span>
      <div class="ring load-ring" style="--p:0"><span class="ring-value">0%</span></div>
      <h3 class="load-title"></h3>
      <div class="bar load-bar"><i style="--p:0%"></i></div>
    </div>`;
  const titleEl = veil.querySelector<HTMLElement>('.load-title')!;
  const ring = veil.querySelector<HTMLElement>('.load-ring')!;
  const ringValue = veil.querySelector<HTMLElement>('.ring-value')!;
  const bar = veil.querySelector<HTMLElement>('.load-bar i')!;
  ring.insertAdjacentHTML('afterbegin', flowerRingSVG(116));
  titleEl.textContent = title;
  const host = document.getElementById('overlayRoot') ?? document.body;
  host.appendChild(veil);

  const start = performance.now();
  let finished = false;
  let frame = 0;
  const tick = (now: number) => {
    if (finished) return;
    const elapsed = now - start;
    const target = progress ? progress() : .9 * (1 - Math.exp(-elapsed / 2600));
    const eased = easeOutCubic(Math.min(1, Math.max(0, target)));
    ring.style.setProperty('--p', eased.toFixed(3));
    const percent = Math.round(eased * 100);
    bar.style.setProperty('--p', `${percent}%`);
    ringValue.textContent = `${percent}%`;
    frame = requestAnimationFrame(tick);
  };
  frame = requestAnimationFrame(tick);

  const stop = (): void => {
    if (finished) return;
    finished = true;
    cancelAnimationFrame(frame);
  };
  const cancel = (): void => { stop(); veil.remove(); };
  const done = (): Promise<void> => {
    if (finished) { veil.remove(); return Promise.resolve(); }
    stop();
    ring.style.setProperty('--p', '1');
    bar.style.setProperty('--p', '100%');
    ringValue.textContent = '100%';
    veil.classList.add('is-done');
    return new Promise(resolve => {
      setTimeout(() => { veil.remove(); resolve(); }, linger);
    });
  };
  const set = (next: string, nextProgress?: () => number): void => {
    if (finished) return;
    if (next) titleEl.textContent = next;
    if (nextProgress) progress = nextProgress;
  };
  return { set, done, cancel };
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
