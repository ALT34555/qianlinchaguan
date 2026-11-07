# 外置语言包

每种语言使用一个目录。内置中文和英文随主程序发布，不依赖这里的文件。

```text
l10n/
  ja/
    manifest.json       语言标识和显示名称
    ui.json             界面及内容翻译
    names/
      flags.json        置旗地自动命名
      people.json       人物命名占位，当前为 {}
```

现有 ja.json 已迁入 ja/ui.json；原文件保存在工作区 003backup/1029-C11文化与语言目录改造前/。
程序只发现子目录里的 manifest.json，不再把根目录下的单个 JSON 当作语言包。

## manifest.json

```json
{
  "schemaVersion": 1,
  "code": "ja",
  "label": "日本語",
  "nameFallback": "en"
}
```

code 为稳定语言标识，建议与目录名相同；label 是设置页和文化选项显示名。
内置 zh-CN、en 不能重复注册。nameFallback 使用 en 或 zh-CN，仅影响缺失的命名词条。
UI 缺词沿用内置中文回退。目录名可以使用 Unicode，JSON 使用 UTF-8。

## UI 与命名分别装载

ui.json 使用键值对象，例如 `{"nav.home":"表紙"}`。
UI 只读取 ui.json，不会把命名词条当作界面翻译。
names/people.json 保留空对象；人物命名功能尚未实现。

## names/flags.json

使用独立平面词表：

- format：模板，仅支持 `{climate}` 和 `{terrain}` 两个占位符。
- climate.1 .. climate.5：常年冰封、寒带、温带、亚热带、热带。
- terrain.<材质>.<形态>：材质 0..5 为承托物、岩、土壤、盐碱土壤、沙、砾；形态 1..5 为平坦、凹地、隆起、陡峭、雕刻。
- fallback.0 .. fallback.3：类型未知、非法或模板无法完成时的通用自动名。

例如：

```json
{
  "format": "{climate}{terrain}の地",
  "climate.2": "寒冷な",
  "terrain.4.1": "砂原",
  "fallback.0": "遠野の地",
  "fallback.1": "初見の地",
  "fallback.2": "新垣の地",
  "fallback.3": "未名の地"
}
```

ja/names/flags.json 中提供完整词表，可作为新语言的起点。格式不是 printf，不支持 %s 指令。
非空命名表会把该语言加入文化选择和混合文化候选。混合按阵营 ID 确定性选择一种候选文化，不混拼不同语言的词。

文化默认取新建世界时的当前语言。自动名在创建时生成，并连同具体文化写入存档。
切换界面语言、更新或移除语言包不会修改已保存的地名；新生成阵营才会使用当前可用词表。
显式自定义名称优先于自动名；类型无效时使用通用自动名，不显示 ID 代替名字。
