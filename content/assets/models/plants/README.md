# 植物模型资源（低模 / low-poly）

本目录存放 `src/systems/world/vegetation/` 低模植物生成器产出的**衍生资源**，与源码解耦：

| 文件 | 说明 | 当前状态 |
| --- | --- | --- |
| `manifest.json` | 49 物种 × 生命周期状态的统计（高度、冠幅、三角形数、材质槽、花色、花瓣数、导出目标路径，并标出"该状态实际呈现为哪一态"） | ✅ 已按当前参数表重出（`version: 3`，七态口径） |
| `_legacy_v1/` | version 1 时期的全部 GLB 归档（`plants.glb` / 按季节分档 / `bloom.glb` / `singles/*.glb`） | ⚠️ 已归档隔离，**与当前参数表不对应** |
| `plants.glb`、`singles/*.glb`（待生成） | 新的 GLB 归档与单株模型，命名 `<物种id>__<状态>__<季节>.glb` | ❌ 待补齐导出器后生成 |

> **真相源在参数表**：`content/data/world/plants/**/*.json`。本目录全部是衍生资源。
>
> 📖 **植被系统完整文档（花瓣与花簇、七态与回退规则、色板、物种清单、接口、低模预算、预览图）：
> [`.fordev_docs/src/systems/world/vegetation/vegetation.md`](../../../../.fordev_docs/src/systems/world/vegetation/vegetation.md)**
>
> 🖼 预览图：[`.fordev_docs/src/systems/world/vegetation/docimg_vegetation-preview/`](../../../../.fordev_docs/src/systems/world/vegetation/docimg_vegetation-preview/)

## 怎么重出这些衍生资源

```text
node 000base\tools\plant-manifest.mjs    # 重出本目录的 manifest.json
node 000base\tools\plant-preview.mjs     # 重出 .fordev_docs/src/systems/world/vegetation/docimg_vegetation-preview/ 的 13 张预览图
```

两个工具都通过 `000base\tools\plant-bundle.mjs` 用工程自带的 rolldown 把
`src/systems/world/vegetation/` 打成临时 ESM 再加载，**不新增任何依赖**。

## 为什么 GLB 仍然没有

GLB 导出器仍未补齐：导出 `position / normal / color / index` 之后还要按材质槽
（`solid` / `snow` / `bloom` / `fruit`）拆 primitive 并序列化 glTF，
这一步与预览器不同（预览器只需要画出来）。清单里的 `glb` 字段目前是**导出目标路径**。

在那之前，version 1 的 GLB 被移到 `_legacy_v1/` 以免被误当成当前资源 ——
它们的物种 id 还是旧命名（形如 `tree.temp.oak`），也没有状态维度。

## 快速索引

- 生成器：`src/systems/world/vegetation/`
  （`geometry.ts` 低模构建、`archetypes.ts` 形态原型、`Plants.ts` 注册表与状态派生、`placement.ts` 散布判定）
- 参数表：`content/data/world/plants/`（一个物种一个文件 + `palettes.json` + `plants.json` 索引）
- 文档：`.fordev_docs/src/systems/world/vegetation/vegetation.md`
- 预览图：`.fordev_docs/src/systems/world/vegetation/docimg_vegetation-preview/`
- 离线工具：`000base/tools/`（`plant-preview.mjs` / `plant-manifest.mjs` / `plant-bundle.mjs`）
