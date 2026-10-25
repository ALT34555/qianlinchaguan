export type ButtonKind = 'primary' | 'ghost' | 'quiet' | 'danger' | 'icon' | 'segment' | 'bookmark' | 'rail' | 'mode' | 'theme' | 'gamepad';

/** 按语义选择设计稿按钮类型 */
export function buttonKind(button: HTMLButtonElement): ButtonKind {
  const explicit = button.dataset.buttonKind as ButtonKind | undefined;
  if (explicit) return explicit;
  if (button.matches('.touch-action-btn, .dpad-btn, .oct-cell-btn, [data-dir]')) return 'gamepad';
  if (button.matches('.world-bookmark-tabs button, .save-import-bookmark, .save-filter-bookmark')) return 'bookmark';
  if (button.matches('.q-tab')) return 'rail';
  if (button.matches('.q-mode')) return 'mode';
  if (button.matches('.theme-pill, .q-theme-toggle')) return 'theme';
  if (button.matches('.btn--danger, .danger, [data-delete], [data-remove]')) return 'danger';
  if (button.matches('.btn--primary, .primary')) return 'primary';
  if (button.matches('.btn--quiet, .text-button, .q-text-btn')) return 'quiet';
  if (button.matches('.seg button, .preset-pill, .climate-pill, .qf-btn, [data-crosshair], [data-joystick-choice], [data-speed-preset]')) return 'segment';
  if (button.matches('.icon-btn, .corner-menu, #random-seed, .q-rail-logo')) return 'icon';
  return 'ghost';
}

/** 动态创建的按钮也使用同一规范 */
export function initButtons(root: HTMLElement = document.body): () => void {
  const decorate = (element: Element) => {
    if (element instanceof HTMLButtonElement) element.dataset.uiButton = buttonKind(element);
    element.querySelectorAll<HTMLButtonElement>('button').forEach(button => { button.dataset.uiButton = buttonKind(button); });
  };
  decorate(root);
  const observer = new MutationObserver(records => {
    for (const record of records) {
      if (record.type === 'attributes') decorate(record.target as Element);
      else for (const node of record.addedNodes) if (node instanceof Element) decorate(node);
    }
  });
  observer.observe(root, { childList: true, subtree: true, attributes: true, attributeFilter: ['class', 'data-button-kind'] });
  return () => observer.disconnect();
}
