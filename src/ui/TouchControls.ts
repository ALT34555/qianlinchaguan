import type { Input } from '../core/Input';
import { actionLabel, type GameAction, type GameSettings } from '../core/GameSettings';
import { icon } from './Icons';
import { t } from '../i18n';

/** 触控手指事件绑定 */
export class TouchControls {
  private readonly root = document.createElement('div');
  private readonly held = new Map<number, GameAction>();
  private readonly moveButtons: HTMLButtonElement[] = [];
  private readonly actionButtons: { el: HTMLButtonElement; action: GameAction }[] = [];
  private look: { id: number; x: number; y: number } | null = null;
  private stickPointer: number | null = null;
  constructor(private readonly input: Input, canvas: HTMLCanvasElement) {
    this.root.className = 'touch-controls hidden';
    this.root.innerHTML = `<div class="touch-move"><div class="touch-stick" role="group" aria-label="${t('settings.touchLayout.move')}">${icon('stick-base', 'stick-base')}${icon('stick-knob', 'stick-knob')}</div></div><div class="touch-actions"><button data-action="sprint"></button><button data-action="jump"></button><button data-action="descend"></button></div>`;
    document.body.appendChild(this.root);
    this.moveButtons.push(...this.root.querySelectorAll<HTMLButtonElement>('.touch-move button'));
    this.actionButtons.push(
      { el: this.root.querySelector<HTMLButtonElement>('.touch-actions [data-action="sprint"]')!, action: 'sprint' },
      { el: this.root.querySelector<HTMLButtonElement>('.touch-actions [data-action="jump"]')!, action: 'jump' },
      { el: this.root.querySelector<HTMLButtonElement>('.touch-actions [data-action="descend"]')!, action: 'descend' },
    );
    this.applyLocale();
    const stick = this.root.querySelector<HTMLElement>('.touch-stick')!;
    const move = (event: PointerEvent) => {
      if (event.pointerId !== this.stickPointer) return;
      const rect = stick.getBoundingClientRect(), radius = rect.width * .32;
      let x = event.clientX - rect.left - rect.width / 2, y = event.clientY - rect.top - rect.height / 2;
      const length = Math.hypot(x, y);
      if (length > radius) { x *= radius / length; y *= radius / length; }
      const knob = this.root.querySelector<HTMLElement>('.stick-knob')!;
      knob.style.left = `${50 + x / rect.width * 100}%`; knob.style.top = `${50 + y / rect.height * 100}%`;
      this.input.setVirtualAction('left', x < -radius * .25); this.input.setVirtualAction('right', x > radius * .25);
      this.input.setVirtualAction('forward', y < -radius * .25); this.input.setVirtualAction('back', y > radius * .25);
    };
    stick.addEventListener('pointerdown', event => {
      if (this.stickPointer !== null) return;
      event.preventDefault(); this.stickPointer = event.pointerId; stick.setPointerCapture(event.pointerId); move(event);
    });
    stick.addEventListener('pointermove', move);
    for (const type of ['pointerup', 'pointercancel', 'lostpointercapture']) stick.addEventListener(type, event => {
      if ((event as PointerEvent).pointerId === this.stickPointer) this.releaseStick();
    });
    this.root.querySelectorAll<HTMLButtonElement>('[data-action]').forEach(button => {
      button.addEventListener('pointerdown', event => {
        event.preventDefault(); button.setPointerCapture(event.pointerId);
        const action = button.dataset.action as GameAction;
        this.held.set(event.pointerId, action); this.input.setVirtualAction(action, true);
      });
      const release = (event: PointerEvent) => {
        const action = this.held.get(event.pointerId); this.held.delete(event.pointerId);
        if (action && !Array.from(this.held.values()).includes(action)) this.input.setVirtualAction(action, false);
      };
      button.addEventListener('pointerup', release); button.addEventListener('pointercancel', release);
      button.addEventListener('lostpointercapture', release);
    });
    canvas.style.touchAction = 'none';
    canvas.addEventListener('pointerdown', event => {
      // 触屏拖动视角，不锁定指针
      // 按住场景拖动转视角
      if (this.root.classList.contains('hidden') || this.look || this.input.pointerLocked) return;
      canvas.setPointerCapture(event.pointerId); this.look = { id: event.pointerId, x: event.clientX, y: event.clientY };
    });
    canvas.addEventListener('pointermove', event => {
      if (this.look?.id !== event.pointerId) return;
      this.input.addLook(event.clientX - this.look.x, event.clientY - this.look.y);
      this.look.x = event.clientX; this.look.y = event.clientY;
    });
    const end = (event: PointerEvent) => { if (this.look?.id === event.pointerId) this.look = null; };
    canvas.addEventListener('pointerup', end); canvas.addEventListener('pointercancel', end);
    canvas.addEventListener('lostpointercapture', end);
    // 鼠标锁定接手视角后
    document.addEventListener('pointerlockchange', () => { if (this.input.pointerLocked) this.look = null; });
    window.addEventListener('blur', () => this.clear());
  }
  /** 方向键是符号 */
  applyLocale(): void {
    this.moveButtons.forEach(button => {
      const action = button.dataset.action as GameAction;
      button.setAttribute('aria-label', actionLabel(action));
    });
    const labels: Record<string, string> = { sprint: t('touch.sprint'), jump: t('touch.jump'), descend: t('touch.descend') };
    this.actionButtons.forEach(({ el, action }) => {
      el.textContent = labels[action];
      el.setAttribute('aria-label', labels[action]);
    });
  }
  private clear(): void {
    this.releaseStick();
    for (const action of this.held.values()) this.input.setVirtualAction(action, false);
    this.held.clear(); this.look = null;
  }
  private releaseStick(): void {
    this.stickPointer = null;
    for (const action of ['left', 'right', 'forward', 'back'] as const) this.input.setVirtualAction(action, false);
    const knob = this.root.querySelector<HTMLElement>('.stick-knob')!;
    knob.style.left = '50%'; knob.style.top = '50%';
  }
  applySettings(settings: GameSettings): void {
    this.root.style.opacity = String(settings.touchOpacity ?? 1);
    for (const key of ['move', 'actions'] as const) {
      const group = this.root.querySelector<HTMLElement>(`.touch-${key}`)!;
      const point = settings.touchLayout![key];
      group.style.left = `${point.x}%`; group.style.top = `${point.y}%`;
      group.style.right = 'auto'; group.style.bottom = 'auto';
      group.style.transform = `translate(-${point.x}%, -${point.y}%)`;
    }
  }
  setVisible(visible: boolean): void { this.clear(); this.root.classList.toggle('hidden', !visible); }
}
