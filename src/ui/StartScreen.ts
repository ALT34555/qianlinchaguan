import { DEFAULT_SEED } from '../core/config';
import { hashString } from '../core/math/Random';
import { WorldGenerator, FLOW_DIRECTIONS, type ChunkInfo } from '../systems/world/WorldGenerator';
import { getChunkTypeDef, formatChunkId, CHUNK_TYPES } from '../systems/world/ChunkTypes';
import { CLIMATES, normalizeClimateWeights, type ClimateWeights } from '../systems/world/WorldSettings';
import { parseWorldSave, type WorldSave } from '../systems/world/WorldSave';
import { AtlasView, type ViewportStats } from './AtlasView';

export function parseSeed(raw: string): number {
  const value = raw.trim();
  if (!value) return DEFAULT_SEED;
  return /^-?\d+$/.test(value) ? Number(BigInt.asUintN(32, BigInt(value))) : hashString(value);
}

export interface StartOptions {
  seed: number;
  climateWeights: ClimateWeights;
  calendarType: 'real' | 'yuan';
  unixMs?: number;
  save?: WorldSave;
}

export class StartScreen {
  private generator: WorldGenerator | null = null;
  private atlasView: AtlasView | null = null;
  private mapShown = false;
  private isFullscreenMap = false;

  constructor(
    private readonly root: HTMLElement,
    seed: string,
    weights: ClimateWeights,
    private readonly enter: (options: StartOptions) => void
  ) {
    this.renderLayout();
    this.initFormValues(seed, weights);
    this.initEventListeners();
  }

  private el<T extends HTMLElement = HTMLElement>(id: string): T {
    return this.root.querySelector(`#${id}`) as T;
  }

  private input(id: string): HTMLInputElement {
    return this.el<HTMLInputElement>(id);
  }

  private status(message: string): void {
    const el = this.el('menu-status');
    if (el) el.textContent = message;
  }

  private error(error: unknown): void {
    this.status(error instanceof Error ? error.message : String(error));
  }

  private weights(): ClimateWeights {
    return normalizeClimateWeights(
      CLIMATES.map((_, i) => {
        const input = this.input(`climate-${i}`);
        return input.value.trim() === '' ? NaN : Number(input.value);
      })
    );
  }

  private settings(): StartOptions {
    const calSelect = this.el<HTMLSelectElement>('calendar-mode');
    return {
      seed: parseSeed(this.input('world-seed').value),
      climateWeights: this.weights(),
      calendarType: (calSelect ? calSelect.value : 'real') as 'real' | 'yuan',
    };
  }

