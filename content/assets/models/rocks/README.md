# 岩石模型资源（低模 / low-poly）

本目录存放 `src/systems/world/rocks/` 低模岩石生成器产出的**衍生资源**，与源码解耦：

| 文件 | 说明 | 当前状态 |
| --- | --- | --- |
| `manifest.json` | 9 个岩石单位 × 4 个形态档 × 四季的统计（高度 / 包围半径 / 最低点 / 三角形数 / 材质槽 / 覆被比例 / 实际色板），外加**区块 → 岩性映射表**（每个区块用哪些单位、换成哪种岩） | ✅ 已按当前参数表与生成器重出（`version: 1`） |
| `singles/*.glb`（待生成） | 单个模型，命名 `<单位id>__<形态档>__<季节>.glb` | ❌ 待补齐导出器后生成 |

> **真相源在参数表**：`content/data/world/rocks/*.json`。本目录全部是衍生资源。
>
> 📖 **岩石系统完整文档（9 个原型、四个形态档、区块配色办法、色板、接口、低模预算、预览图）：
> [`.fordev_docs/rocks.md`](../../../../.fordev_docs/rocks.md)**
>
> 🖼 预览图：[`.fordev_docs/rocks-preview/`](../../../../.fordev_docs/rocks-preview/)

## 怎么重出这些衍生资源

```text
node 000base\tools\rock-manifest.mjs   # 重出本目录的 manifest.json
node 000base\tools\rock-preview.mjs    # 重出 .fordev_docs\src\systems\world\rocks\docimg_rocks-preview\ 的 6 张预览图
node 000base\tools\rock-index.mjs      # 重出参数表目录的 rocks.json 索引
```

三个工具都通过 `000base\tools\rock-bundle.mjs` 用工程自带的 rolldown 把
`src/systems/world/rocks/` 打成临时 ESM 再加载，**不新增任何依赖**。
预览渲染链路来自与植物预览共用的 `000base\tools\lowpoly-preview.mjs`。

## 为什么 GLB 仍然没有

与植被同一情况：GLB 导出器仍未补齐。导出 `position / normal / color / index` 之后
还要按材质槽（`solid` / `moss` / `snow` / `ice`）拆 primitive 并序列化 glTF，
这一步与预览器不同（预览器只需要画出来）。清单里的 `glb` 字段目前是**导出目标路径**。

## 快速索引

- 生成器：`src/systems/world/rocks/`
  （`geometry.ts` 低模原语、`archetypes.ts` 9 个形态原型、`Rocks.ts` 注册表与区块配色、
  `placement.ts` 散布判定）
- 参数表：`content/data/world/rocks/`（一块石头一个文件 + `palettes.json` + `rocks.json` 索引）
- 文档：`.fordev_docs/rocks.md`
- 预览图：`.fordev_docs/rocks-preview/`
- 离线工具：`000base/tools/`（`rock-preview.mjs` / `rock-manifest.mjs` / `rock-index.mjs` / `rock-bundle.mjs` /
  `lowpoly-preview.mjs`）
