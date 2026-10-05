# 茜林茶馆 (Qianlinchaguan) - RPG 游戏项目结构说明

本目录结构专为满足**跨平台（Win/Linux/Android）、支持Mod/插件与地图分享、支持多人协作**的游戏项目而设计。

## 目录结构规划

```text
qianlinchaguan/
│
├── src/                    # 核心源代码区 (程序员工作区)
│   ├── core/               # 游戏核心逻辑 (启动, 状态机, 设置持久化)
│   ├── systems/            # 玩法系统 (世界生成、日历系统、植被等)
│   │   ├── world/          # 体素/高度图世界、区块生成、Worker 池、存档客户端
│   │   │   └── vegetation/ #   植物低模生成 (几何构建 + 原型 + 注册表 + 散布)
│   │   └── calendar/       # 公历 / 农历 / 元历三套历法
│   ├── entities/           # 实体类代码 (玩家运动与交互)
│   ├── i18n/               # 多语言接口与语言包 (中英双语)
│   ├── ui/                 # 界面控制与交互逻辑 (开始菜单、设置、舆图、调试面板等)
│   └── modding_api/        # 暴露给插件/Mod调用的API接口层
│
├── content/                # 游戏内容区 (策划与美术工作区，与代码解耦)
│   ├── assets/             # 媒体资源 (2d, 3d, 音效, UI切图, 字体)
│   │   └── ui/fonts/       #   UI 字体包 (资源圆体等)
│   ├── data/               # 配置数据 (JSON/CSV格式: 方块表、地貌表、植物参数表等)
│   │   └── world/          #   世界配置 (blocks.json, chunk_types.json, plants/)
│   └── maps/               # 官方内置的地图数据 (含模拟地球大陆 land.json)
│
├── platforms/              # 平台适配区 (一次开发，多向编译)
│   ├── windows/            # Windows 本地启动器与桌面客户端 (launcher.mjs + 桌面端脚本 + 图标)
│   ├── saves/              # 存档后端 local-saves.ts (Node 侧生产代码，dev / preview / 启动器共用)
│   ├── linux/              # Linux 构建脚本和配置 (待建)
│   └── android/            # Android 构建脚本 (待建)
│
├── userdata/               # 运行时存档目录 (userdata/saves/*.json，已被 .gitignore 忽略)
│
└── index.html / package.json / tsconfig.json / vite.config.ts
```

## 设计理念

### 1. 一次开发，多向编译 (跨平台)
- **解耦平台相关代码**：所有的业务逻辑全部集中在 `src/` 中，尽量不使用与特定操作系统绑定的底层 API。
- **独立编译配置**：`platforms/` 目录下按系统存放编译配置文件，让持续集成（CI/CD）和多端打包变得非常清晰。

### 2. 支持插件、存档、地图分享
- **数据驱动设计**：游戏的主体内容（物品、地图、参数表等）全部存放在 `content/` 目录下，并以 JSON/XML/CSV 等通用数据格式读取。
- **本地存档**：存档以独立 JSON 文件保存在 `userdata/saves/`，支持导入、导出备份及多端同步。

### 3. 清晰的分工协作
- **程序开发**：只关注 `src/` 和 `platforms/`，修改代码，扩充 `modding_api`。
- **美术音频**：只关注 `content/assets/`，更新资源文件。
- **游戏策划**：只关注 `content/data/` 和 `content/maps/`，通过表格和数据修改游戏数值和关卡，不需要动核心代码。
