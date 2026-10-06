# 岩石参数表（低模地表点缀）

本目录是岩石系统的**真相源**：一块石头一个文件，只写数据、不写代码。
运行时不读 `rocks.json`（那只是索引）；注册表在 `src/systems/world/rocks/Rocks.ts`
里**逐个静态 import**，所以**新增单位必须在 `ROCK_FILES` 里登记一行**。

## 目录内容

| 文件 | 说明 |
| --- | --- |
| `palettes.json` | 色板：36 个岩性色阶（按地质类别分组）+ 4 个覆被色。**岩性色直接对齐 `../blocks.json`** |
| `rocks.json` | 单位索引（生成物，仅供查阅）：每单位覆盖哪些区块、用哪些岩性色 |
| `rock.boulder.json` | 巨砾 —— 通用大石（火成） |
| `rock.block.json` | 岩块 —— 露出基岩（火成） |
| `rock.ledge.json` | 石台 —— 台阶状岩台（沉积） |
| `rock.slab.json` | 板岩席 —— 层理薄板（变质） |
| `rock.shard.json` | 碎岩 —— 棱角碎片（变质，**唯一覆盖全部 19 个"无专用地质方块"区块的单位**） |
| `rock.spire.json` | 尖石 —— 收尖竖石（火成） |
| `rock.stub.json` | 石墩 —— 矮粗石柱（沉积） |
| `rock.rubble.json` | 碎石堆 —— 松散石堆（松散堆积） |
| `rock.floe.json` | 冰岩 —— 冰壳 / 冰块（冰） |

## 一块石头怎么覆盖所有区块

三层叠加，任一层都可以省略：

1. **几何** —— `archetype`（9 个原型之一）决定形状；
2. **岩性** —— `family` + `palette` 决定默认颜色，`chunkPalette` 决定"在哪个区块换成哪种岩"；
3. **选用** —— `tags` 里的 `chunk:<区块id或key>` 决定它出现在哪些区块。

所以同一个 `boulder` 几何在山丘是花岗岩、在高原是玄武岩、在亚热带灌木林是石灰岩、
在寒带雪山是披雪的冻裂岩 —— 模型只需要 9 个。

## 字段

见 `.fordev_docs/rocks.md` §2。要点：

- **不写 `params`**：形状数字（直径 / 层数 / 收缩比…）写在
  `archetypes.ts` 的 `ROCK_ARCHETYPE_DEFAULTS` 里，"形状参数与单位一一对应"，
  两处各写一份会漂移。将来要变体时才往 `params` 里写覆盖值。
- `palette` 的槽名只能是 `body` / `band` / `cap` / `base` / `crust`；
- `chunkPalette` 的键是 `../chunk_types.json` 里的**区块 id**，且不能指向虚空；
- 色板键**只能引用 `palettes.json` 里已有的键**，写错会在加载时直接抛错。

## 重出衍生资源

```text
node 000base\tools\rock-index.mjs      # 本目录的 rocks.json
node 000base\tools\rock-manifest.mjs   # content\assets\models\rocks\manifest.json
node 000base\tools\rock-preview.mjs    # .fordev_docs\src\systems\world\rocks\docimg_rocks-preview\*.png
```

📖 完整文档：**[`.fordev_docs/rocks.md`](../../../../.fordev_docs/rocks.md)**
🖼 预览图：[`.fordev_docs/rocks-preview/`](../../../../.fordev_docs/rocks-preview/)
