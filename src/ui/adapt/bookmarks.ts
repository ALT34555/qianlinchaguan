const BOOKMARK_ART = `<svg class="q-bookmark-art" viewBox="0 0 112 44" preserveAspectRatio="none" aria-hidden="true">
  <g class="q-bookmark-top"><path class="q-bookmark-rim" d="M1 1H111V33L56 28L1 33Z"/><path class="q-bookmark-paper" d="M3 3H109V30L56 25L3 30Z"/><path class="q-bookmark-cord" d="M56 28V43M54 41L53 43M58 41L59 43"/><circle class="q-bookmark-bead" cx="56" cy="37" r="2"/></g>
  <g class="q-bookmark-side"><path class="q-bookmark-rim" d="M1 1H95L88 22L95 43H1Z"/><path class="q-bookmark-paper" d="M3 3H92L85 22L92 41H3Z"/><path class="q-bookmark-cord" d="M90 22H111M108 20L111 19M108 24L111 25"/><circle class="q-bookmark-bead" cx="104" cy="22" r="2"/></g>
</svg>`;

/** 书签按可用宽高切换方向 */
export function fitBookmarks(panel: HTMLElement): () => void {
  panel.classList.add('q-bookmark-panel');
  panel.querySelectorAll('.save-filter-bookmark, .world-bookmark-tabs button').forEach(button => {
    if (!button.querySelector('.q-bookmark-art')) button.insertAdjacentHTML('afterbegin', BOOKMARK_ART);
  });
  const control = panel.querySelector<HTMLElement>('.save-filter-bookmark, .world-bookmark-tabs button');
  const update = () => {
    const rect = panel.getBoundingClientRect();
    const scroller = panel.closest<HTMLElement>('#world-menu, .pause-settings');
    const top = rect.top + (scroller?.scrollTop ?? 0);
    const sheet = panel.classList.contains('world-settings') ? panel.closest<HTMLElement>('.q-sheet') : null;
    const sheetStyle = sheet ? getComputedStyle(sheet) : null;
    const sheetInset = sheetStyle ? parseFloat(sheetStyle.paddingBottom) + parseFloat(sheetStyle.borderBottomWidth) : 0;
    const bottom = sheet && scroller ? scroller.getBoundingClientRect().bottom - sheetInset : window.innerHeight - 32;
    const height = Math.max(240, bottom - top);
    if (panel.classList.contains('world-settings')) panel.style.setProperty('--q-creator-height', `${height}px`);
    const width = control?.getBoundingClientRect().width ?? 112;
    const sideHeight = scroller?.classList.contains('pause-settings') ? rect.height : height;
    const side = rect.width >= width + 420 && rect.width / sideHeight >= 1.35;
    panel.dataset.bookmarkSide = side ? 'left' : 'top';
    panel.querySelector('.world-bookmark-tabs')?.setAttribute('aria-orientation', side ? 'vertical' : 'horizontal');
  };
  const observer = new ResizeObserver(update);
  observer.observe(panel);
  if (control) observer.observe(control);
  window.addEventListener('resize', update);
  update();
  return () => {
    observer.disconnect();
    window.removeEventListener('resize', update);
  };
}
