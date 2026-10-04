/**
 * F12 调试面板（右上角）：
 *  - 小地图：以玩家所在区块为中心的 5x5 = 25 个区块，北（-Z）朝上
 *  - 当前区块类型
 */
import { FLOW_DIRECTIONS } from '../systems/world/WorldGenerator';
import { CHUNK_SIZE } from '../core/config';
import type { Player } from '../entities/Player';
import { CHUNK_TYPES, formatChunkId, getChunkTypeDef } from '../systems/world/ChunkTypes';
import type { LoadedChunk, World } from '../systems/world/World';
import type { Game } from '../core/Game';

const GRID = 5;
const HALF = Math.floor(GRID / 2);
const MAP_PX = GRID * CHUNK_SIZE; // 320

export class DebugOverlay {
  private readonly root: HTMLDivElement;
  private readonly canvas: HTMLCanvasElement;
  private readonly ctx: CanvasRenderingContext2D;
  /** 底图（地形 + 网格 + 类型标记），仅在中心区块变化或有新区块加载时重绘 */
  private readonly base: HTMLCanvasElement;
  private readonly baseCtx: CanvasRenderingContext2D;
  private readonly typeEl: HTMLDivElement;
  private readonly infoEl: HTMLDivElement;
  private visible = false;
  private dirty = true;
  private centerCx = Number.NaN;
  private centerCz = Number.NaN;
  private lastTextUpdate = 0;

  constructor(parent: HTMLElement, private readonly world: World, private readonly player: Player, public readonly game: Game) {
    this.root = document.createElement('div');
    this.root.className = 'debug-overlay hidden';
    this.root.innerHTML = `
      <div class="debug-title">调试信息 <span class="debug-key">F12</span></div>
      <canvas class="debug-minimap" width="${MAP_PX}" height="${MAP_PX}"></canvas>
      <div class="debug-legend">${CHUNK_TYPES.filter((t) => t.id !== 0)
        .map((t) => `<span><i style="background:${t.mapColor}"></i>${formatChunkId(t.id)} ${t.name}</span>`)
        .join('')}</div>
      <div class="debug-type"></div>
      <div class="debug-info"></div>
    
      <div class="debug-calendar" style="margin-top:10px; font-size:12px; border-top:1px solid #444; padding-top:10px;">
        <div id="debug-calendar-text" style="margin-bottom:5px; white-space: pre-wrap; line-height: 1.4;"></div>
        <div style="display:flex; gap:5px; align-items:center; flex-wrap: wrap;">
          <button id="cal-pause" style="padding:2px 5px; cursor: pointer;">暂停</button>
          <button id="cal-05x" style="padding:2px 5px; cursor: pointer;">0.5x</button>
          <button id="cal-1x" style="padding:2px 5px; cursor: pointer;">1x</button>
          <button id="cal-8x" style="padding:2px 5px; cursor: pointer;">8x</button>
          <button id="cal-64x" style="padding:2px 5px; cursor: pointer;">64x</button>
        </div>
        <div style="display:flex; gap:5px; align-items:center; margin-top:5px;">
          <input id="cal-jump-input" type="time" style="padding:1px; font-size:11px; background: #333; color: white; border: 1px solid #666;" value="12:00">
          <button id="cal-jump-btn" style="padding:2px 5px; cursor: pointer;">跳转指定时间</button>
        </div>
      </div>
    `;
    parent.appendChild(this.root);
    this.canvas = this.root.querySelector('canvas')!;
    this.ctx = this.canvas.getContext('2d')!;
    this.typeEl = this.root.querySelector('.debug-type')!;
    this.infoEl = this.root.querySelector('.debug-info')!;
    this.base = document.createElement('canvas');
    this.base.width = this.base.height = MAP_PX;
    this.baseCtx = this.base.getContext('2d')!;

    const clock = this.game.calendarClock;
    const updateSpeedBtns = () => {
      ['cal-pause', 'cal-05x', 'cal-1x', 'cal-8x', 'cal-64x'].forEach(id => {
        this.root.querySelector('#' + id)!.setAttribute('style', 'padding:2px 5px; cursor:pointer;');
      });
      if (clock.paused) {
        this.root.querySelector('#cal-pause')!.setAttribute('style', 'padding:2px 5px; cursor:pointer; background: #666; color: white;');
      } else {
        const speed = 86400 / clock.dayLengthSeconds;
        let activeId = 'cal-1x';
        if (speed === 0.5) activeId = 'cal-05x';
        else if (speed === 8) activeId = 'cal-8x';
        else if (speed === 64) activeId = 'cal-64x';
        this.root.querySelector('#' + activeId)!.setAttribute('style', 'padding:2px 5px; cursor:pointer; background: #666; color: white;');
      }
    };

    this.root.querySelector('#cal-pause')!.addEventListener('click', () => {
      clock.paused = !clock.paused;
      updateSpeedBtns();
    });
    const setSpeed = (mul: number) => {
      clock.paused = false;
      clock.dayLengthSeconds = 86400 / mul;
      updateSpeedBtns();
    };
    this.root.querySelector('#cal-05x')!.addEventListener('click', () => setSpeed(0.5));
    this.root.querySelector('#cal-1x')!.addEventListener('click', () => setSpeed(1));
    this.root.querySelector('#cal-8x')!.addEventListener('click', () => setSpeed(8));
    this.root.querySelector('#cal-64x')!.addEventListener('click', () => setSpeed(64));
    
    this.root.querySelector('#cal-jump-btn')!.addEventListener('click', () => {
      const input = this.root.querySelector<HTMLInputElement>('#cal-jump-input')!;
      const parts = input.value.split(':');
      const h = Number(parts[0]);
      const m = Number(parts[1]);
      if (!isNaN(h) && !isNaN(m)) {
        const currentFrac = this.game.currentSnapshot.instant.frac;
        const currentMsSinceMidnight = currentFrac * 86400000;
        const targetMsSinceMidnight = (h * 3600 + m * 60) * 1000;
        const diff = targetMsSinceMidnight - currentMsSinceMidnight;
        
        let newUnixMs = clock.unixMs + diff;
        if (diff < 0) {
          newUnixMs += 86400000;
        }
        clock.setUnixMs(newUnixMs);
      }
    });

    setTimeout(updateSpeedBtns, 100);

    world.onChunkLoaded((c) => this.onChunkLoaded(c));
  }

