<div align="center">

<img src="platforms/windows/qianlin.png" alt="茜林茶馆 Logo" width="108" height="108" style="border-radius: 20px;" />

# 茜林茶馆 · Qianlin Teahouse

**从一粒种子生成完整世界，在舆图与山川之间自由漫游。**  
*A procedural low-poly sandbox RPG world generator & explorer.*

[![Node.js](https://img.shields.io/badge/Node.js-22%2B-339933?logo=node.js&logoColor=white)](https://nodejs.org/)
[![TypeScript](https://img.shields.io/badge/TypeScript-7.x-3178C6?logo=typescript&logoColor=white)](https://www.typescriptlang.org/)
[![Three.js](https://img.shields.io/badge/Three.js-0.186-black?logo=three.js&logoColor=white)](https://threejs.org/)
[![Vite](https://img.shields.io/badge/Vite-8.x-646CFF?logo=vite&logoColor=white)](https://vitejs.dev/)
[![Electron](https://img.shields.io/badge/Electron-44.x-47848F?logo=electron&logoColor=white)](https://www.electronjs.org/)

[特性亮点](#-特性亮点) • [快速开始](#-快速开始) • [操作说明](#-操作说明) • [项目架构](#-项目架构) • [开源致谢](#-开源致谢)

</div>

---

## 🍵 关于项目

**《茜林茶馆》** 是一套融合了**中式古典美学**与**高扩展性程序生成架构**的跨平台 3D 沙盒世界探索项目。

项目旨在通过纯参数表驱动与程序化几何算法，从任意随机种子生成具备真实气候梯度、板块构造、水系分布、四季植被、岩石群与旗帜的连续 Low-Poly 大陆与星球。玩家可在二维**「舆图」**与三维**「山川」**之间无缝流转，体验漫游、空降与勘测的乐趣。

---

## ✨ 特性亮点

### 🎨 古朴中式美学 (Classical Aesthetic)
- **宣纸与印泥设计语言**：全局界面以纸面（`#e5d3aa`）、墨色（`#4d4030`）、朱砂（`#a61b29`）为主色调，辅以线装书双重线框打底。
- **印章风格交互**：按键赋予阴文阳文印章质感，清晰区分可用印色（`#894e54`）与禁用印色（`#867e76`）。
- **中英双语多语言架构**：全界面文本与逻辑解耦，提供动态多语言切换接口与平铺词条结构。

### 🗺️ 舆图与山川双视角 (Atlas & World)
- **舆图 (2D Atlas)**：二维宏观地图。支持无极平移缩放、高程与气候图层实时渲染、板块水系与大陆分布诊断。
- **山川 (3D World)**：在舆图上双击或触控任意地块即可**空降进入三维世界**，支持地面漫步、疾跑、双击浮空与自由飞行。
- **可调视距**：设置面板支持 2~16 区块渲染视距，雾效与相机裁剪面随视距实时同步。
- **世界装饰**：低模岩石群与 49 物种植被按群系自然散布于三维世界，四季与生命阶段实时呈现。
- **双向无缝联动**：随时通过 ESC 菜单在 2D 舆图与 3D 山川之间往返流转。

### 🏔️ 程序化地貌与统一流域水系 (Procedural Generation & Watershed)
- **五层地貌架构**：气候层（纬度温湿度）、海陆形态层（陆地占比连续可调）、高度抬升层（512/1024/2048 三级地势）、离岛噪声层与星球板块层。
- **统一流域拓扑求解**：基于严格下降 D8 拓扑与实际汇水累加求解，消除局部凹陷死循环。
- **宽深汇流与自然河网**：支持支流向干流汇聚、主槽深度与河宽随流量平滑增长；自然三段河弯与走廊约束，连通湖面与河槽带。
- **入湖/入海自然过渡**：低缓河口渐宽延展、水下沉积浅滩与沙泥渐变，瀑布入水保留深潭。
- **瀑布飞流与水雾**：落差水流带飞流水丝与落点粒子水雾，可在设置中开关粒子效果。
- **自然陆地支持**：内置 Natural Earth 真实海岸距离场（`land.json`），可直接生成还原地球陆地轮廓的探索世界。

### 🌿 数据驱动低模植被 (Low-Poly Vegetation)
- **纯参数表驱动**：外置按物种拆分的参数表（涵盖热带、亚热带、温带、寒带乔木、灌木、特色经济作物、水生及观赏植物等 49 个独立物种）。
- **纯几何程序生成**：不依赖外部模型包，运行时直接按物种基因生成顶点着色网格。
- **七态与四季色板**：支持苗、生、旺、衰、枯及开花（针叶树除外）、果期完整生命阶段与四季色板变换。

### 🪨 参数化低模岩石系统 (Low-Poly Rocks)
- **九大原型几何**：涵盖巨石 (Boulder)、方形荒岩 (Block)、悬岩阶地 (Ledge)、石板 (Slab)、尖棱石片 (Shard)、峰针 (Spire)、残桩石 (Stub)、碎屑堆 (Rubble) 与浮冰/岩层 (Floe)。
- **多阶碎裂与色板映射**：由纯解析几何算法生成，结合岩石色板与群系地质特征实现自然散布。

### 🚩 低模旗帜与旗杆系统 (Low-Poly Flags & Poles)
- **七种旗形与三类杆型**：燕尾旗、直角旗、三角旗、悬旗等，配合木制、石制与金属立杆。
- **解析风动与布料物理**：通过正弦合成波形实时模拟旗面迎风飘扬、下垂与侧倾。

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
│   ├── assets/                  #   媒体与衍生资源
│   │   ├── models/              #     植被、岩石与旗帜生成清单 (manifest.json)
│   │   └── ui/fonts/            #     UI 字体资源（资源圆体等开源字库）
│   ├── data/world/              #   世界配置数据（blocks.json, chunk_types.json）
│   │   ├── flags/               #     低模旗帜与旗杆参数表
│   │   ├── plants/              #     49 物种模块化植物参数表（woody/, herbaceous/）
│   │   └── rocks/               #     低模岩石参数表 (九大原型)
│   └── maps/earth/              #   内置地球大陆高精度海岸距离场 (land.json)
├── platforms/                   # 【平台适配区】一次开发，多端发布
│   ├── saves/                   #   跨端统一本地存档服务 (local-saves.ts)
│   └── windows/                 #   Windows 本地启动器与便携版构建脚本
├── src/                         # 【核心源代码】游戏业务与底层实现
│   ├── core/                    #   引擎核心 (主循环 Game、输入系统、配置、设置持久化)
│   ├── entities/                #   实体控制 (玩家运动、台阶物理、飞行与碰撞)
│   ├── i18n/                    #   多语言系统 (中英本地化资源与热更新)
│   ├── systems/                 #   玩法系统
│   │   ├── calendar/            #     公历 / 农历 / 元历三历法并轨系统
│   │   └── world/               #     程序化体素世界
│   │       ├── flags/           #       低模旗帜与旗杆生成器
│   │       ├── rocks/           #       低模岩石生成器
│   │       ├── vegetation/      #       49 物种低模植被生成器与散布判定
│   │       └── ...              #       统一流域求解、曲流河网、水体与光照
│   └── ui/                      #   界面交互 (古朴中式主题 theme.css、舆图 Atlas、设置面板)
├── index.html                   # 网页应用入口
├── package.json                 # 项目配置与构建脚本
├── tsconfig.json                # TypeScript 编译配置
└── vite.config.ts               # Vite 构建配置与本地存档服务中间件
```

---

## 📜 开源协议与致谢

本项目核心代码基于开源规范开发，并在项目中使用了以下优秀的开源技术与资源支持：

- 渲染与数学库：[Three.js](https://github.com/mrdoob/three.js) (MIT)、[simplex-noise](https://github.com/jwagner/simplex-noise.js) (MIT)
- 构建与运行时：[Vite](https://github.com/vitejs/vite) (MIT)、[TypeScript](https://github.com/microsoft/TypeScript) (Apache-2.0)、[Electron](https://github.com/electron/electron) (MIT)
- 中文开源字体：[Resource Han Rounded (资源圆体)](https://github.com/CyanoHao/Resource-Han-Rounded) (SIL OFL 1.1)、[NanoOldSong (纳米老宋)](https://github.com/Hansha2011/NanoOldSong) (SIL OFL 1.1)
- 地理空间数据：[Natural Earth Vector](https://github.com/nvkelso/natural-earth-vector) (Public Domain)
- 天文算法参考：[astronomia](https://github.com/commenthol/astronomia) (MIT)、[NASA Eclipse Delat-T](https://eclipse.gsfc.nasa.gov/)

完整第三方资产使用清单与开源许可地址参见 [list.txt](list.txt)。
