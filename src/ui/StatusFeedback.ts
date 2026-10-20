import { t } from '../i18n';

export const STATUS_EFFECTS = ['001', '002', '003', '101', '102', '103', '501'] as const;
export type StatusEffect = typeof STATUS_EFFECTS[number];
const COLORS = ['242, 243, 244', '26, 26, 26', '171, 55, 47'];
interface EffectFade {
  layer: HTMLDivElement | null;
  from: number;
  to: number;
  changedAt: number;
  startedAt: number;
}

export class StatusFeedback {
  private readonly root = document.createElement('div');
  private forced: StatusEffect | null = null;
  private target: HTMLElement | null = null;
  private previousFilter = '';
  private current: StatusEffect | null = null;
  private readonly fades = new Map<StatusEffect, EffectFade>();

  constructor(container: HTMLElement) {
    this.root.className = 'status-feedback';
    this.root.setAttribute('aria-hidden', 'true');
    Object.assign(this.root.style, { position: 'fixed', inset: '0', pointerEvents: 'none', zIndex: '20', display: 'none' });
    container.appendChild(this.root);
  }

  command(value: string): string | null {
    const match = /^\/visual\.statu(?:\s+(\S+))?\s*$/.exec(value);
    if (!match) return value.startsWith('/visual.statu') ? t('visual.status.usage') : null;
    const id = match[1];
    if (!id || id === 'reset') {
      this.forced = null;
      this.render(null);
      return t('visual.status.reset');
    }
    if (!STATUS_EFFECTS.includes(id as StatusEffect)) return t('visual.status.usage');
    this.forced = id as StatusEffect;
    this.render(this.forced);
    return t('visual.status.forced', { id });
  }

  update(automatic: StatusEffect | null, enabled: boolean, target: HTMLElement | null): void {
    if (target !== this.target) {
      if (this.target) this.target.style.filter = this.previousFilter;
      this.target = target;
      this.previousFilter = target?.style.filter ?? '';
    }
    this.render(this.forced ?? (enabled ? automatic : null));
  }

  private render(effect: StatusEffect | null): void {
    const now = performance.now();
    if (this.current !== effect) {
      for (const [id, fade] of this.fades) {
        fade.from = this.strength(fade, now);
        fade.to = id === effect ? 1 : 0;
        fade.changedAt = now;
      }
      if (effect && !this.fades.has(effect)) this.fades.set(effect, this.createFade(effect, now));
      this.current = effect;
    }
    this.root.dataset.effect = effect ?? '';
    let visible = false, blur = 0;
    for (const [id, fade] of this.fades) {
      const strength = this.strength(fade, now);
      if (!fade.to && now - fade.changedAt >= 1000) {
        fade.layer?.remove();
        this.fades.delete(id);
        continue;
      }
      const phase = (now - fade.startedAt) * Math.PI * 2 / 2400;
      if (id === '501') blur = strength * 4 * (1 + .15 * Math.sin(phase));
      else {
        visible = true;
        fade.layer!.style.opacity = String(strength * (1 - (1 - Math.cos(phase)) / 2 * (id[0] === '1' ? .1 : .15)));
      }
    }
    this.root.style.display = visible ? 'block' : 'none';
    if (this.target) this.target.style.filter = blur > 0
      ? `${this.previousFilter} blur(${blur.toFixed(3)}px)`.trim() : this.previousFilter;
  }

  private strength(fade: EffectFade, now: number): number {
    const progress = Math.max(0, Math.min(1, (now - fade.changedAt) / 1000));
    return fade.from + (fade.to - fade.from) * progress * progress * (3 - 2 * progress);
  }

  private createFade(effect: StatusEffect, now: number): EffectFade {
    const layer = effect === '501' ? null : document.createElement('div');
    if (layer) {
      const color = COLORS[Number(effect[2]) - 1];
      layer.dataset.effect = effect;
      Object.assign(layer.style, { position: 'absolute', inset: '0', height: effect[0] === '0' ? '20%' : '100%' });
      layer.style.backgroundImage = effect[0] === '0'
        ? `linear-gradient(to bottom, rgba(${color}, 0.8), rgba(${color}, 0))`
        : `radial-gradient(circle farthest-corner at center, rgba(${color}, 0) 50%, rgba(${color}, 1) 100%)`;
      this.root.appendChild(layer);
    }
    return { layer, from: 0, to: 1, changedAt: now, startedAt: now };
  }

  dispose(): void {
    if (this.target) this.target.style.filter = this.previousFilter;
    this.fades.clear();
    this.root.remove();
  }
}
