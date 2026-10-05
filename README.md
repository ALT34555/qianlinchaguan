<div align="center">

<img src="platforms/windows/qianlin.png" alt="茜林茶馆 Logo" width="108" height="108" style="border-radius: 20px;" />

# 茜林茶馆 · Qianlin Teahouse

**从一粒种子生成完整世界，在舆图与山川之间自由漫游。**  
*A procedural low-poly sandbox RPG world generator & explorer.*

[![Node.js](https://img.shields.io/badge/Node.js-22%2B-339933?logo=node.js&logoColor=white)](https://nodejs.org/)
[![TypeScript](https://img.shields.io/badge/TypeScript-5.x-3178C6?logo=typescript&logoColor=white)](https://www.typescriptlang.org/)
[![Three.js](https://img.shields.io/badge/Three.js-0.186-black?logo=three.js&logoColor=white)](https://threejs.org/)
[![Vite](https://img.shields.io/badge/Vite-6.x-646CFF?logo=vite&logoColor=white)](https://vitejs.dev/)
[![Electron](https://img.shields.io/badge/Electron-44.x-47848F?logo=electron&logoColor=white)](https://www.electronjs.org/)

[特性亮点](#-特性亮点) • [快速开始](#-快速开始) • [操作说明](#-操作说明) • [项目架构](#-项目架构) • [开源致谢](#-开源致谢)

</div>

---

## 🍵 关于项目

**《茜林茶馆》** 是一套融合了**中式古典美学**与**高扩展性程序生成架构**的跨平台 3D 沙盒世界探索项目。

项目旨在通过纯参数表驱动与程序化几何算法，从任意随机种子生成具备真实气候梯度、板块构造、水系分布及四季植被的连续 Low-Poly 大陆与星球。玩家可在二维**「舆图」**与三维**「山川」**之间无缝流转，体验漫游、空降与勘测的乐趣。

---

## ✨ 特性亮点

### 🎨 古朴中式美学 (Classical Aesthetic)
- **宣纸与印泥设计语言**：全局界面以纸面（`#e5d3aa`）、墨色（`#4d4030`）、朱砂（`#a61b29`）为主色调，辅以线装书双重线框打底。
- **印章风格交互**：按键赋予阴文阳文印章质感，清晰区分可用印色（`#894e54`）与禁用印色（`#867e76`）。
- **中英双语多语言架构**：全界面文本与逻辑解耦，提供动态多语言切换接口与平铺词条结构。

### 🗺️ 舆图与山川双视角 (Atlas & World)
- **舆图 (2D Atlas)**：二维宏观地图。支持无极平移缩放、高程与气候图层实时渲染、板块与大陆分布诊断。
- **山川 (3D World)**：在舆图上双击或触控任意地块即可**空降进入三维世界**，支持地面漫步、疾跑、双击浮空与自由飞行。
- **双向无缝联动**：随时通过 ESC 菜单在 2D 舆图与 3D 山川之间往返流转。

### 🏔️ 五层程序化世界生成 (Procedural Generation)
- **五层地貌架构**：
  1. **气候层**：赤道至极地的纬度温度梯度与湿度模拟。
  2. **海陆形态层**：支持陆地占比（`0.0 ~ 1.0`）连续滑杆调节。
  3. **高度抬升层**：模拟地壳板块挤压，划分 512 / 1024 / 2048 三级地势抬升。
  4. **小岛噪声层**：大陆近海离岛、峡湾与碎裂边缘自然生成。
  5. **星球板块层**：模拟板块漂移与大型大陆群落。
- **自然陆地支持**：内置 Natural Earth 真实海岸距离场（`land.json`），可直接生成还原地球陆地轮廓的探索世界。
- **连续 Low Poly 渲染**：采用平滑连续的多边形着色地形、动态水面、折射滤镜及夜间弱光环绕光照。

### 🌿 数据驱动低模植被 (Low-Poly Vegetation)
- **纯参数表驱动**：外置 7 套物种定义参数表（涵盖热带、亚热带、温带、寒带乔木、灌木及作物水生等 49 个独立物种）。
- **纯几何程序生成**：不依赖外部臃肿模型包，运行时直接按物种基因生成顶点着色网格。
- **四季与花期变体**：支持春夏秋冬四色转换及专属开花版本（针叶树除外）。

### 📅 三历法并轨系统 (Tri-Calendar System)
- **公历 (Gregorian)**：标准公历日期与星期运算。
- **农历 (Chinese Lunar)**：基于天文算法中气置闰、干支纪年、月相朔望与二十四节气。
- **元历 (Yuan Calendar)**：360 日古典岁历系统，三套历法在游戏 HUD 中同轴步进。

### 💻 跨平台与独立客户端 (Multiplatform & Launcher)
- **Windows 独立客户端**：基于 Electron 打包的便携桌面版，包含本地回环服务与启动器，不依赖外部浏览器与 Node.js 运行时。
- **双端存档服务**：基于 Node 的统一本地存档后端，支持原子落盘、损坏容错、格式迁移与独立 JSON 导入导出。
- **触屏友好**：内置多点触控与虚拟按键（方向盘、跳跃、疾跑、浮空升降），可动态启用。

---

## 🚀 快速开始

### 环境依赖
- [Node.js](https://nodejs.org/) `22.x LTS` 或更新版本
- npm `10.x` 或更新版本

### 1. 网页开发版启动
```bash
# 克隆仓库
git clone https://github.com/ALT34555/qianlinchaguan.git
cd qianlinchaguan

# 安装依赖
npm install

# 启动开发服务器
npm run dev
```
启动后在浏览器中打开 `http://localhost:5173/` 即可体验。

### 2. Windows 桌面客户端运行
如果在 Windows 环境下开发，可直接使用本地启动器：
```bash
# 启动桌面独立客户端
npm run desktop
```
或者直接双击根目录下的脚本：
- **`platforms\windows\启动茜林茶馆.cmd`**：一键检查环境并运行客户端。

### 3. 构建与打包
```bash
# 检查类型
npm run typecheck

# 构建生产网页端静态资源 (输出至 dist/)
npm run build

# 打包 Windows x64 便携版独立应用 (输出至 release/)
npm run build:windows
```

---

## 🎮 操作说明

| 操作键位 | 动作说明 |
| :--- | :--- |
| **W / A / S / D** 或 **方向键** | 地面行走 / 空中移动 |
| **鼠标移动** | 转动第一人称视角（点击画面进入鼠标锁定） |
| **空格 (Space)** | 单击跳跃；**快速双击切换飞行/浮空模式** |
| **Shift** | 疾跑 / 加速飞行 |
| **空格 / Ctrl** (浮空时) | 垂直上升 / 垂直下降 |
| **鼠标双击 / 触控双触** (舆图界面) | 从目标地块上空 24 格**空降**进入 3D 山川 |
| **F12** | 打开/关闭调试面板（含小地图、气候与区块数据） |
| **Esc** | 暂停游戏 / 打开菜单 / 释放鼠标锁定 / 返回 2D 舆图 |

> 💡 **触屏支持**：可在「设置」中开启「屏幕虚拟按键」，直接使用触控方向舵与动作按钮操控。

---

## 📁 项目架构

项目严格遵守**「业务逻辑、游戏数据、平台接入解耦」**的分工规范：

```text
qianlinchaguan/
├── content/                     # 【游戏内容区】策划与美术工作区（纯数据，与核心代码解耦）
│   ├── assets/ui/fonts/         #   UI 字体资源（资源圆体等商用开源字库）
│   ├── data/world/              #   世界配置数据（blocks.json, chunk_types.json）
│   │   └── plants/              #     植物物种参数表（palettes.json, 乔木/灌木配置）
│   └── maps/earth/              #   内置地球大陆高精度海岸距离场 (land.json)
├── platforms/                   # 【平台适配区】一次开发，多端发布
│   ├── saves/                   #   跨端统一本地存档服务 (local-saves.ts)
│   └── windows/                 #   Windows 本地启动器与便携版构建脚本
├── src/                         # 【核心源代码】游戏业务与底层实现
│   ├── core/                    #   引擎核心 (输入系统、配置、设置持久化)
│   ├── entities/                #   实体控制 (玩家运动、飞行与碰撞)
│   ├── i18n/                    #   多语言系统 (中英本地化资源与热更新)
│   ├── systems/                 #   玩法系统
│   │   ├── calendar/            #     公历 / 农历 / 元历三历法并轨系统
│   │   └── world/               #     五层地形生成、体素区块调度、水体光照
│   │       └── vegetation/      #     程序化植物低模生成器与散布判定
│   └── ui/                      #   界面交互 (古朴中式主题 theme.css、舆图 Atlas、设置面板)
├── index.html                   # 网页应用入口
├── package.json                 # 项目配置与构建脚本
└── tsconfig.json                # TypeScript 编译配置
```

---

## 📜 开源协议与致谢

本项目核心代码基于开源规范开发，并在项目中使用了以下优秀的开源技术与资源支持：

- 渲染与数学库：[Three.js](https://github.com/mrdoob/three.js) (MIT)、[simplex-noise](https://github.com/jwagner/simplex-noise.js) (MIT)
- 构建与运行时：[Vite](https://github.com/vitejs/vite) (MIT)、[TypeScript](https://github.com/microsoft/TypeScript) (Apache-2.0)、[Electron](https://github.com/electron/electron) (MIT)
- 中文开源字体：[Resource Han Rounded (资源圆体)](https://github.com/CyanoHao/Resource-Han-Rounded) (SIL OFL 1.1)
- 地理空间数据：[Natural Earth Vector](https://github.com/nvkelso/natural-earth-vector) (Public Domain)
- 天文算法参考：[astronomia](https://github.com/commenthol/astronomia) (MIT)、[NASA Eclipse Delat-T](https://eclipse.gsfc.nasa.gov/)

完整第三方资产使用清单与开源许可地址参见 [list.txt](file:///d:/JoeStudio/5001develop/101github/public/qianlinchaguan/list.txt)。
