# 旗帜与旗杆参数表（低模旗帜）

本目录是旗帜系统的**真相源**：只写数据、不写代码。
旗帜（旗面）与旗杆是**两份独立的表**，运行时由 `mount`（挂载方式）把它们组合起来 ——
所以"9 面旗 × 6 根杆 × 4 种挂法"不需要 216 个模型，只需要 15 条数据 + 组合规则。

| 文件 | 说明 |
| --- | --- |
| `palettes.json` | 色板：12 个布色（主色 / 镶边 / 图案底色）+ 12 个五金色（杆身 / 金属 / 石座 / 麻绳） |
| `flags.json` | 旗面单位：9 面（7 种旗形全覆盖，其中 4 面带中央方形图案） |
| `poles.json` | 旗杆单位：6 根（落地式 2 / 横向 2 / 棍子 2） |
| `emblems.json` | 阵营徽标接口，当前为**空数组**；契约与字段见下 |

## 旗面：`flags.json`

```jsonc
{
  "id": "flag.rect.chizhi",        // 唯一 id，约定 flag.<旗形>.<拼音>
  "name": "赤帜",
  "latin": "chizhi",
  "shape": "rect",                 // 七种旗形之一，决定轮廓与默认长宽比
  "params": {
    "fly": 1.5,                    // 可选：旗面外飘长度（格）
    "hoist": 1.0,                  // 可选：挂接边长度（格）
    "ratio": 1.5,                  // 可选：未写 fly 时用 hoist × ratio
    "trimWidth": 0.11,             // 可选：镶边厚度（格），0 = 不要镶边
    "sleeve": true,                // 可选：挂接边是否留杆套（用镶边色）
    "charge": { "kind": "square", "size": 0.44 },   // 中央图案
    "wave": { "waves": 1.3 }       // 波形覆盖
  },
  "palette": { "field": "vermilion", "trim": "gold", "charge": "ivory" },
  "tags": ["faction", "war"]
}
```

- `palette` 的槽名只能是 `field` / `trim` / `charge` / `emblem` / `pole` / `metal` / `stone` / `cord`；
- 色板键**只能引用 `palettes.json` 里已有的键**，写错在加载时直接抛错并列出可用键；
- `charge.kind` 只能引用已注册的图案绘制器（内置 `none` / `square`，可用
  `registerFlagChargePainter` 追加）；`charge.slot` 决定用哪个色槽上色（`emblem` 槽即阵营徽标接口）；
- 尺寸的最终值 = 调用方入参 > 单位 `params` > 旗形默认（`FLAG_SHAPE_TABLE`）。

## 旗杆：`poles.json`

```jsonc
{
  "id": "pole.ground.miaogan",
  "kind": "ground",                // ground 落地式 / horizontal 横向 / stick 棍子
  "params": {
    "height": 4.2,                 // 杆高（格）；横向杆这里指横杆离地高度
    "radius": 0.1, "sides": 6,     // 杆身半径与棱数（低模：5~6 棱）
    "tipRatio": 0.68,              // 顶端半径 / 底端半径（收分）
    "segments": 3, "joints": 2,    // 杆身分节数与节间金属箍
    "base":   { "style": "stone", "radius": 0.42, "height": 0.42, "sides": 6 },
    "finial": { "style": "spear", "size": 0.9 },
    "arm":    { "length": 1.25, "rise": 0.16, "brace": 0.85, "tipKnob": 1.6 },
    "collar": { "style": "band", "size": 1.1 },
    "cord": 0.55                   // >0 = 挂一段旗绳垂端
  },
  "palette": { "pole": "lacquer", "metal": "brass", "stone": "sandstone" }
}
```

- `base.style`：`none` / `slab` 单层石台 / `step` 两级台阶 / `stone` 六棱石墩 / `cross` 十字基脚；
- `finial.style`：`none` / `knob` 圆钮 / `ball` 圆球 / `spear` 矛尖 / `flame` 火焰 / `cross` 十字；
- `arm`（横木）：**落地式与棍子是可选项，横向旗杆是必须项**（横杆本身就是杆身）。
  有没有横木决定这杆能用哪些挂法：
  - **没有横木** → `masthead` 杆顶飘扬 / `midmast` 杆中悬挂；
  - **有横木** → `armFly` 杆头飘扬 / `armHang` 横木垂挂（垂幅幡只能走 armHang）。
  两类冲突都会抛中文错误并列出可用挂载（详见 `flags.md` 的挂载矩阵）；
- 横向旗杆另有 `plate`（墙面法兰）：`none` / `plate` / `bracket`（带斜撑）。

## 阵营徽标接口：`emblems.json`

当前 `emblems` 是空数组，字段契约已经定好，后期加阵营只需追加条目：

```jsonc
{
  "version": 1,
  "emblems": [
    {
      "id": "emblem.example",
      "name": "示例徽标",
      "charge": { "kind": "square", "size": 0.42, "slot": "emblem" },
      "palette": { "emblem": "gold" },
      "tags": ["faction"]
    }
  ]
}
```

调用 `buildFlagAssembly({ flag, pole, emblem: 'emblem.example' })` 时，
徽标的 `charge` / `palette` 会与旗帜自身的设置**逐字段合并**（入参优先，其次徽标，最后旗帜）；
要用真正的徽标图形，注册一个自定义 `kind` 的绘制器即可（`registerFlagChargePainter`）。

## 重出衍生资源

```text
node 000base\tools\flag-manifest.mjs   # content\assets\models\flags\manifest.json
node 000base\tools\flag-preview.mjs    # .fordev_docs\src\systems\world\flags\docimg_flags-preview\*.png
```

📖 完整文档：**[`.fordev_docs/src/systems/world/flags/flags.md`](../../../../.fordev_docs/src/systems/world/flags/flags.md)**
🖼 预览图：[`docimg_flags-preview/`](../../../../.fordev_docs/src/systems/world/flags/docimg_flags-preview/)
