/**
 * 键盘 / 鼠标输入。按 KeyboardEvent.code 记录按键状态，并提供"按下"事件回调。
 */

import { actionCodes, defaultSettings, type GameAction, type GameSettings } from './GameSettings';

/** 需要阻止浏览器默认行为的按键（页面滚动、F12 开发者工具等） */
const PREVENT_DEFAULT = new Set([
  'ArrowUp',
  'ArrowDown',
  'ArrowLeft',
  'ArrowRight',
  'Space',
  'F12',
]);

export class Input {
  private readonly down = new Set<string>();
  private readonly virtual = new Set<GameAction>();
  private readonly pressHandlers = new Map<string, ((e: KeyboardEvent) => void)[]>();
  private readonly actionHandlers = new Map<GameAction, ((e: KeyboardEvent) => void)[]>();
  private bindings = defaultSettings().keyBindings;
  private enabledValue = false;
  private mouseDX = 0;
  private mouseDY = 0;
  pointerLocked = false;

  constructor(private readonly target: HTMLElement) {
    window.addEventListener('keydown', this.onKeyDown, { capture: true });
    window.addEventListener('keyup', this.onKeyUp, { capture: true });
    window.addEventListener('blur', () => { this.down.clear(); this.virtual.clear(); });
    document.addEventListener('mousemove', this.onMouseMove);
    document.addEventListener('pointerlockchange', () => {
      this.pointerLocked = document.pointerLockElement === this.target;
    });
  }

  requestPointerLock(): Promise<void> {
    return this.pointerLocked ? Promise.resolve() : Promise.resolve(this.target.requestPointerLock());
  }

  set enabled(value: boolean) {
    this.enabledValue = value;
    this.down.clear(); this.mouseDX = 0; this.mouseDY = 0;
    this.virtual.clear();
  }

  setBindings(bindings: GameSettings['keyBindings']): void { this.bindings = { ...bindings }; this.enabled = this.enabledValue; }

  isActionDown(action: GameAction): boolean { return this.virtual.has(action) || this.isDown(...actionCodes(action, this.bindings)); }

  setVirtualAction(action: GameAction, pressed: boolean): void {
    if (!pressed) { this.virtual.delete(action); return; }
    if (!this.enabledValue || this.virtual.has(action)) return;
    this.virtual.add(action);
    const event = new KeyboardEvent('keydown', { code: this.bindings[action] });
    for (const handler of this.actionHandlers.get(action) ?? []) handler(event);
  }
  addLook(dx: number, dy: number): void {
    if (this.enabledValue) { this.mouseDX += dx; this.mouseDY += dy; }
  }

  onAction(action: GameAction, fn: (e: KeyboardEvent) => void): void {
    const list = this.actionHandlers.get(action) ?? []; list.push(fn); this.actionHandlers.set(action, list);
  }

  isDown(...codes: string[]): boolean {
    for (const c of codes) if (this.down.has(c)) return true;
    return false;
  }

  /** 注册按键"按下"回调（忽略长按自动重复）。 */
  onPress(code: string, fn: (e: KeyboardEvent) => void): void {
    const list = this.pressHandlers.get(code) ?? [];
    list.push(fn);
    this.pressHandlers.set(code, list);
  }

  /** 取出并清零本帧累计的鼠标位移。 */
  consumeMouse(): [number, number] {
    const r: [number, number] = [this.mouseDX, this.mouseDY];
    this.mouseDX = 0;
    this.mouseDY = 0;
    return r;
  }

  private onKeyDown = (e: KeyboardEvent) => {
    if (!this.enabledValue || (e.target instanceof HTMLElement && e.target.closest('input, select, textarea, button, [contenteditable]'))) return;
    if (PREVENT_DEFAULT.has(e.code) || Object.values(this.bindings).includes(e.code)) e.preventDefault();
    this.down.add(e.code);
    if (e.repeat) return;
    const list = this.pressHandlers.get(e.code);
    if (list) for (const fn of list) fn(e);
    for (const [action, handlers] of this.actionHandlers) {
      if (actionCodes(action, this.bindings).includes(e.code)) for (const fn of handlers) fn(e);
    }
  };

  private onKeyUp = (e: KeyboardEvent) => {
    if (this.enabledValue && PREVENT_DEFAULT.has(e.code)) e.preventDefault();
    this.down.delete(e.code);
  };

  private onMouseMove = (e: MouseEvent) => {
    if (!this.pointerLocked || !this.enabledValue) return;
    this.mouseDX += e.movementX;
    this.mouseDY += e.movementY;
  };
}
