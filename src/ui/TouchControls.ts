import jxqyBase from '../../content/assets/ui/control_pad/m-joy-base.svg?raw';
import jxqyKnob from '../../content/assets/ui/control_pad/m-joy-knob.svg?raw';
import jxqySprint from '../../content/assets/ui/control_pad/m-joy-sprint.svg?raw';
import type { Input } from '../core/Input';
import { TOUCH_ITEMS, type GameAction, type GameSettings, type JoystickStyle, type TouchItem } from '../core/GameSettings';
import { padIconUrl } from './Icons';
import { t } from '../i18n';

// 生成摇杆HTML
export function renderJoystickMarkup(style: JoystickStyle): string {
  if (style === 'jxqy') {
    const art = (svg: string, className: string) => svg.replace(/<\?xml[^>]*>/, '').replace('<svg ', `<svg class="${className}" `).replaceAll('#a61b29', 'currentColor');
    return `<div class="joystick-stage joystick-jxqy" role="group" aria-label="${t('settings.touchLayout.move')}">${art(jxqySprint, 'joy-sprint-badge').replace('<svg ', `<svg role="button" tabindex="0" aria-pressed="false" aria-label="${t('touch.autoSprint')}" `)}${art(jxqyBase, 'joy-base-img')}${art(jxqyKnob, 'joy-knob-img stick-knob')}</div>`;
  }
  if (style === 'cross') {
    return `<div class="dpad-cross-stage" role="group" aria-label="${t('settings.touchLayout.move')}">
      <img class="dpad-cross-bg" src="${padIconUrl('dpad-cross-whole')}" alt="" draggable="false">
      <button type="button" class="dpad-btn up" data-dir="0,-1" aria-label="${t('action.forward')}"><img src="${padIconUrl('dpad-up')}" alt="" draggable="false"></button>
      <button type="button" class="dpad-btn down" data-dir="0,1" aria-label="${t('action.back')}"><img src="${padIconUrl('dpad-down')}" alt="" draggable="false"></button>
      <button type="button" class="dpad-btn left" data-dir="-1,0" aria-label="${t('action.left')}"><img src="${padIconUrl('dpad-left')}" alt="" draggable="false"></button>
      <button type="button" class="dpad-btn right" data-dir="1,0" aria-label="${t('action.right')}"><img src="${padIconUrl('dpad-right')}" alt="" draggable="false"></button>
    </div>`;
  }
  if (style === 'octagon') {
    return `<div class="dpad-oct-stage" role="group" aria-label="${t('settings.touchLayout.move')}">
      <div class="dpad-oct-grid">
        <button type="button" class="oct-cell-btn" data-dir="-0.7,-0.7" aria-label="左前"><img src="${padIconUrl('dpad-up-left')}" alt="" draggable="false"></button>
        <button type="button" class="oct-cell-btn" data-dir="0,-1" aria-label="${t('action.forward')}"><img src="${padIconUrl('dpad-flow-n')}" alt="" draggable="false"></button>
        <button type="button" class="oct-cell-btn" data-dir="0.7,-0.7" aria-label="右前"><img src="${padIconUrl('dpad-up-right')}" alt="" draggable="false"></button>
        <button type="button" class="oct-cell-btn" data-dir="-1,0" aria-label="${t('action.left')}"><img src="${padIconUrl('dpad-flow-w')}" alt="" draggable="false"></button>
        <div class="oct-cell-center"><img src="${padIconUrl('joy-knob-ink')}" alt="" draggable="false"></div>
        <button type="button" class="oct-cell-btn" data-dir="1,0" aria-label="${t('action.right')}"><img src="${padIconUrl('dpad-flow-e')}" alt="" draggable="false"></button>
        <button type="button" class="oct-cell-btn" data-dir="-0.7,0.7" aria-label="左后"><img src="${padIconUrl('dpad-down-left')}" alt="" draggable="false"></button>
        <button type="button" class="oct-cell-btn" data-dir="0,1" aria-label="${t('action.back')}"><img src="${padIconUrl('dpad-flow-s')}" alt="" draggable="false"></button>
        <button type="button" class="oct-cell-btn" data-dir="0.7,0.7" aria-label="右后"><img src="${padIconUrl('dpad-down-right')}" alt="" draggable="false"></button>
      </div>
    </div>`;
  }
  const baseMap: Record<string, string> = {
    bagua: 'joy-base-bagua', luopan: 'joy-base-luopan', lotus: 'joy-base-lotus',
    minimal: 'joy-base-minimal', classic: 'stick-base'
  };
  const knobMap: Record<string, string> = {
    bagua: 'joy-knob-seal', luopan: 'joy-knob-jade', lotus: 'joy-knob-sword',
    minimal: 'joy-knob-ink', classic: 'stick-knob'
  };
  const baseIcon = baseMap[style] || 'joy-base-bagua';
  const knobIcon = knobMap[style] || 'joy-knob-seal';
  const hasArc = style !== 'classic';
  return `<div class="joystick-stage" role="group" aria-label="${t('settings.touchLayout.move')}">
    <img class="joy-sprint-badge" src="${padIconUrl('joy-sprint-lock')}" alt="${t('touch.autoSprint')}" title="${t('touch.autoSprint')}" role="button" tabindex="0" aria-pressed="false" draggable="false">
    <img class="joy-base-img" src="${padIconUrl(baseIcon)}" alt="" draggable="false">
    ${hasArc ? `<img class="joy-active-arc" src="${padIconUrl('joy-active-arc-n')}" alt="" draggable="false">` : ''}
    <img class="joy-knob-img stick-knob" src="${padIconUrl(knobIcon)}" alt="" draggable="false">
  </div>`;
}

