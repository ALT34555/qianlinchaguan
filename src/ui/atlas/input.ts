// 舆图输入与交互控制

export interface AtlasInputHost {
  readonly active: boolean;
  getChunkAtPoint(x: number, y: number, requestInfo?: boolean): { cx: number; cz: number } | null;
  onHover(chunk: { cx: number; cz: number } | null): void;
  onSelect(chunk: { cx: number; cz: number }, clientX: number, clientY: number): void;
  onActivate(chunk: { cx: number; cz: number }): void;
  onDestination(x: number, y: number, chunk: { cx: number; cz: number }): void;
  panBy(dx: number, dy: number): void;
  zoomBy(factor: number, clientX?: number, clientY?: number): void;
  requestRender(): void;
  setCursor(cursor: string): void;
}

export class AtlasInputController {
  private readonly canvas: HTMLCanvasElement;
  private readonly host: AtlasInputHost;
  private readonly events = new AbortController();

  private isDragging = false;
  private dragStartX = 0;
  private dragStartY = 0;
  private lastDragX = 0;
  private lastDragY = 0;
  private hasMoved = false;

  private touchStartDist = 0;
  private lastTap: { time: number; x: number; y: number } | null = null;

  constructor(canvas: HTMLCanvasElement, host: AtlasInputHost) {
    this.canvas = canvas;
    this.host = host;
    this.initEvents();
  }

  private initEvents(): void {
    const canvas = this.canvas;
    const signal = this.events.signal;

    canvas.addEventListener('mousedown', (e) => {
      if (!this.host.active || e.button !== 0) return;
      this.isDragging = true;
      this.hasMoved = false;
      this.dragStartX = e.clientX;
      this.dragStartY = e.clientY;
      this.lastDragX = e.clientX;
      this.lastDragY = e.clientY;
      this.host.setCursor('grabbing');
    }, { signal });

    window.addEventListener('mousemove', (e) => {
      if (!this.host.active) return;
      if (this.isDragging) {
        const dx = e.clientX - this.lastDragX;
        const dy = e.clientY - this.lastDragY;
        if (Math.hypot(e.clientX - this.dragStartX, e.clientY - this.dragStartY) > 3) {
          this.hasMoved = true;
        }
        this.host.panBy(dx, dy);
        this.lastDragX = e.clientX;
        this.lastDragY = e.clientY;
        this.host.requestRender();
      } else {
        const rect = canvas.getBoundingClientRect();
        if (
          e.clientX >= rect.left && e.clientX <= rect.right &&
          e.clientY >= rect.top && e.clientY <= rect.bottom
        ) {
          const chunk = this.host.getChunkAtPoint(e.clientX - rect.left, e.clientY - rect.top, false);
          this.host.onHover(chunk);
        } else {
          this.host.onHover(null);
        }
      }
    }, { signal });

    window.addEventListener('mouseup', (e) => {
      if (!this.host.active || !this.isDragging) return;
      this.isDragging = false;
      this.host.setCursor('grab');
      this.host.requestRender();

      if (!this.hasMoved) {
        const rect = canvas.getBoundingClientRect();
        if (
          e.clientX >= rect.left && e.clientX <= rect.right &&
          e.clientY >= rect.top && e.clientY <= rect.bottom
        ) {
          const x = e.clientX - rect.left;
          const y = e.clientY - rect.top;
          const chunk = this.host.getChunkAtPoint(x, y);
          if (chunk) {
            this.host.onSelect(chunk, e.clientX, e.clientY);
            this.host.onDestination(x, y, chunk);
            this.host.requestRender();
          }
        }
      }
    }, { signal });

    canvas.addEventListener('wheel', (e) => {
      if (!this.host.active) return;
      e.preventDefault();
      const factor = e.deltaY < 0 ? 1.15 : 0.87;
      this.host.zoomBy(factor, e.clientX, e.clientY);
    }, { passive: false, signal });

    canvas.addEventListener('dblclick', (e) => {
      if (!this.host.active) return;
      const rect = canvas.getBoundingClientRect();
      const chunk = this.host.getChunkAtPoint(e.clientX - rect.left, e.clientY - rect.top);
      if (chunk) this.host.onActivate(chunk);
    }, { signal });

    canvas.addEventListener('touchstart', (e) => {
      if (!this.host.active) return;
      if (e.touches.length === 1) {
        this.isDragging = true;
        this.hasMoved = false;
        this.dragStartX = e.touches[0].clientX;
        this.dragStartY = e.touches[0].clientY;
        this.lastDragX = e.touches[0].clientX;
        this.lastDragY = e.touches[0].clientY;
      } else if (e.touches.length === 2) {
        this.isDragging = false;
        this.lastTap = null;
        this.hasMoved = true;
        this.touchStartDist = Math.hypot(
          e.touches[0].clientX - e.touches[1].clientX,
          e.touches[0].clientY - e.touches[1].clientY
        );
      }
    }, { passive: true, signal });

    canvas.addEventListener('touchmove', (e) => {
      if (!this.host.active) return;
      if (e.touches.length === 1 && this.isDragging) {
        const dx = e.touches[0].clientX - this.lastDragX;
        const dy = e.touches[0].clientY - this.lastDragY;
        this.host.panBy(dx, dy);
        this.lastDragX = e.touches[0].clientX;
        this.lastDragY = e.touches[0].clientY;
        if (Math.hypot(e.touches[0].clientX - this.dragStartX, e.touches[0].clientY - this.dragStartY) > 8) {
          this.hasMoved = true;
        }
        this.host.requestRender();
      } else if (e.touches.length === 2 && this.touchStartDist > 0) {
        const dist = Math.hypot(
          e.touches[0].clientX - e.touches[1].clientX,
          e.touches[0].clientY - e.touches[1].clientY
        );
        const factor = dist / this.touchStartDist;
        this.touchStartDist = dist;
        const midX = (e.touches[0].clientX + e.touches[1].clientX) / 2;
        const midY = (e.touches[0].clientY + e.touches[1].clientY) / 2;
        this.host.zoomBy(factor, midX, midY);
      }
    }, { passive: true, signal });

    canvas.addEventListener('touchend', (e) => {
      if (!this.host.active) return;
      if (this.isDragging && !this.hasMoved && e.changedTouches.length === 1) {
        const rect = canvas.getBoundingClientRect();
        const t = e.changedTouches[0];
        const x = t.clientX - rect.left;
        const y = t.clientY - rect.top;
        const chunk = this.host.getChunkAtPoint(x, y);
        if (chunk) {
          this.host.onSelect(chunk, t.clientX, t.clientY);
          this.host.onDestination(x, y, chunk);
          const now = performance.now();
          if (this.lastTap && now - this.lastTap.time < 350 && Math.hypot(t.clientX - this.lastTap.x, t.clientY - this.lastTap.y) < 24) {
            this.lastTap = null;
            this.host.onActivate(chunk);
          } else {
            this.lastTap = { time: now, x: t.clientX, y: t.clientY };
          }
          this.host.requestRender();
        }
      }
      this.isDragging = false;
      this.touchStartDist = 0;
      this.host.requestRender();
    }, { passive: true, signal });

    canvas.addEventListener('touchcancel', () => {
      this.isDragging = false;
      this.hasMoved = true;
      this.touchStartDist = 0;
      this.lastTap = null;
      this.host.requestRender();
    }, { signal });
  }

  get dragging(): boolean {
    return this.isDragging;
  }

  reset(): void {
    this.isDragging = false;
    this.hasMoved = false;
    this.touchStartDist = 0;
    this.lastTap = null;
  }

  dispose(): void {
    this.events.abort();
    this.reset();
  }
}
