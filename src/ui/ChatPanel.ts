import { onLocaleChange, t } from '../i18n';

export interface ChatMessage {
  author: string;
  text: string;
  player?: boolean;
  color?: string;
}

export class ChatPanel {
  readonly element = document.createElement('section');
  private readonly history = document.createElement('div');
  private readonly input = document.createElement('input');
  private readonly clearButton = document.createElement('button');
  private readonly messages: ChatMessage[] = [];
  private readonly stopLocaleWatch: () => void;
  private timer: ReturnType<typeof setTimeout> | undefined;
  private sidebar = false;
  private lastMessageAt = 0;

  constructor(parent: HTMLElement, private readonly playerName: string, private readonly playerColor: string | undefined,
    private readonly command: (value: string) => string, private readonly editingChanged: (editing: boolean) => void = () => {}) {
    this.element.className = 'chat-panel chat-idle';
    this.history.className = 'chat-history';
    this.history.setAttribute('role', 'log');
    this.history.setAttribute('aria-live', 'polite');
    this.history.setAttribute('aria-relevant', 'additions');
    this.input.type = 'text'; this.input.maxLength = 1000;
    this.input.autocomplete = 'off'; this.input.spellcheck = false;
    const form = document.createElement('form'); form.className = 'chat-composer';
    form.append(this.input);
    this.clearButton.type = 'button'; this.clearButton.className = 'chat-clear secondary';
    this.element.append(form, this.history, this.clearButton); parent.append(this.element);
    form.addEventListener('submit', event => { event.preventDefault(); this.submit(); });
    this.input.addEventListener('focus', () => editingChanged(true));
    this.input.addEventListener('blur', () => editingChanged(false));
    this.clearButton.addEventListener('click', () => {
      this.messages.length = 0; this.history.replaceChildren(); this.lastMessageAt = 0;
      this.scheduleHide();
    });
    this.input.addEventListener('keydown', event => {
      if (event.isComposing) return;
      if (event.code !== 'Escape') event.stopPropagation();
    });
    this.stopLocaleWatch = onLocaleChange(() => this.applyLocale());
    this.applyLocale();
  }

  private applyLocale(): void {
    this.input.placeholder = t('chat.placeholder');
    this.input.setAttribute('aria-label', t('chat.input'));
    this.clearButton.textContent = t('chat.clear');
    this.element.setAttribute('aria-label', t('chat.title'));
  }

  get editing(): boolean { return document.activeElement === this.input; }

  focus(prefix = ''): void {
    if (prefix && !this.input.value) this.input.value = prefix;
    this.input.focus({ preventScroll: true });
    this.editingChanged(true);
  }

  setSidebar(open: boolean): void {
    this.sidebar = open;
    this.element.classList.toggle('sidebar-open', open);
    if (!open) { this.input.blur(); this.editingChanged(false); }
    this.scheduleHide();
  }

  addMessage(message: ChatMessage): void {
    this.messages.unshift(message);
    this.messages.length = Math.min(this.messages.length, 63);
    const row = document.createElement('p'); row.className = 'chat-message';
    const author = document.createElement('span'); author.className = 'chat-author';
    author.textContent = message.author;
    if (message.player) {
      author.classList.add('chat-player');
      if (message.color) author.style.color = message.color;
    }
    row.append(author, document.createTextNode(`: ${message.text}`));
    this.history.prepend(row);
    while (this.history.children.length > 63) this.history.lastElementChild!.remove();
    this.history.scrollTop = 0;
    this.lastMessageAt = Date.now(); this.scheduleHide();
  }

  private submit(): void {
    const value = this.input.value.trim();
    if (!value) return;
    this.input.value = '';
    this.addMessage({ author: this.playerName, text: value, player: true, color: this.playerColor });
    if (value.startsWith('/')) {
      let response: string;
      try { response = this.command(value); }
      catch { response = 'Command failed.'; }
      this.addMessage({ author: 'System', text: response });
    }
  }

  private scheduleHide(): void {
    clearTimeout(this.timer);
    const remaining = this.lastMessageAt + 30000 - Date.now();
    this.element.classList.toggle('chat-idle', !this.sidebar && (!this.messages.length || remaining <= 0));
    if (!this.sidebar && this.messages.length && remaining > 0) {
      this.timer = setTimeout(() => this.scheduleHide(), remaining);
    }
  }

  destroy(): void { clearTimeout(this.timer); this.stopLocaleWatch(); this.element.remove(); }
}
