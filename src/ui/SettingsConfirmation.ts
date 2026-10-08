import { t } from '../i18n';

/** 独立于缩放的设置确认顶层弹窗 */
export class SettingsConfirmation {
  private readonly dialog = document.createElement('dialog');
  private readonly events = new AbortController();
  private readonly deadline: number;
  private readonly timer: ReturnType<typeof setTimeout>;
  private readonly ticker: ReturnType<typeof setInterval>;
  private finished = false;

  constructor(private readonly keep: () => void, private readonly revert: () => void, timeoutMs = 12000) {
    this.deadline = Date.now() + timeoutMs;
    this.dialog.className = 'settings-confirmation';
    this.dialog.setAttribute('aria-labelledby', 'settings-confirmation-title');
    this.dialog.setAttribute('aria-describedby', 'settings-confirmation-countdown');
    this.dialog.innerHTML = `<h2 id="settings-confirmation-title">${t('settings.confirm.title')}</h2><p id="settings-confirmation-countdown" role="timer"></p><div class="confirmation-actions"><button class="secondary" data-revert autofocus>${t('settings.confirm.no')}</button><button class="primary" data-keep>${t('settings.confirm.yes')}</button></div>`;
    const signal = this.events.signal;
    this.dialog.querySelector('[data-keep]')!.addEventListener('click', () => this.finish(Date.now() < this.deadline), { signal });
    this.dialog.querySelector('[data-revert]')!.addEventListener('click', () => this.cancel(), { signal });
    this.dialog.addEventListener('cancel', event => { event.preventDefault(); this.cancel(); }, { signal });
    window.addEventListener('keydown', event => {
      if (event.code === 'Escape') { event.preventDefault(); event.stopImmediatePropagation(); this.cancel(); }
    }, { capture: true, signal });
    document.addEventListener('visibilitychange', () => this.updateCountdown(), { signal });
    window.addEventListener('pagehide', () => this.cancel(), { signal });
    document.body.append(this.dialog);
    this.dialog.showModal();
    this.timer = setTimeout(() => this.cancel(), timeoutMs);
    this.ticker = setInterval(() => this.updateCountdown(), 200);
    this.updateCountdown();
  }

  private updateCountdown(): void {
    const seconds = Math.max(0, Math.ceil((this.deadline - Date.now()) / 1000));
    if (seconds === 0) { this.cancel(); return; }
    this.dialog.querySelector('p')!.textContent = t('settings.confirm.countdown', { seconds });
  }
  private finish(confirmed: boolean): void {
    if (this.finished) return;
    this.finished = true;
    clearTimeout(this.timer); clearInterval(this.ticker); this.events.abort();
    this.dialog.close(); this.dialog.remove();
    if (confirmed) this.keep(); else this.revert();
  }
  cancel(): void { this.finish(false); }
}
