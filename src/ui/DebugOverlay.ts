/** F12 调试面板与实时监控 */
import { FLOW_DIRECTIONS } from '../systems/world/WorldGenerator';
import { CHUNK_SIZE } from '../core/config';
import type { Player } from '../entities/Player';
import { CHUNK_TYPES, formatChunkId, getChunkTypeDef, isRiverType } from '../systems/world/ChunkTypes';
import type { LoadedChunk, World } from '../systems/world/World';
import type { Game } from '../core/Game';
import { chunkName, seasonName } from '../i18n/content';
import { t } from '../i18n';
import { daysInMonth, gregorianToJdn, unixMsFromDayTime } from '../systems/calendar/JulianDay';
import { yuanCalendar } from '../systems/calendar/YuanCalendar';

const GRID = 5;
const HALF = Math.floor(GRID / 2);
const MAP_PX = GRID * CHUNK_SIZE; // 320

/** 画布配色 */
const INK_BG = '#241c13';
const INK_BG_EMPTY = '#2e2519';
const INK_TEXT_DIM = '#8a7a5f';
const CINNABAR_MARK = '#c2333f';
const CANVAS_FONT = '"Qianlin Song", "Songti SC", serif';

export class DebugOverlay {
  private readonly root: HTMLDivElement;
  private readonly canvas: HTMLCanvasElement;
  private readonly ctx: CanvasRenderingContext2D;
  /** 底图（地形 + 网格 + 类型标记） */
  private readonly base: HTMLCanvasElement;
  private readonly baseCtx: CanvasRenderingContext2D;
  private readonly typeEl: HTMLDivElement;
  private readonly infoEl: HTMLDivElement;
  private readonly aimEl: HTMLDivElement;
  private readonly titleEl: HTMLDivElement;
  private readonly legendEl: HTMLDivElement;
  private readonly calendarTextEl: HTMLDivElement;
  private readonly speedButtons: { el: HTMLButtonElement; labelKey: string }[] = [];
  private readonly jumpButton: HTMLButtonElement;
  private visible = false;
  private dirty = true;
  private mapStyle = { info: true, colors: false, relief: true };
  private centerCx = Number.NaN;
  private centerCz = Number.NaN;
  private lastTextUpdate = 0;