// 生成动作按键HTML
export function renderActionMarkup(action: Exclude<TouchItem, 'move'>, style: JoystickStyle = 'jxqy'): string {
  const icon = action === 'sprint' ? 'dodge' : action;
  return `<button type="button" class="touch-action-btn touch-${action}" data-action="${action}" aria-label="${t(`touch.${action}`)}"><img class="action-plate" src="${padIconUrl(`action-plate-${style}`)}" alt="" draggable="false"><img class="action-icon" src="${padIconUrl(`btn-${icon}-${style}`)}" alt="" draggable="false"><span class="skill-name-tip">${t(`touch.${action}`)}</span></button>`;
}

export function renderActionsMarkup(style: JoystickStyle = 'jxqy'): string {
  return (['sprint', 'jump', 'crouch'] as const).map(action => renderActionMarkup(action, style)).join('');
}

// 触控手指事件绑定
export class TouchControls {
  private readonly root = document.createElement('div');
  private readonly held = new Map<number, GameAction>();
  private readonly actionButtons: { el: HTMLButtonElement; action: GameAction }[] = [];
  private look: { id: number; x: number; y: number } | null = null;
  private stickPointer: number | null = null;
  private sprintLocked = false;
  private currentStyle: JoystickStyle = 'jxqy';
  private readonly events = new AbortController();
  private moveEvents = new AbortController();

  constructor(private readonly input: Input, canvas: HTMLCanvasElement) {
    this.root.className = 'touch-controls hidden';
    this.root.innerHTML = `<div class="touch-move"></div>${renderActionsMarkup()}`;
    document.body.appendChild(this.root);

    this.actionButtons.push(
      ...(['sprint', 'jump', 'crouch'] as const).map(action => ({ el: this.root.querySelector<HTMLButtonElement>(`[data-action="${action}"]`)!, action })),
    );

    this.setStyle('jxqy');
    this.bindActionButtons();
    this.bindCanvasLook(canvas);
  }

  // 绑定动作按键事件
  private bindActionButtons(): void {
    this.root.querySelectorAll<HTMLButtonElement>('[data-action]').forEach(button => {
      button.addEventListener('pointerdown', event => {
        event.preventDefault();
        button.setPointerCapture(event.pointerId);
        button.classList.add('pressed');
        const action = button.dataset.action as GameAction;
        if (action === 'sprint' && this.sprintLocked) this.releaseStick(false);
        this.held.set(event.pointerId, action);
        this.input.setVirtualAction(action, true);
      }, { signal: this.events.signal });
      const release = (event: PointerEvent) => {
        button.classList.remove('pressed');
        const action = this.held.get(event.pointerId);
        this.held.delete(event.pointerId);
        if (action && !Array.from(this.held.values()).includes(action)) this.input.setVirtualAction(action, action === 'sprint' && this.sprintLocked);
      };
      button.addEventListener('pointerup', release, { signal: this.events.signal });
      button.addEventListener('pointercancel', release, { signal: this.events.signal });
      button.addEventListener('lostpointercapture', release, { signal: this.events.signal });
    });
  }

