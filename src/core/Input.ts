/**
 * 键盘 / 鼠标输入。按 KeyboardEvent.code 记录按键状态，并提供"按下"事件回调。
 */

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
  private readonly pressHandlers = new Map<string, ((e: KeyboardEvent) => void)[]>();
  private mouseDX = 0;
  private mouseDY = 0;
  pointerLocked = false;

  constructor(private readonly target: HTMLElement) {
    window.addEventListener('keydown', this.onKeyDown, { capture: true });
    window.addEventListener('keyup', this.onKeyUp, { capture: true });
    window.addEventListener('blur', () => this.down.clear());
    document.addEventListener('mousemove', this.onMouseMove);
    document.addEventListener('pointerlockchange', () => {
      this.pointerLocked = document.pointerLockElement === this.target;
    });
  }

  requestPointerLock(): void {
    if (!this.pointerLocked) void this.target.requestPointerLock();
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
    if (PREVENT_DEFAULT.has(e.code)) e.preventDefault();
    this.down.add(e.code);
    if (e.repeat) return;
    const list = this.pressHandlers.get(e.code);
    if (list) for (const fn of list) fn(e);
  };

  private onKeyUp = (e: KeyboardEvent) => {
    if (PREVENT_DEFAULT.has(e.code)) e.preventDefault();
    this.down.delete(e.code);
  };

  private onMouseMove = (e: MouseEvent) => {
    if (!this.pointerLocked) return;
    this.mouseDX += e.movementX;
    this.mouseDY += e.movementY;
  };
}