  private renderLayout(): void {
    this.root.innerHTML = `
      <header class="menu-header">
        <a class="wordmark" href="./">
          茜林茶馆
          <span>QIANLIN · WORLD ATLAS</span>
        </a>
        <div class="header-right">
          <span class="edition">沙盒原型 · 探索版 02</span>
          <button id="header-enter-btn" class="header-action-btn primary hidden">进入世界 <span>→</span></button>
        </div>
      </header>

      <div class="menu-layout" id="menu-layout-container">
        <!-- 左侧：世界设置面板 -->
        <section class="world-settings" id="world-settings-panel" aria-labelledby="menu-title">
          <div class="eyebrow">一粒种子，一方天地</div>
          <h1 id="menu-title">山川有序<br><span>万物相生。</span></h1>
          <p class="menu-intro">让山丘相连成脉，让溪流奔向河海。<br>在进入世界前，预览属于你的大陆。</p>

          <!-- 快速预设 -->
          <div class="presets-section">
            <span class="field-label-mini">推荐预设</span>
            <div class="presets-row">
              <button class="preset-pill" data-preset="balanced">🌸 均衡大陆</button>
              <button class="preset-pill" data-preset="islands">🏝️ 温暖群岛</button>
              <button class="preset-pill" data-preset="mountains">🏔️ 连绵高山</button>
              <button class="preset-pill" data-preset="arctic">❄️ 极北冰原</button>
            </div>
          </div>

          <label class="field-title" for="world-seed">
            世界种子 <small>数字、文字或日期</small>
          </label>
          <div class="seed-field">
            <input id="world-seed" maxlength="128" autocomplete="off" placeholder="输入任意种子…">
            <button id="random-seed" title="随机生成种子" aria-label="随机种子">↻</button>
          </div>

          <label class="field-title" for="calendar-mode">
            世界历法 <small>创建后不可更改（影响季节流转）</small>
          </label>
          <div class="seed-field">
            <select id="calendar-mode" class="calendar-select" style="width: 100%; padding: 0.5rem; background: var(--input-bg, rgba(255,255,255,0.1)); border: 1px solid var(--border-color, rgba(255,255,255,0.2)); color: white; border-radius: 4px; font-size: 0.9rem;">
              <option value="real" style="color: black;">真实历法 (公历 + 农历)</option>
              <option value="yuan" style="color: black;">简化历法 (元历)</option>
            </select>
          </div>

          <div class="field-title climate-heading">
            气候配比 <small>权重自动换算为百分比</small>
          </div>
          <div class="climate-bar" aria-label="气候比例"></div>
          <div class="climate-inputs">
            ${CLIMATES.map((c, i) => `
              <label class="climate-row">
                <span><i style="background:${c.color}"></i>${c.name}</span>
                <input id="climate-${i}" aria-label="${c.name}权重" type="number" min="0" max="1000" step="1">
                <output id="percent-${i}"></output>
              </label>
            `).join('')}
          </div>

          <p class="setting-note">
            热带 → 亚热带 → 常规 → 温带 → 寒带<br>
            比例控制大范围气候带宽；局部视口实际占比会有所差异。基础地形可穿插在任意气候带中。
          </p>

          <p id="menu-status" class="menu-status" role="status" aria-live="polite"></p>

          <div class="menu-actions">
            <button id="browse-map" class="secondary">
              <span>浏览 2D 地图</span> <span class="btn-arrow">↗</span>
            </button>
            <button id="enter-world" class="primary">
              <span>进入世界</span> <span class="btn-arrow">→</span>
            </button>
          </div>

          <button id="menu-import" class="text-button">📂 导入世界存档 (.json)</button>
          <input id="menu-import-file" class="hidden" type="file" accept=".json">
        </section>

        <!-- 右侧：世界图志面板 -->
        <section class="atlas-panel" id="atlas-main-panel" aria-label="地图浏览器">
          <!-- 初始未激活状态卡片 -->
          <div id="atlas-placeholder" class="atlas-placeholder">
            <div class="hero-art-container">
              <div class="contour-art">
                <span>山</span>
                <span>川</span>
              </div>
            </div>
            <div class="placeholder-text-group">
              <div class="hero-tag">WORLD ATLAS · 2D 地图</div>
              <h3>山川草木，皆有迹可循</h3>
              <p>设定种子与气候配比，点击下方开启 2D 地图预览。<br>支持拖拽、缩放、图层控制与特定区块红线高亮筛选。</p>
              <button id="placeholder-browse-btn" class="primary hero-browse-btn">
                <span>开启 2D 地图探索</span> <span class="btn-arrow">↗</span>
              </button>
            </div>
          </div>

          <!-- 2D 地图主界面 -->
          <div id="atlas-content" class="atlas-content hidden">
            <!-- 地图顶部操作栏 -->
            <div class="atlas-header-bar">
              <div class="atlas-title-group">
                <span class="eyebrow">WORLD ATLAS</span>
                <h2>世界图志 · 2D 地图</h2>
                <span id="map-coord-pill" class="coord-pill">中心 (0, 0) · 100%</span>
              </div>
              <div class="atlas-view-tools">
                <button id="map-btn-spawn" class="tool-btn" title="定位到出生点">🎯 出生点</button>
                <button id="map-btn-reset" class="tool-btn" title="重置视角与缩放">⟲ 重置</button>
                <button id="map-btn-zoom-in" class="tool-btn icon-btn" title="放大">＋</button>
                <button id="map-btn-zoom-out" class="tool-btn icon-btn" title="缩小">－</button>
                <button id="map-btn-expand" class="tool-btn icon-btn" title="全屏展开/还原">⛶</button>
              </div>
            </div>

            <!-- 地图主体工作区：左侧视口 + 右侧控制器窗口 -->
            <div class="atlas-workspace">
              <!-- 左侧：地图画布视口 -->
              <div class="map-viewport-wrapper" id="map-viewport-container">
                <canvas id="atlas-canvas" class="atlas-canvas" aria-label="2D 地图画布"></canvas>
                <!-- 视口左下角快捷提示 -->
                <div class="viewport-tip">
                  <span>🖱️ 拖拽平移 · 滚轮缩放 · 点击区块查看详情</span>
                </div>
              </div>

              <!-- 右侧新增控制窗口 -->
              <aside class="atlas-control-panel" id="atlas-control-sidebar" aria-label="地图图层与筛选控制器">
                <!-- 1. 显示与图层控制 -->
                <div class="ctrl-group">
                  <div class="ctrl-group-title">
                    <span>图层与投影控制</span>
                  </div>

                  <!-- 控制项 1：显示/隐藏区块信息（编号、类型） -->
                  <label class="ctrl-toggle-row">
                    <span class="toggle-text">
                      <strong>显示区块信息</strong>
                      <small>在色块上标明编号与类型名称</small>
                    </span>
                    <input type="checkbox" id="toggle-chunk-info" class="switch-input">
                    <span class="switch-slider"></span>
                  </label>

                  <!-- 控制项 2：显示/隐藏具体投影 -->
                  <label class="ctrl-toggle-row">
                    <span class="toggle-text">
                      <strong>显示具体投影</strong>
                      <small>地势起伏阴影浮雕与水系流向</small>
                    </span>
                    <input type="checkbox" id="toggle-projection" class="switch-input">
                    <span class="switch-slider"></span>
                  </label>

                  <!-- 控制项 3：显示气候分类 -->
                  <label class="ctrl-toggle-row">
                    <span class="toggle-text">
                      <strong>显示气候分类</strong>
                      <small>按五大气候带色彩渲染色块</small>
                    </span>
                    <input type="checkbox" id="toggle-climate-mode" class="switch-input">
                    <span class="switch-slider"></span>
                  </label>

                  <!-- 气候细分标签与视口占比 -->
                  <div class="climate-tags-panel" id="climate-tags-panel">
                    <div class="climate-tags-title">气候带分布（点击过滤）：</div>
                    <div class="climate-pills-row" id="climate-pills-container">
                      ${CLIMATES.map((c, i) => `
                        <button class="climate-pill" data-climate="${i}">
                          <i style="background:${c.color}"></i>
                          <span>${c.name}</span>
                          <b id="pill-percent-${i}">0%</b>
                        </button>
                      `).join('')}
                    </div>
                  </div>
                </div>

                <!-- 2. 控制项 4：筛选特定区块（用红色表示轮廓线） -->
                <div class="ctrl-group">
                  <div class="ctrl-group-title">
                    <span>筛选特定区块</span>
                    <span class="red-line-badge">🔴 红色轮廓线标示</span>
                  </div>

                  <div class="filter-controls-area">
                    <select id="chunk-type-filter" class="filter-dropdown" aria-label="选择要筛选的区块类型">
                      <option value="">-- 显示全部区块 (无筛选) --</option>
                      ${this.generateChunkFilterOptions()}
                    </select>

                    <div class="quick-filter-row">
                      <button class="qf-btn" data-qf="river">🌊 河流水系 (005)</button>
                      <button class="qf-btn" data-qf="water">💧 全部水体</button>
                      <button class="qf-btn" data-qf="mountain">⛰️ 山地高地</button>
                      <button class="qf-btn" data-qf="snow">❄️ 雪山冰原</button>
                      <button class="qf-btn clear-btn" data-qf="clear">✕ 清除筛选</button>
                    </div>

                    <div id="filter-match-status" class="filter-match-status hidden">
                      <span id="filter-match-text">已高亮 0 个匹配区块</span>
                    </div>
                  </div>
                </div>

                <!-- 3. 选中区块详情卡片 -->
                <div class="ctrl-group">
                  <div class="ctrl-group-title">
                    <span>区块详情</span>
                    <small id="selected-chunk-badge" class="detail-badge">点击色块查看</small>
                  </div>
                  <div id="chunk-detail-card" class="chunk-detail-card">
                    <div class="detail-placeholder">点击地图上的任意色块查看坐标与地形数据</div>
                  </div>
                </div>

                <!-- 底部快捷进入世界 -->
                <div class="sidebar-action-footer">
                  <button id="sidebar-enter-btn" class="primary full-width">
                    进入此世界 <span>→</span>
                  </button>
                </div>
              </aside>
            </div>
          </div>

          <footer class="atlas-footer">
            <span>连续地势 · 八向汇流 · 有序气候</span>
            <span>64 × 64 方块 / 区块 · 支持无限拖拽与平滑缩放</span>
          </footer>
        </section>
      </div>
    `;
  }