  // 绑定视角触控
  private bindCanvasLook(canvas: HTMLCanvasElement): void {
    canvas.style.touchAction = 'none';
    canvas.addEventListener('pointerdown', event => {
      if (this.root.classList.contains('hidden') || this.look || this.input.pointerLocked) return;
      canvas.setPointerCapture(event.pointerId);
      this.look = { id: event.pointerId, x: event.clientX, y: event.clientY };
    }, { signal: this.events.signal });
    canvas.addEventListener('pointermove', event => {
      if (this.look?.id !== event.pointerId) return;
      this.input.addLook(event.clientX - this.look.x, event.clientY - this.look.y);
      this.look.x = event.clientX;
      this.look.y = event.clientY;
    }, { signal: this.events.signal });
    const end = (event: PointerEvent) => { if (this.look?.id === event.pointerId) this.look = null; };
    canvas.addEventListener('pointerup', end, { signal: this.events.signal });
    canvas.addEventListener('pointercancel', end, { signal: this.events.signal });
    canvas.addEventListener('lostpointercapture', end, { signal: this.events.signal });
    document.addEventListener('pointerlockchange', () => { if (this.input.pointerLocked) this.look = null; }, { signal: this.events.signal });
    window.addEventListener('blur', () => this.clear(), { signal: this.events.signal });
  }

  // 切换摇杆形态
  setStyle(style: JoystickStyle): void {
    this.clear();
    this.currentStyle = style;
    this.moveEvents.abort();
    this.moveEvents = new AbortController();
    const moveGroup = this.root.querySelector<HTMLElement>('.touch-move')!;
    moveGroup.innerHTML = renderJoystickMarkup(style);
    this.root.querySelectorAll<HTMLButtonElement>('.touch-action-btn').forEach(button => {
      const action = button.dataset.action as Exclude<TouchItem, 'move'>;
      button.querySelector<HTMLImageElement>('.action-plate')!.src = padIconUrl(`action-plate-${style}`);
      button.querySelector<HTMLImageElement>('.action-icon')!.src = padIconUrl(`btn-${action === 'sprint' ? 'dodge' : action}-${style}`);
    });
    const signal = this.moveEvents.signal;

    if (style === 'cross' || style === 'octagon') {
      this.bindDirectionButtons(moveGroup, signal);
    } else {
      this.bindJoystickStage(moveGroup, signal);
    }
  }

  // 绑定方向键事件
  private bindDirectionButtons(container: HTMLElement, signal: AbortSignal): void {
    const dirMap = new Map<number, [number, number]>();
    container.querySelectorAll<HTMLButtonElement>('[data-dir]').forEach(btn => {
      const [vx, vy] = btn.dataset.dir!.split(',').map(Number);
      btn.addEventListener('pointerdown', event => {
        event.preventDefault();
        btn.setPointerCapture(event.pointerId);
        btn.classList.add('pressed');
        dirMap.set(event.pointerId, [vx, vy]);
        this.updateCombinedDir(dirMap);
      }, { signal });
      const release = (event: PointerEvent) => {
        btn.classList.remove('pressed');
        dirMap.delete(event.pointerId);
        this.updateCombinedDir(dirMap);
      };
      btn.addEventListener('pointerup', release, { signal });
      btn.addEventListener('pointercancel', release, { signal });
      btn.addEventListener('lostpointercapture', release, { signal });
    });
  }

  // 综合多指方向输入
  private updateCombinedDir(dirMap: Map<number, [number, number]>): void {
    let sx = 0, sy = 0;
    for (const [x, y] of dirMap.values()) { sx += x; sy += y; }
    this.input.setVirtualAction('left', sx < -0.2);
    this.input.setVirtualAction('right', sx > 0.2);
    this.input.setVirtualAction('forward', sy < -0.2);
    this.input.setVirtualAction('back', sy > 0.2);
  }