  get isVisible(): boolean {
    return this.visible;
  }

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
    
    // 季节与昼夜色调 (地图)
    if (this.game && this.game.currentSnapshot) {
      const season = this.game.currentSeason;
      const timeToMidnight = Math.abs(this.game.currentDayRatio - 0.5);
      const brightness = Math.max(0.2, Math.min(1.0, timeToMidnight * 2 + 0.1));
      
      ctx.globalCompositeOperation = 'multiply';
      let r = 255, g = 255, b = 255;
      if (season === 0) { r = 212; g = 255; b = 212; }
      else if (season === 1) { r = 255; g = 212; b = 212; }
      else if (season === 2) { r = 255; g = 255; b = 212; }
      else if (season === 3) { r = 212; g = 212; b = 255; }
      r *= brightness; g *= brightness; b *= brightness;
      ctx.fillStyle = "rgb(" + Math.round(r) + "," + Math.round(g) + "," + Math.round(b) + ")";
      ctx.fillRect(0, 0, MAP_PX, MAP_PX);
      ctx.globalCompositeOperation = 'source-over';
    }
    const px = p.x - (pcx - HALF) * CHUNK_SIZE;
    const pz = p.z - (pcz - HALF) * CHUNK_SIZE;
    // 前方向量 (-sin yaw, -cos yaw)，画布 y 轴对应世界 +Z
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
    ctx.fillStyle = '#ff3b30';
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
      this.typeEl.innerHTML = `当前区块类型：<b style="color:${def.mapColor}">${codeStr} = ${def.name}</b>`;
      const mode = p.flying ? '浮空' : p.inWater ? '水中' : p.onGround ? '地面' : '空中';
      this.infoEl.textContent =
        `区块 (${pcx}, ${pcz})  ·  坐标 ${p.x.toFixed(1)}, ${p.y.toFixed(1)}, ${p.z.toFixed(1)}  ·  ${mode}`;
        