  private generateChunkFilterOptions(): string {
    const groups: { label: string; min: number; max: number }[] = [
      { label: '基础地形', min: 1, max: 9 },
      { label: '热带', min: 101, max: 109 },
      { label: '亚热带', min: 201, max: 208 },
      { label: '温带', min: 301, max: 308 },
      { label: '寒带', min: 401, max: 409 },
    ];

    return groups.map(g => {
      const types = CHUNK_TYPES.filter(t => t.generate && t.id >= g.min && t.id <= g.max);
      if (types.length === 0) return '';
      return `
        <optgroup label="${g.label}">
          ${types.map(t => `<option value="${t.id}">${formatChunkId(t.id)} ${t.name}</option>`).join('')}
        </optgroup>
      `;
    }).join('');
  }

  private initFormValues(seed: string, weights: ClimateWeights): void {
    this.input('world-seed').value = seed;
    weights.forEach((v, i) => {
      this.input(`climate-${i}`).value = String(Math.round(v * 10000) / 100);
    });
    this.updateWeights();
  }

  private updateWeights(): void {
    try {
      const weights = this.weights();
      weights.forEach((v, i) => {
        const out = this.el(`percent-${i}`);
        if (out) out.textContent = `${(v * 100).toFixed(1)}%`;
      });
      this.el('enter-world')?.removeAttribute('disabled');
      this.el('browse-map')?.removeAttribute('disabled');
      const bar = this.root.querySelector('.climate-bar');
      if (bar) {
        bar.innerHTML = weights
          .map((w, i) => `<span style="width:${w * 100}%;background:${CLIMATES[i].color}"></span>`)
          .join('');
      }
    } catch (e) {
      CLIMATES.forEach((_, i) => {
        const out = this.el(`percent-${i}`);
        if (out) out.textContent = '—';
      });
      this.el('enter-world')?.setAttribute('disabled', '');
      this.el('browse-map')?.setAttribute('disabled', '');
      this.error(e);
    }
  }