  constructor(parent: HTMLElement, private readonly world: World, private readonly player: Player, public readonly game: Game) {
    this.root = document.createElement('div');
    this.root.className = 'debug-overlay hidden';
    this.root.innerHTML = `
      <div class="debug-title"><span data-title></span><span class="debug-key">F12</span></div>
      <canvas class="debug-minimap" width="${MAP_PX}" height="${MAP_PX}"></canvas>
      <div class="debug-legend"></div>
      <div class="debug-type"></div>
      <div class="debug-info"></div>
      <div class="debug-aim"></div>
      <div class="debug-calendar">
        <div id="debug-calendar-text" class="debug-calendar-text"></div>
        <div class="debug-calendar-row">
          <button id="cal-pause" data-speed="pause"></button>
          <button id="cal-05x" data-speed="0.5">0.5x</button>
          <button id="cal-1x" data-speed="1">1x</button>
          <button id="cal-8x" data-speed="8">8x</button>
          <button id="cal-64x" data-speed="64">64x</button>
        </div>
        <div class="debug-calendar-row">
          <input id="cal-jump-date" type="text" inputmode="numeric" placeholder="YYYY-MM-DD" aria-label="YYYY-MM-DD">
          <input id="cal-jump-input" type="time" value="12:00">
          <button id="cal-jump-btn"></button>
        </div>
      </div>
    `;
    parent.appendChild(this.root);
    this.canvas = this.root.querySelector('canvas')!;
    this.ctx = this.canvas.getContext('2d')!;
    this.typeEl = this.root.querySelector('.debug-type')!;
    this.infoEl = this.root.querySelector('.debug-info')!;
    this.aimEl = this.root.querySelector('.debug-aim')!;
    this.titleEl = this.root.querySelector('[data-title]')!;
    this.legendEl = this.root.querySelector('.debug-legend')!;
    this.calendarTextEl = this.root.querySelector('#debug-calendar-text')!;
    this.jumpButton = this.root.querySelector<HTMLButtonElement>('#cal-jump-btn')!;
    this.base = document.createElement('canvas');
    this.base.width = this.base.height = MAP_PX;
    this.baseCtx = this.base.getContext('2d')!;

    const clock = this.game.calendarClock;
    // 0.5x/1x/8x/64x 是国际通用写法
    const ids = ['cal-pause', 'cal-05x', 'cal-1x', 'cal-8x', 'cal-64x'];
    ids.forEach(id => this.speedButtons.push({ el: this.root.querySelector<HTMLButtonElement>('#' + id)!, labelKey: id === 'cal-pause' ? 'debug.pause' : '' }));

    const updateSpeedBtns = () => {
      this.speedButtons.forEach(({ el }) => el.classList.remove('is-active'));
      if (clock.paused) {
        this.speedButtons[0].el.classList.add('is-active');
        return;
      }
      const speed = 86400 / clock.dayLengthSeconds;
      const activeId = speed === 1 ? 'cal-1x' : speed === 0.5 ? 'cal-05x' : speed === 8 ? 'cal-8x' : speed === 64 ? 'cal-64x' : '';
      if (activeId) this.root.querySelector('#' + activeId)!.classList.add('is-active');
    };

    this.speedButtons[0].el.addEventListener('click', () => {
      this.game.setTimeSpeed(this.game.timeSpeed === 0 ? 72 : 0);
      updateSpeedBtns();
    });
    const setSpeed = (mul: number) => {
      this.game.setTimeSpeed(mul);
      updateSpeedBtns();
    };
    this.root.querySelector('#cal-05x')!.addEventListener('click', () => setSpeed(0.5));
    this.root.querySelector('#cal-1x')!.addEventListener('click', () => setSpeed(1));
    this.root.querySelector('#cal-8x')!.addEventListener('click', () => setSpeed(8));
    this.root.querySelector('#cal-64x')!.addEventListener('click', () => setSpeed(64));

    this.root.querySelector('#cal-jump-btn')!.addEventListener('click', () => {
      const input = this.root.querySelector<HTMLInputElement>('#cal-jump-input')!;
      const dateInput = this.root.querySelector<HTMLInputElement>('#cal-jump-date')!;
      const parts = input.value.split(':');
      const h = Number(parts[0]);
      const m = Number(parts[1]);
      if (isNaN(h) || isNaN(m)) return;
      const dateRaw = dateInput.value.trim();
      if (dateRaw) {
        try {
          const match = /^(\d{1,4})-(\d{1,2})-(\d{1,2})$/.exec(dateRaw);
          if (!match) throw new Error('bad date');
          const year = Number(match[1]), month = Number(match[2]), day = Number(match[3]);
          const yuan = this.game.calendarType === 'yuan';
          if (year < 1 || year > 9999 || month < 1 || month > 12 || day < 1 || day > (yuan ? 30 : daysInMonth(year, month))) throw new Error('bad date');
          const jdn = yuan ? yuanCalendar.toJdn(year, month, day) : gregorianToJdn(year, month, day);
          clock.setUnixMs(unixMsFromDayTime(jdn, (h * 3600 + m * 60) / 86400, this.game.calendarSystem.utcOffsetMinutes));
          dateInput.setCustomValidity('');
        } catch {
          dateInput.setCustomValidity(t('debug.jump.invalid'));
          dateInput.reportValidity();
        }
        return;
      }
      const currentFrac = this.game.currentSnapshot.instant.frac;
      const currentMsSinceMidnight = currentFrac * 86400000;
      const targetMsSinceMidnight = (h * 3600 + m * 60) * 1000;
      const diff = targetMsSinceMidnight - currentMsSinceMidnight;

      let newUnixMs = clock.unixMs + diff;
      if (diff < 0) {
        newUnixMs += 86400000;
      }
      clock.setUnixMs(newUnixMs);
    });

    this.applyLocale();
    updateSpeedBtns();

    world.onChunkLoaded((c) => this.onChunkLoaded(c));
  }

  /** 语言变更后刷新静态文案（不重建画布 */
  applyLocale(): void {
    this.titleEl.textContent = t('debug.title');
    this.speedButtons.forEach(({ el, labelKey }) => { if (labelKey) el.textContent = t(labelKey); });
    this.jumpButton.textContent = t('debug.jump');
    this.dirty = true;
  }

  setMapStyle(style: { info: boolean; colors: boolean; relief: boolean }): void {
    this.mapStyle = style;
    this.dirty = true;
  }

  get isVisible(): boolean {
    return this.visible;
  }

  setKeyLabel(label: string): void { this.root.querySelector('.debug-key')!.textContent = label; }

  setVisible(v: boolean): void {
    this.visible = v;
    this.root.classList.toggle('hidden', !v);
    this.dirty = true;
  }

  toggle(): void {
    this.setVisible(!this.visible);
  }

  private onChunkLoaded(c: LoadedChunk): void {
    if (Math.abs(c.cx - this.centerCx) <= HALF && Math.abs(c.cz - this.centerCz) <= HALF) this.dirty = true;
  }

