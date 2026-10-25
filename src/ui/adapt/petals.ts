// 封面花瓣：canvas 贝塞尔花瓣
interface Petal {
  x: number; y: number; size: number; angle: number;
  spin: number; vy: number; vx: number; alpha: number; color: string;
}

export function startPetals(canvas: HTMLCanvasElement): () => void {
  if (matchMedia('(prefers-reduced-motion: reduce)').matches) return () => {};
  const ctx = canvas.getContext('2d');
  if (!ctx) return () => {};
  let width = 0, height = 0, raf = 0;
  let petals: Petal[] = [];

  const palette = (): string[] => {
    const css = getComputedStyle(document.documentElement);
    return ['--q-petal', '--q-celadon', '--q-primary-ui']
      .map(name => css.getPropertyValue(name).trim() || '#f0a9b8');
  };

  const resize = (): void => {
    const rect = canvas.getBoundingClientRect();
    const dpr = Math.min(2, devicePixelRatio || 1);
    width = rect.width; height = rect.height;
    canvas.width = Math.max(1, width * dpr);
    canvas.height = Math.max(1, height * dpr);
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  };

  const seed = (): void => {
    const colors = palette();
    petals = Array.from({ length: 16 }, () => ({
      x: Math.random() * width,
      y: Math.random() * height,
      size: 3 + Math.random() * 4.4,
      angle: Math.random() * Math.PI * 2,
      spin: (Math.random() - 0.5) * 0.012,
      vy: 0.16 + Math.random() * 0.24,
      vx: (Math.random() - 0.5) * 0.22,
      alpha: 0.16 + Math.random() * 0.24,
      color: colors[Math.floor(Math.random() * colors.length)],
    }));
  };

  const draw = (petal: Petal): void => {
    const { x, y, size, angle } = petal;
    ctx.save();
    ctx.translate(x, y);
    ctx.rotate(angle);
    ctx.globalAlpha = petal.alpha;
    ctx.fillStyle = petal.color;
    ctx.beginPath();
    ctx.moveTo(0, -size);
    ctx.bezierCurveTo(size * 0.92, -size * 0.46, size * 0.62, size * 0.5, 0, size);
    ctx.bezierCurveTo(-size * 0.62, size * 0.5, -size * 0.92, -size * 0.46, 0, -size);
    ctx.fill();
    ctx.restore();
  };

  const loop = (): void => {
    ctx.clearRect(0, 0, width, height);
    petals.forEach(petal => {
      petal.x += petal.vx + Math.sin(petal.y * 0.01) * 0.14;
      petal.y += petal.vy;
      petal.angle += petal.spin;
      if (petal.y > height + 20) { petal.y = -20; petal.x = Math.random() * width; }
      if (petal.x > width + 20) petal.x = -20;
      if (petal.x < -20) petal.x = width + 20;
      draw(petal);
    });
    raf = requestAnimationFrame(loop);
  };

  const onResize = (): void => { resize(); seed(); };
  const onVisibility = (): void => {
    cancelAnimationFrame(raf);
    if (!document.hidden) loop();
  };

  resize(); seed(); loop();
  addEventListener('resize', onResize);
  document.addEventListener('visibilitychange', onVisibility);

  return () => {
    cancelAnimationFrame(raf);
    removeEventListener('resize', onResize);
    document.removeEventListener('visibilitychange', onVisibility);
  };
}