  private initEventListeners(): void {
    // 种子与气候输入监听
    this.root.querySelectorAll<HTMLInputElement>('.world-settings input:not([type=file])').forEach(input => {
      input.addEventListener('input', () => {
        this.generator = null;
        this.status(this.mapShown ? '设置已更改，点击「浏览 2D 地图」更新预览。' : '');
        this.updateWeights();
      });
    });

    // 随机种子按钮
    this.el('random-seed').onclick = () => {
      this.input('world-seed').value = String(crypto.getRandomValues(new Uint32Array(1))[0]);
      this.generator = null;
      this.status('已生成随机种子，点击「浏览 2D 地图」即可查看。');
      if (this.mapShown) this.preview();
    };

    // 快速预设按钮
    this.root.querySelectorAll<HTMLButtonElement>('[data-preset]').forEach(btn => {
      btn.onclick = () => {
        const preset = btn.dataset.preset;
        this.applyPreset(preset);
      };
    });

    // 浏览地图按钮
    this.el('browse-map').onclick = () => this.preview();
    this.el('placeholder-browse-btn')?.addEventListener('click', () => this.preview());

    // 进入世界按钮（支持各处的进入按钮）
    const handleEnter = () => {
      try {
        const options = this.settings();
        this.enter(options);
      } catch (e) {
        this.error(e);
      }
    };
    this.el('enter-world').onclick = handleEnter;
    this.el('sidebar-enter-btn')?.addEventListener('click', handleEnter);
    this.el('header-enter-btn')?.addEventListener('click', handleEnter);

    // 导入世界存档
    this.el('menu-import').onclick = () => this.input('menu-import-file').click();
    this.input('menu-import-file').onchange = async () => {
      const file = this.input('menu-import-file').files?.[0];
      if (!file) return;
      try {
        const save = parseWorldSave(await file.text());
        this.enter({ ...save, save });
      } catch (e) {
        this.error(e);
      }
      this.input('menu-import-file').value = '';
    };

    // ---- 2D 地图右侧窗口控制项绑定 ----

    // 1. 显示/隐藏区块信息（编号、类型）
    const toggleInfo = this.el<HTMLInputElement>('toggle-chunk-info');
    toggleInfo.onchange = () => {
      if (this.atlasView) {
        this.atlasView.setShowChunkInfo(toggleInfo.checked);
      }
    };

    // 2. 显示/隐藏具体投影
    const toggleProj = this.el<HTMLInputElement>('toggle-projection');
    toggleProj.onchange = () => {
      if (this.atlasView) {
        this.atlasView.setShowProjection(toggleProj.checked);
      }
    };

    // 3. 显示气候分类
    const toggleClimate = this.el<HTMLInputElement>('toggle-climate-mode');
    toggleClimate.onchange = () => {
      if (this.atlasView) {
        this.atlasView.setShowClimate(toggleClimate.checked);
      }
    };

    // 气候单个标签过滤点击
    this.root.querySelectorAll<HTMLButtonElement>('.climate-pill').forEach(pill => {
      pill.onclick = () => {
        if (!this.atlasView) return;
        const climateIdx = Number(pill.dataset.climate);
        const current = this.atlasView.getActiveClimateFilter();
        const next = current === climateIdx ? null : climateIdx;
        this.atlasView.setActiveClimateFilter(next);
        this.root.querySelectorAll('.climate-pill').forEach(p => p.classList.remove('active'));
        if (next !== null) pill.classList.add('active');
      };
    });

    // 4. 筛选特定区块（红色轮廓线高亮）
    const typeFilterSelect = this.el<HTMLSelectElement>('chunk-type-filter');
    typeFilterSelect.onchange = () => {
      if (!this.atlasView) return;
      const val = typeFilterSelect.value;
      if (val === '') {
        this.atlasView.setFilterTypeId(null);
      } else {
        this.atlasView.setFilterTypeId(Number(val));
      }
      this.updateQuickFilterActive('');
    };

    // 快捷筛选按钮
    this.root.querySelectorAll<HTMLButtonElement>('[data-qf]').forEach(btn => {
      btn.onclick = () => {
        if (!this.atlasView) return;
        const qf = btn.dataset.qf;
        if (qf === 'clear') {
          typeFilterSelect.value = '';
          this.atlasView.clearFilter();
          this.updateQuickFilterActive('');
          this.root.querySelectorAll('.climate-pill').forEach(p => p.classList.remove('active'));
        } else {
          typeFilterSelect.value = '';
          this.atlasView.setFilterCategory(qf || null);
          this.updateQuickFilterActive(qf || '');
        }
      };
    });

    // 地图顶部工具栏按钮
    this.el('map-btn-spawn')?.addEventListener('click', () => {
      this.atlasView?.centerSpawn();
    });
    this.el('map-btn-reset')?.addEventListener('click', () => {
      this.atlasView?.resetView();
    });
    this.el('map-btn-zoom-in')?.addEventListener('click', () => {
      this.atlasView?.zoomBy(1.25);
    });
    this.el('map-btn-zoom-out')?.addEventListener('click', () => {
      this.atlasView?.zoomBy(0.8);
    });
    this.el('map-btn-expand')?.addEventListener('click', () => {
      this.toggleExpandMap();
    });
  }