      if (this.game && this.game.currentSnapshot) {
        const snap = this.game.currentSnapshot;
        const cal = this.game.calendarSystem;
        let text = "";
        if (this.game.calendarType === "real") {
          const g = cal.gregorian.format(snap.gregorian);
          const c = cal.chinese.format(snap.chinese);
          const time = cal.gregorian.formatTime(snap.gregorian, true);
          const shichen = snap.shichen ? snap.shichen.label : "";
          const season = snap.season ? snap.season.name : "";
          text = `公历：${g} ${time}n农历：${c} ${snap.chinese.dayGanZhi}日n时辰：${shichen}  季节：${season}`;
        } else {
          const y = cal.yuan.format(snap.yuan);
          const time = cal.gregorian.formatTime(snap.gregorian, true);
          const season = snap.season ? snap.season.name : "";
          text = `元历：${y}n时间：${time}n季节：${season}`;
        }
        this.root.querySelector("#debug-calendar-text")!.textContent = text;
      }
    }
  }

  private redrawBase(): void {
    const ctx = this.baseCtx;
    ctx.fillStyle = '#1b1d22';
    ctx.fillRect(0, 0, MAP_PX, MAP_PX);

    for (let gz = 0; gz < GRID; gz++) {
      for (let gx = 0; gx < GRID; gx++) {
        const cx = this.centerCx - HALF + gx;
        const cz = this.centerCz - HALF + gz;
        const ox = gx * CHUNK_SIZE;
        const oz = gz * CHUNK_SIZE;
        const chunk = this.world.getChunk(cx, cz);
        if (chunk) {
          ctx.putImageData(chunk.minimap, ox, oz);
        } else {
          ctx.fillStyle = '#2a2d34';
          ctx.fillRect(ox, oz, CHUNK_SIZE, CHUNK_SIZE);
          ctx.fillStyle = '#6b7080';
          ctx.font = '12px sans-serif';
          ctx.textAlign = 'center';
          ctx.textBaseline = 'middle';
          ctx.fillText('加载中', ox + CHUNK_SIZE / 2, oz + CHUNK_SIZE / 2);
        }

        // 区块类型角标
        const typeId = this.world.getChunkType(cx, cz);
        const def = getChunkTypeDef(typeId);
        const codeStr = formatChunkId(typeId);
        ctx.fillStyle = 'rgba(0,0,0,0.65)';
        ctx.fillRect(ox + 2, oz + 2, 28, 15);
        ctx.fillStyle = def.mapColor;
        ctx.fillRect(ox + 4, oz + 4, 3, 11);
        ctx.fillStyle = '#ffffff';
        ctx.font = 'bold 10px sans-serif';
        ctx.textAlign = 'left';
        ctx.textBaseline = 'middle';
        ctx.fillText(codeStr, ox + 9, oz + 10);
        const info = this.world.generator.getChunkInfo(cx, cz);
        if (typeId === 5 && info.flow >= 0) {
          ctx.font = 'bold 22px sans-serif';
          ctx.textAlign = 'center';
          ctx.strokeStyle = '#164b73'; ctx.lineWidth = 3;
          ctx.strokeText(FLOW_DIRECTIONS[info.flow].arrow, ox + 32, oz + 35);
          ctx.fillText(FLOW_DIRECTIONS[info.flow].arrow, ox + 32, oz + 35);
        }
      }
    }

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
    ctx.font = 'bold 11px sans-serif';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText('北', MAP_PX - 11, 10);
  }
}
