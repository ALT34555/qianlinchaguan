// 动效模板库：统一登记与调用
const PETAL_D = 'M24 24C18.8 20.2 16.8 13.6 24 4.6C31.2 13.6 29.2 20.2 24 24Z';
const PETAL_ROT = [0, 72, 144, 216, 288];

export interface PetalLoadingOptions {
  /** 图形边长，默认 96 */
  size?: number;
}

export interface PetalLoadingInstance {
  readonly element: SVGSVGElement;
  /** 0..1：每瓣占 20%，花心占最后 20% */
  setProgress(progress: number): void;
  finish(): void;
  destroy(): void;
}

const clamp01 = (value: number): number => Math.max(0, Math.min(1, value));

/** 第 index 瓣在总进度中的开合比例 */
export function petalScale(progress: number, index: number): number {
  const pct = clamp01(progress) * 100;
  return clamp01((pct - index * 20) / 20);
}

/** 花心在总进度中的开合比例 */
export function coreScale(progress: number): number {
  return clamp01((clamp01(progress) * 100 - 80) / 20);
}

/** 五瓣花轮廓，逐瓣由 --s 控制 */
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

/** 逐瓣展开加载：调用方每帧或每阶段喂 0..1 */
export function createPetalLoading(host: HTMLElement, options: PetalLoadingOptions = {}): PetalLoadingInstance {
  const svg = loaderSVG(options.size ?? 96);
  host.replaceChildren(svg);
  const petals = [...svg.querySelectorAll<SVGPathElement>('.pl-petal')];
  const core = svg.querySelector<SVGCircleElement>('.pl-core')!;
  const apply = (progress: number): void => {
    petals.forEach((petal, index) => petal.style.setProperty('--s', petalScale(progress, index).toFixed(3)));
    core.style.setProperty('--s', coreScale(progress).toFixed(3));
  };
  apply(0);
  return {
    element: svg,
    setProgress: apply,
    finish: (): void => apply(1),
    destroy: (): void => svg.remove(),
  };
}

/** 动效模板清单：新效果在此登记 */
export const motionLibrary = {
  'petal-loading': {
    name: '逐瓣展开加载',
    description: '五瓣依次展开，每瓣占 20%，花心占最后 20%',
    create: createPetalLoading,
  },
} as const;

export type MotionId = keyof typeof motionLibrary;