  private applyPreset(preset?: string): void {
    if (!preset) return;
    const presets: Record<string, { weights: ClimateWeights; desc: string }> = {
      balanced: { weights: [20, 20, 20, 20, 20], desc: '均衡大陆：五种气候均匀过渡' },
      islands: { weights: [45, 35, 15, 5, 0], desc: '温暖群岛：偏向热带与亚热带海域' },
      mountains: { weights: [10, 15, 35, 30, 10], desc: '连绵高山：偏向常规温带山地' },
      arctic: { weights: [0, 5, 15, 35, 45], desc: '极北冰原：严寒气候与终年积雪' },
    };
    const target = presets[preset];
    if (target) {
      target.weights.forEach((w, i) => {
        this.input(`climate-${i}`).value = String(w);
      });
      this.generator = null;
      this.updateWeights();
      this.status(`已应用预设【${target.desc}】`);
      if (this.mapShown) this.preview();
    }
  }

  private updateQuickFilterActive(activeQf: string): void {
    this.root.querySelectorAll<HTMLButtonElement>('[data-qf]').forEach(b => {
      b.classList.toggle('active', b.dataset.qf === activeQf && activeQf !== 'clear');
    });
  }

  private toggleExpandMap(): void {
    this.isFullscreenMap = !this.isFullscreenMap;
    const container = this.el('menu-layout-container');
    const settingsPanel = this.el('world-settings-panel');
    const headerEnterBtn = this.el('header-enter-btn');

    if (this.isFullscreenMap) {
      container.classList.add('map-fullscreen');
      settingsPanel.classList.add('collapsed');
      headerEnterBtn.classList.remove('hidden');
    } else {
      container.classList.remove('map-fullscreen');
      settingsPanel.classList.remove('collapsed');
      headerEnterBtn.classList.add('hidden');
    }

    setTimeout(() => {
      this.atlasView?.requestRender();
    }, 120);
  }

