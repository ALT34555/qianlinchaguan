# 茜林茶馆 · Windows 独立客户端

双击便携目录中的 `QianlinChaguan.exe` 打开启动器，再点“启动游戏”。客户端自带 Electron 运行时，不需要 Node.js、浏览器或网络连接。分发时必须复制整个便携目录，不能只复制 EXE。

## 命名约定

全工程统一使用这套叫法，界面文案、文档与提交说明都不要另起叫法：

| 名称 | 含义 |
| --- | --- |
| 茜林茶馆 | 主工程 / 产品名 |
| 舆图 | 2D 世界地图（俯视：生成、查看、筛选地形） |
| 山川 | 3D 世界（进入其中行走、飞行、建造） |

英文语言包对应 `Qianlin Teahouse / Atlas / World`，键名固定为 `app.name`、`view.atlas`、`view.world`。

界面不放装饰性文字：没有英文小标题、没有诗句口号，需要说明的地方就写清楚它是什么、怎么用。

## 视觉

启动器与游戏内界面共用一套视觉基准，取自 `src/ui/theme.css`：底色 `#e5d3aa`、正文 `#4d4030`、强调 `#a61b29`；按钮做成印章感，可用 `#894e54`、禁用 `#867e76`；标题用纳米老宋，正文用资源圆体（`content/assets/ui/fonts/`）。

启动器是独立窗口，由 Electron 以 `file://` 直接加载，不经过 Vite，因此 `platforms/windows/launcher.css` 镜像了一份同样的设计令牌与印章按钮规则。改配色时请与 `src/ui/theme.css` 同步修改这两处。字体在 `launcher.css` 中按相对路径 `../../content/assets/ui/fonts/` 引用，打包时由 `build-desktop.mjs` 一并带入 stage。

启动器提供启动游戏、打开存档目录两个入口，以及舆图 / 山川 / 本地存档三张说明卡。

## 游戏流程与触屏

创建 / 读取世界 → 舆图 → 双击地图位置空降进入山川。鼠标拖动或单指拖动平移舆图，滚轮或双指缩放；触屏双触同一位置也能空降。点“从玩家位置进入山川”可继续原位置。

山川内按 ESC 或点右上角菜单：继续、保存、导出、设置、回到舆图、返回开始界面。设置不再提供视图模式（舆图 / 山川）下拉选择。每次创建或读取世界均先进入舆图，不受旧设置中的视图值影响。

“屏幕虚拟按键”在设置中开关并保存在本机；检测到触屏时默认开启。开启后，左侧方向按钮移动，右侧疾跑、跳跃/上升、下降；双击跳跃切换浮空，单指拖动山川场景转动视角。暂停、焦点丢失、触控取消时释放按键。关闭后使用键盘和鼠标锁定操作。

## 存档

- 源码开发版：工程 `userdata/saves`，与 Vite 和旧本地服务共用。
- 打包版：`%APPDATA%/qianlinchaguan-base/saves`，放在可写用户目录，避免安装目录权限问题。
- 启动器“打开存档目录”打开实际使用的目录；游戏也显示实际路径。
- 旧开发存档可在游戏“读取世界 → 导入存档”导入到打包版；不会自动搬动原始存档。
- 游戏设置写入存档目录同级的 `settings.json`，不受随机本地端口变化影响；浏览器开发模式仍使用 localStorage，与桌面版各自独立。

## 开发与打包

首次运行 `npm install`，再执行：

```powershell
npm run desktop
npm run build:windows
```

`desktop` 构建游戏和共享存档服务，然后打开独立窗口。Electron 44 的 npm 包未自动下载运行时，构建脚本会在缺失时执行其官方安装脚本。

`build:windows` 生成 `release/QianlinChaguan-win32-x64`。可用 `npm run build:windows -- --out release/20261005` 指定输出目录。Windows 开发环境需要 Node.js 22.12+。普通玩家不需要 Node.js。

`启动茜林茶馆.cmd` 无参数启动独立客户端；`静默启动.vbs` 隐藏构建控制台。旧 `launcher.mjs` 保留用于存档服务诊断和浏览器开发模式：`node platforms/windows/launcher.mjs --check`，以及 `--no-open`、`--stop` 等参数。`停止本地服务.cmd` 仅停止旧服务；独立客户端在关闭全部窗口时自动停止自己的服务。

## 实现与验证

客户端主进程使用随机回环端口，复用 `platforms/saves/local-saves.ts` 的原子存档和请求校验。SSR 构建将存档 TypeScript 转成随包运行的 JavaScript。渲染进程禁用 Node 集成，启用 sandbox 与 contextIsolation；启动器只暴露启动和打开存档目录两项接口。

自动回归涵盖地形/星球/历法/存档；Electron 窗口测试覆盖创建、读取、地图双击进入山川、地形加载、ESC 保存与返回舆图、虚拟输入及开关持久化。触屏事件通过模拟验证，真实设备的多指手感仍需实机体验。