  update(now: number): void {
    if (!this.visible) return;
    const p = this.player;
    const pcx = Math.floor(p.x / CHUNK_SIZE);
    const pcz = Math.floor(p.z / CHUNK_SIZE);
    if (pcx !== this.centerCx || pcz !== this.centerCz) {
      this.centerCx = pcx;
      this.centerCz = pcz;
      this.dirty = true;
    }
    if (this.dirty) {
      this.redrawBase();
      this.dirty = false;
    }

    // 合成：底图 + 玩家箭头
    const ctx = this.ctx;
    ctx.globalCompositeOperation = 'source-over';
    ctx.drawImage(this.base, 0, 0);

    // 小地图不做昼夜调色，仅作地形参考
    const px = p.x - (pcx - HALF) * CHUNK_SIZE;
    const pz = p.z - (pcz - HALF) * CHUNK_SIZE;
    // 前方向量 (-sin yaw, -cos yaw)
    const angle = Math.atan2(-Math.cos(p.yaw), -Math.sin(p.yaw));
    ctx.save();
    ctx.translate(px, pz);
    ctx.rotate(angle);
    ctx.beginPath();
    ctx.moveTo(11, 0);
    ctx.lineTo(-7, -7);
    ctx.lineTo(-3, 0);
    ctx.lineTo(-7, 7);
    ctx.closePath();
    ctx.fillStyle = CINNABAR_MARK;
    ctx.strokeStyle = '#ffffff';
    ctx.lineWidth = 2;
    ctx.fill();
    ctx.stroke();
    ctx.restore();

    // 文本（10 Hz）
    if (now - this.lastTextUpdate > 100) {
      this.lastTextUpdate = now;
      const typeId = this.world.getChunkType(pcx, pcz);
      const def = getChunkTypeDef(typeId);
      const codeStr = formatChunkId(typeId);
      this.typeEl.innerHTML = `${t('debug.currentChunk')}<b style="color:${def.mapColor}">${codeStr} = ${chunkName(def)}</b>`;
      const mode = p.flying ? t('debug.mode.flying') : p.inWater ? t('debug.mode.water') : p.onGround ? t('debug.mode.ground') : t('debug.mode.air');
      this.infoEl.textContent = t('debug.info', {
        cx: pcx, cz: pcz, x: p.x.toFixed(1), y: p.y.toFixed(1), z: p.z.toFixed(1), mode,
      });
      this.aimEl.textContent = this.aimText();

      if (this.game && this.game.currentSnapshot) {
        const snap = this.game.currentSnapshot;
        const cal = this.game.calendarSystem;
        let text = '';
        const season = snap.season ? seasonName(snap.season.index) : '';
        const time = cal.gregorian.formatTime(snap.gregorian, true);
        if (this.game.calendarType === 'real') {
          const g = cal.gregorian.format(snap.gregorian);
          const c = cal.chinese.format(snap.chinese);
          const shichen = snap.shichen ? snap.shichen.label : '';
          // 换行符拼接文本
          text = t('debug.calendar.real', { gregorian: g, time, chinese: c, ganzhi: snap.chinese.dayGanZhi, shichen, season });
        } else {
          const y = cal.yuan.format(snap.yuan);
          text = t('debug.calendar.yuan', { yuan: y, time, season });
        }
        this.calendarTextEl.textContent = text;
      }
    }
  }

  private aimText(): string {
    const p = this.player;
    const cosPitch = Math.cos(p.pitch);
    const dx = -cosPitch * Math.sin(p.yaw), dy = Math.sin(p.pitch), dz = -cosPitch * Math.cos(p.yaw);
    for (let d = 1; d <= 256; d += 0.5) {
      const x = p.x + dx * d, y = p.eyeY + dy * d, z = p.z + dz * d;
      if (y > 512) break;
      const ground = this.world.getHeight(x, z);
      const water = this.world.getWaterLevel(x, z);
      if (y <= Math.max(ground, water)) {
        const cx = Math.floor(x / CHUNK_SIZE), cz = Math.floor(z / CHUNK_SIZE);
        const kind = y <= ground ? t('debug.aim.ground') : t('debug.aim.water');
        const name = chunkName(getChunkTypeDef(this.world.getChunkType(cx, cz)));
        return t('debug.aim.hit', { kind, name, cx, cz, distance: d.toFixed(1) });
      }
    }
    return t('debug.aim.none');
  }