  // 绑定滑动摇杆事件
  private bindJoystickStage(container: HTMLElement, signal: AbortSignal): void {
    const stage = container.querySelector<HTMLElement>('.joystick-stage')!;
    const knob = container.querySelector<HTMLElement>('.joy-knob-img')!;
    const arc = container.querySelector<HTMLElement>('.joy-active-arc');
    const badge = container.querySelector<HTMLElement>('.joy-sprint-badge');

    const onMove = (event: PointerEvent) => {
      if (event.pointerId !== this.stickPointer) return;
      const rect = stage.getBoundingClientRect(), radius = Math.min(rect.width, rect.height) * 0.24;
      let x = event.clientX - rect.left - rect.width / 2;
      let y = event.clientY - rect.top - rect.height / 2;
      const length = Math.hypot(x, y);
      if (length > radius) { x *= radius / length; y *= radius / length; }

      knob.style.left = `${50 + (x / rect.width) * 100}%`;
      knob.style.top = `${50 + (y / rect.height) * 100}%`;

      if (arc && length > 6) {
        const deg = Math.atan2(y, x) * 180 / Math.PI + 90;
        arc.style.transform = `rotate(${deg}deg)`;
        arc.style.opacity = '0.9';
      }

      this.sprintLocked = y < -radius * 0.82 && Math.abs(x) < radius * 0.45;
      badge?.classList.toggle('locked', this.sprintLocked);
      badge?.setAttribute('aria-pressed', String(this.sprintLocked));
      this.input.setVirtualAction('sprint', this.sprintLocked || Array.from(this.held.values()).includes('sprint'));

      this.input.setVirtualAction('left', x < -radius * 0.25);
      this.input.setVirtualAction('right', x > radius * 0.25);
      this.input.setVirtualAction('forward', y < -radius * 0.25);
      this.input.setVirtualAction('back', y > radius * 0.25);
    };

    stage.addEventListener('pointerdown', event => {
      if (this.stickPointer !== null) return;
      this.releaseStick(false);
      event.preventDefault();
      this.stickPointer = event.pointerId;
      stage.setPointerCapture(event.pointerId);
      onMove(event);
    }, { signal });

    stage.addEventListener('pointermove', onMove, { signal });

    const onRelease = (event: PointerEvent) => {
      if (event.pointerId === this.stickPointer) this.releaseStick(event.type === 'pointerup');
    };
    stage.addEventListener('pointerup', onRelease, { signal });
    stage.addEventListener('pointercancel', onRelease, { signal });
    stage.addEventListener('lostpointercapture', onRelease, { signal });
    badge?.addEventListener('pointerdown', event => {
      event.preventDefault(); event.stopPropagation();
      this.releaseStick(false);
    }, { signal });
    badge?.addEventListener('keydown', event => {
      if (event.code === 'Enter' || event.code === 'Space') { event.preventDefault(); event.stopPropagation(); this.releaseStick(false); }
    }, { signal });
  }

  // 国际化文本更新
  applyLocale(): void {
    const labels: Record<string, string> = {
      sprint: t('touch.sprint'),
      jump: t('touch.jump'),
      crouch: t('touch.crouch'),
    };
    this.actionButtons.forEach(({ el, action }) => {
      const tip = el.querySelector('.skill-name-tip');
      if (tip) tip.textContent = labels[action];
      el.setAttribute('aria-label', labels[action]);
    });
  }

  // 清除全部触控输入状态
  private clear(): void {
    this.releaseStick(false);
    for (const action of this.held.values()) this.input.setVirtualAction(action, false);
    this.held.clear();
    this.actionButtons.forEach(({ el }) => el.classList.remove('pressed'));
    this.look = null;
  }

  // 释放摇杆归位
  private releaseStick(preserveLock = true): void {
    if (!preserveLock) this.sprintLocked = false;
    this.stickPointer = null;
    for (const action of ['left', 'right', 'forward', 'back'] as const) this.input.setVirtualAction(action, action === 'forward' && this.sprintLocked);
    this.input.setVirtualAction('sprint', this.sprintLocked || Array.from(this.held.values()).includes('sprint'));
    const knob = this.root.querySelector<HTMLElement>('.joy-knob-img');
    if (knob) {
      const rect = this.root.querySelector<HTMLElement>('.joystick-stage')!.getBoundingClientRect();
      knob.style.left = '50%';
      knob.style.top = this.sprintLocked ? `${50 - Math.min(rect.width, rect.height) * 24 / (rect.height || 1)}%` : '50%';
    }
    const arc = this.root.querySelector<HTMLElement>('.joy-active-arc');
    if (arc) { arc.style.opacity = this.sprintLocked ? '0.9' : '0'; arc.style.transform = 'rotate(0deg)'; }
    const badge = this.root.querySelector<HTMLElement>('.joy-sprint-badge');
    badge?.classList.toggle('locked', this.sprintLocked);
    badge?.setAttribute('aria-pressed', String(this.sprintLocked));
  }

  // 应用配置
  applySettings(settings: GameSettings): void {
    this.root.style.opacity = String(settings.touchOpacity ?? 1);
    if (settings.joystickStyle && settings.joystickStyle !== this.currentStyle) {
      this.setStyle(settings.joystickStyle);
    }
    for (const key of TOUCH_ITEMS) {
      const group = this.root.querySelector<HTMLElement>(`.touch-${key}`)!;
      if (!group) continue;
      const point = settings.touchLayout![key]!;
      group.style.setProperty('--touch-size', String(point.scale ?? 1));
      group.style.left = `${point.x}%`;
      group.style.top = `${point.y}%`;
      group.style.right = 'auto';
      group.style.bottom = 'auto';
      group.style.transform = `translate(-${point.x}%, -${point.y}%)`;
    }
  }

  setVisible(visible: boolean): void { this.clear(); this.root.classList.toggle('hidden', !visible); }
  dispose(): void {
    this.moveEvents.abort();
    this.events.abort();
    this.setVisible(false);
    this.root.remove();
  }
}