  private preview(): void {
    try {
      const options = this.settings();
      if (!this.generator) {
        this.generator = new WorldGenerator(options.seed, options.climateWeights);
      }

      this.mapShown = true;
      this.el('atlas-placeholder').classList.add('hidden');
      this.el('atlas-content').classList.remove('hidden');

      const canvas = this.el<HTMLCanvasElement>('atlas-canvas');
      if (!this.atlasView) {
        this.atlasView = new AtlasView({
          canvas,
          generator: this.generator,
          onChunkSelect: (info) => this.showChunkDetail(info),
          onStatsUpdate: (stats) => this.updateViewportStats(stats),
        });
      } else {
        this.atlasView.setGenerator(this.generator);
      }

      this.status(`种子 ${options.seed} · 2D 地图已就绪，支持拖拽平移与滚轮缩放。`);
    } catch (e) {
      this.error(e);
    }
  }

  private showChunkDetail(info: ChunkInfo): void {
    const card = this.el('chunk-detail-card');
    const badge = this.el('selected-chunk-badge');
    const def = getChunkTypeDef(info.type);
    const code = formatChunkId(info.type);
    const climate = CLIMATES[info.climate];
    const arrow = info.flow >= 0 ? FLOW_DIRECTIONS[info.flow].arrow : '—';
    const flowText = info.flow >= 0 ? `${arrow} (${FLOW_DIRECTIONS[info.flow].dx}, ${FLOW_DIRECTIONS[info.flow].dz})` : '无流动 (静水或高地)';

    if (badge) {
      badge.textContent = `(${info.cx}, ${info.cz})`;
    }

    card.innerHTML = `
      <div class="detail-header">
        <span class="detail-type-tag" style="background:${def.mapColor}">
          ${code} · ${def.name}
        </span>
        <span class="detail-climate-tag" style="border-color:${climate.color};color:${climate.color}">
          ${climate.name}
        </span>
      </div>

      <div class="detail-grid">
        <div class="detail-item">
          <span class="detail-label">区块坐标</span>
          <span class="detail-val">(${info.cx}, ${info.cz})</span>
        </div>
        <div class="detail-item">
          <span class="detail-label">基准地势</span>
          <span class="detail-val">${info.elevation.toFixed(1)} 米</span>
        </div>
        <div class="detail-item">
          <span class="detail-label">局部温度</span>
          <span class="detail-val">${info.temperature.toFixed(2)}</span>
        </div>
        <div class="detail-item">
          <span class="detail-label">河流流向</span>
          <span class="detail-val">${flowText}</span>
        </div>
        ${info.type === 5 || info.discharge > 0 ? `
          <div class="detail-item">
            <span class="detail-label">上游汇水量</span>
            <span class="detail-val">${info.discharge.toFixed(1)}</span>
          </div>
          <div class="detail-item">
            <span class="detail-label">河道宽度</span>
            <span class="detail-val">约 ${info.riverWidth.toFixed(0)} 格</span>
          </div>
        ` : ''}
      </div>

      <div class="detail-desc">${def.description}</div>
    `;
  }

  private updateViewportStats(stats: ViewportStats): void {
    // 顶部坐标胶囊
    const pill = this.el('map-coord-pill');
    if (pill) {
      pill.textContent = `中心 (${stats.centerCx}, ${stats.centerCz}) · 缩放 ${Math.round(stats.scale * 100)}% · 视野 ${stats.visibleChunks} 区块`;
    }

    // 气候标签百分比
    stats.climatePercents.forEach((pct, i) => {
      const el = this.el(`pill-percent-${i}`);
      if (el) el.textContent = `${pct.toFixed(0)}%`;
    });

    // 筛选结果提示
    const matchStatus = this.el('filter-match-status');
    const matchText = this.el('filter-match-text');
    if (stats.activeFilterName) {
      matchStatus.classList.remove('hidden');
      matchText.innerHTML = `已高亮 <b>${stats.activeFilterName}</b> · 匹配 <b>${stats.matchedCount}</b> 个区块 (占比 ${stats.matchedPercent.toFixed(1)}%)`;
    } else {
      matchStatus.classList.add('hidden');
    }
  }
}