  private redrawBase(): void {
    const ctx = this.baseCtx;
    // 临时请求流向，避免常规查询开销
    this.world.generator.displayFlowWanted = this.mapStyle.relief;
    try {
      this.drawBase(ctx);
    } finally {
      this.world.generator.displayFlowWanted = false;
    }
  }

  private drawBase(ctx: CanvasRenderingContext2D): void {
    ctx.fillStyle = INK_BG;
    ctx.fillRect(0, 0, MAP_PX, MAP_PX);
    const present = new Set<number>();

    for (let gz = 0; gz < GRID; gz++) {
      for (let gx = 0; gx < GRID; gx++) {
        const cx = this.centerCx - HALF + gx;
        const cz = this.centerCz - HALF + gz;
        const ox = gx * CHUNK_SIZE;
        const oz = gz * CHUNK_SIZE;
        const typeId = this.world.getChunkType(cx, cz);
        const def = getChunkTypeDef(typeId);
        present.add(typeId);
        const chunk = this.world.getChunk(cx, cz);
        if (this.mapStyle.colors) {
          ctx.fillStyle = def.mapColor;
          ctx.fillRect(ox, oz, CHUNK_SIZE, CHUNK_SIZE);
        } else if (chunk) {
          ctx.putImageData(chunk.minimap, ox, oz);
        } else {
          ctx.fillStyle = INK_BG_EMPTY;
          ctx.fillRect(ox, oz, CHUNK_SIZE, CHUNK_SIZE);
          ctx.fillStyle = INK_TEXT_DIM;
          ctx.font = `12px ${CANVAS_FONT}`;
          ctx.textAlign = 'center';
          ctx.textBaseline = 'middle';
          ctx.fillText(t('detail.loadingChunk'), ox + CHUNK_SIZE / 2, oz + CHUNK_SIZE / 2);
        }

        // 区块类型角标
        if (this.mapStyle.info) {
          const codeStr = formatChunkId(typeId);
          ctx.fillStyle = 'rgba(0,0,0,0.65)';
          ctx.fillRect(ox + 2, oz + 2, 28, 15);
          ctx.fillStyle = def.mapColor;
          ctx.fillRect(ox + 4, oz + 4, 3, 11);
          ctx.fillStyle = '#ffffff';
          ctx.font = `bold 10px ${CANVAS_FONT}`;
          ctx.textAlign = 'left';
          ctx.textBaseline = 'middle';
          ctx.fillText(codeStr, ox + 9, oz + 10);
        }
        if (this.mapStyle.relief) {
          const info = this.world.generator.getChunkInfo(cx, cz);
          if (isRiverType(typeId) && info.displayFlow >= 0) {
            ctx.font = `bold 22px ${CANVAS_FONT}`;
            ctx.textAlign = 'center';
            ctx.strokeStyle = '#164b73'; ctx.lineWidth = 3;
            ctx.fillStyle = '#ffffff';
            ctx.strokeText(FLOW_DIRECTIONS[info.displayFlow].arrow, ox + 32, oz + 35);
            ctx.fillText(FLOW_DIRECTIONS[info.displayFlow].arrow, ox + 32, oz + 35);
          }
        }
      }
    }

    this.legendEl.innerHTML = CHUNK_TYPES.filter((def) => def.id !== 0 && present.has(def.id))
      .map((def) => `<span><i style="background:${def.mapColor}"></i>${formatChunkId(def.id)} ${chunkName(def)}</span>`)
      .join('');

    // 区块网格
    ctx.strokeStyle = 'rgba(0,0,0,0.45)';
    ctx.lineWidth = 1;
    ctx.beginPath();
    for (let i = 1; i < GRID; i++) {
      ctx.moveTo(i * CHUNK_SIZE + 0.5, 0);
      ctx.lineTo(i * CHUNK_SIZE + 0.5, MAP_PX);
      ctx.moveTo(0, i * CHUNK_SIZE + 0.5);
      ctx.lineTo(MAP_PX, i * CHUNK_SIZE + 0.5);
    }
    ctx.stroke();

    // 当前区块高亮
    ctx.strokeStyle = 'rgba(255,255,255,0.9)';
    ctx.lineWidth = 2;
    ctx.strokeRect(HALF * CHUNK_SIZE + 1, HALF * CHUNK_SIZE + 1, CHUNK_SIZE - 2, CHUNK_SIZE - 2);

    // 北向标记
    ctx.fillStyle = 'rgba(0,0,0,0.55)';
    ctx.fillRect(MAP_PX - 20, 2, 18, 16);
    ctx.fillStyle = '#ffffff';
    ctx.font = `bold 11px ${CANVAS_FONT}`;
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText(t('debug.north'), MAP_PX - 11, 10);
  }
}
