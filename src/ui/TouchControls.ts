import type { Input } from '../core/Input';
import { actionLabel, type GameAction } from '../core/GameSettings';
import { t } from '../i18n';

/** Each finger owns a held action; cancellation, focus loss and menus release it. */
export class TouchControls {
  private readonly root = document.createElement('div');
  private readonly held = new Map<number, GameAction>();
  private readonly moveButtons: HTMLButtonElement[] = [];
  private readonly actionButtons: { el: HTMLButtonElement; action: GameAction }[] = [];
  private look: { id: number; x: number; y: number } | null = null;
  constructor(private readonly input: Input, canvas: HTMLCanvasElement) {
    this.root.className = 'touch-controls hidden';
    this.root.innerHTML = `<div class="touch-move">${[['forward', '↑'], ['left', '←'], ['back', '↓'], ['right', '→']].map(([action, label]) => `<button data-action="${action}">${label}</button>`).join('')}</div><div class="touch-actions"><button data-action="sprint"></button><button data-action="jump"></button><button data-action="descend"></button></div>`;
    document.body.appendChild(this.root);
    this.moveButtons.push(...this.root.querySelectorAll<HTMLButtonElement>('.touch-move button'));
    this.actionButtons.push(
      { el: this.root.querySelector<HTMLButtonElement>('.touch-actions [data-action="sprint"]')!, action: 'sprint' },
      { el: this.root.querySelector<HTMLButtonElement>('.touch-actions [data-action="jump"]')!, action: 'jump' },
      { el: this.root.querySelector<HTMLButtonElement>('.touch-actions [data-action="descend"]')!, action: 'descend' },
    );
    this.applyLocale();
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
      // 显示虚拟按键时不会申请鼠标锁定（按键要留着自己可点），所以鼠标也要能像手指一样
      // 按住场景拖动转视角；鼠标已被锁定时位移由 Input 的 mousemove 负责，这里不再重复累计。
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
    // 鼠标锁定接手视角后，丢掉正在进行的拖拽，避免两套位移叠加。
    document.addEventListener('pointerlockchange', () => { if (this.input.pointerLocked) this.look = null; });
    window.addEventListener('blur', () => this.clear());
  }
  /** 方向键是符号，不需要翻译；文字按钮与无障碍名随语言更新。 */
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
    for (const action of this.held.values()) this.input.setVirtualAction(action, false);
    this.held.clear(); this.look = null;
  }
  setVisible(visible: boolean): void { this.clear(); this.root.classList.toggle('hidden', !visible); }
}
