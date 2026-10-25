import { motionLibrary, type PetalLoadingInstance } from '../adapt/motion';

/** 创建页遮罩：模板驱动，时间与阶段共同推进 */
export interface AtlasLoadingHandle {
  update(label: string): void;
  phase(progress: number): void;
  complete(): Promise<void>;
  cancel(): void;
}

const raf: (callback: FrameRequestCallback) => number = typeof requestAnimationFrame === 'function'
  ? requestAnimationFrame
  : callback => setTimeout(() => callback(performance.now()), 16) as unknown as number;
const caf: (id: number) => void = typeof cancelAnimationFrame === 'function'
  ? cancelAnimationFrame
  : id => clearTimeout(id);

export function createAtlasLoading(panel: HTMLElement, overlay: HTMLElement): AtlasLoadingHandle {
  const host = overlay.querySelector<HTMLElement>('.atlas-loading-flower')!;
  const label = overlay.querySelector('.atlas-loading-label')!;
  const flower: PetalLoadingInstance = motionLibrary['petal-loading'].create(host, { size: 132 });
  overlay.classList.remove('hidden');
  panel.setAttribute('aria-busy', 'true');
  let finished = false;
  const startedAt = performance.now();
  let shown = .06;
  let target = .06;
  let last = startedAt;
  let frame = 0;
  const tick = (now: number): void => {
    if (finished) return;
    // 时间缓动保证不停顿；阶段值只作下限
    target = Math.max(target, .97 * (1 - Math.exp(-(now - startedAt) / 1000)));
    shown += (target - shown) * (1 - Math.exp(-Math.max(0, now - last) / 160));
    last = now;
    flower.setProgress(shown);
    frame = raf(tick);
  };
  flower.setProgress(shown);
  frame = raf(tick);
  const cancel = (): void => {
    if (finished) return;
    finished = true;
    caf(frame);
    overlay.classList.add('hidden');
    panel.removeAttribute('aria-busy');
    flower.destroy();
  };
  return {
    update(text: string): void { if (!finished) label.textContent = text; },
    phase(progress: number): void {
      if (finished) return;
      target = Math.max(target, Math.max(.06, Math.min(1, progress)));
    },
    complete: async (): Promise<void> => {
      if (finished) return;
      flower.finish();
      cancel();
    },
    cancel,
  };
}
