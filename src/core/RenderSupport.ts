export type RenderFailure = 'unavailable' | 'failed' | 'lost';

export function probeWebGL2(): boolean {
  if (typeof document === 'undefined') return false;
  const canvas = document.createElement('canvas');
  try {
    const context = canvas.getContext('webgl2');
    if (!context) return false;
    context.getExtension('WEBGL_lose_context')?.loseContext();
    return true;
  } catch { return false; }
}

export class RenderSupport {
  private checked = false;
  private failure: RenderFailure | null = null;
  constructor(private readonly probe = probeWebGL2) {}
  get reason(): RenderFailure | null {
    if (!this.checked) {
      this.checked = true;
      try { if (!this.probe()) this.failure = 'unavailable'; }
      catch { this.failure = 'unavailable'; }
    }
    return this.failure;
  }
  get available(): boolean { return this.reason === null; }
  disable(reason: RenderFailure): void { this.checked = true; this.failure = reason; }
}

export const renderSupport = new RenderSupport();
