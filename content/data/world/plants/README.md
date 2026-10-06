# 植物参数表（低模 / low-poly）

本目录是**植被的唯一真相源**：形状、颜色、四季变化、生命周期状态全部在这里定义，
`src/systems/world/vegetation/` 只按参数表构建几何（纯函数，可跑在 Worker 里）。

```text
content/data/world/plants/
├─ palettes.json              色板：52 个色阶键 + 四季覆盖
├─ plants.json                物种索引（生成物，仅供查阅，不参与运行）
├─ woody/                     木本：乔木 + 灌木（32 个文件）
└─ herbaceous/                草本 / 地被 / 水生 / 作物 / 观赏（17 个文件）
```

**一个物种一个文件**：`<类群>.<物种拼音>.json`，类群为
`shu.`（树）/ `guan.`（灌木）/ `cao.`（草本地被）/ `shui.`（水生）/ `nong.`（经济作物）/ `hua.`（观赏）。
每份文件里是同一物种的 **6 条记录**（成株 + 幼苗 / 亚成 / 开花 / 结实 / 干枯）：
第一条写全尺寸与色板，其余每条只写 `"state"`，参数自动继承。

```jsonc
// woody/shu.gaoshan.json
{
  "plants": [
    { "id": "shu.gaoshan", "archetype": "broadleaf", "climate": "temperate",
      "name": "高山栎", "latin": "gaoshanli", "seed": 201,
      "tags": ["tree", "canopy", "fruit", "timber"],
      "params": { "height": 14, "trunk": 6.5, "canopy": 4.8, "blobs": 9, "fruitCount": 2 },
      "palette": { "foliage": "foliage", "fruit": "fruit" },
      "states": ["normal", "seedling", "subadult", "flowering", "fruiting", "withered"] },
    { "id": "shu.gaoshan@seedling",  "state": "seedling" },
    { "id": "shu.gaoshan@subadult",  "state": "subadult" },
    { "id": "shu.gaoshan@flowering", "state": "flowering" },
    { "id": "shu.gaoshan@fruiting",  "state": "fruiting" },
    { "id": "shu.gaoshan@withered",  "state": "withered" }
  ]
}
```

- **统一七态**：`0 种植`（只有接口、没有模型，生成时呈现为 1 普通）/ `1 普通` / `2 幼苗` /
  `3 亚成` / `4 开花` / `5 结实` / `6 干枯`。**所有物种的 `states` 清单完全一样**，
  某个物种"想象不出来"的状态（针叶树结实、地衣开花…）在生成时平滑退回普通态，不会报错。
- 花朵的最小单位是**花瓣**：`petals` 写 4 就是四瓣花，写 5 就是五瓣花（默认），
  草类写 2（颖花）、竹写 3、莲 8、菊 10；同一片花瓣换色板键就是叶子（幼苗子叶、树冠碎叶、枯叶）。
- 参数表**只放数据**，不写 `_note` / `version` 之类的说明文字。
- 新增物种必须在 `src/systems/world/vegetation/Plants.ts` 的 `PLANT_FILES` 里登记一行 import，否则不生效。

> 📖 **完整文档（字段语义、花瓣与花簇、七态与回退规则、色板、物种清单、低模预算、预览图、新增物种步骤）：
> [`.fordev_docs/src/systems/world/vegetation/vegetation.md`](../../../.fordev_docs/src/systems/world/vegetation/vegetation.md)**
>
> 🖼 预览图：[`.fordev_docs/src/systems/world/vegetation/docimg_vegetation-preview/`](../../../.fordev_docs/src/systems/world/vegetation/docimg_vegetation-preview/)
> （重出：`node 000base\tools\plant-preview.mjs`）

## 快速核对

| 项 | 值 |
| --- | --- |
| 物种 / 状态条目 | 49 物种 / 294 条（每物种 6 条） |
| 生长带分布 | 寒带 7 · 温带 18 · 亚热带 16 · 热带 8 |
| 各状态物种数 | 普通 49 · 幼苗 49 · 亚成 49 · 开花 49 · 结实 49 · 干枯 49（七态统一，0 种植不占条目） |
| 全量几何 | 49 物种 × 6 态 × 四季 = 1176 个，平均 178 三角形/个 |
| 普通态平均面数 | 228（49 个成株） |
| 花瓣数分布 | 2 瓣 7 种 · 3 瓣 2 种 · 4 瓣 4 种 · 5 瓣 33 种 · 6 瓣 1 种 · 8 瓣 1 种 · 10 瓣 1 种 |
| 色板 | 52 个键 |

## 与 `content/assets/models/plants/` 的关系

那边是**衍生资源**：`manifest.json` 已按当前参数表重出（含七态与逐季统计、花瓣数与回退信息）；
`_legacy_v1/` 是 version 1 时期的 GLB 产物（旧物种 id、无状态维度），已归档隔离。
重出清单：`node 000base\tools\plant-manifest.mjs`。
GLB 导出器仍未补齐（清单里的 `glb` 是导出目标路径）。
