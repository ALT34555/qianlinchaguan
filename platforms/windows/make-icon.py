#!/usr/bin/env python3
"""茜林茶馆 · Windows 启动器图标生成器
================================================================================
从项目自带的圆体字库生成 qianlin.ico（多尺寸 ICO，供桌面快捷方式与文件夹使用）。

依赖：Pillow（仅生成图标时需要，运行游戏不需要）。
用法：
    python platforms/windows/make-icon.py
输出：
    platforms/windows/qianlin.ico   （16 / 24 / 32 / 48 / 64 / 128 / 256）
================================================================================
"""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parent.parent
FONT = PROJECT / "content" / "assets" / "ui" / "fonts" / "ResourceHanRoundedCN-Medium.ttf"
OUTPUT = HERE / "qianlin.ico"

# 取色自 src/ui/menu.css 的品牌绿与米白，保证与开始界面同一套色系。
TOP = (41, 78, 59)        # #294e3b
BOTTOM = (24, 48, 36)     # #183024
CREAM = (250, 251, 245)   # #fafbf5
GOLD = (206, 172, 111)    # #ceac6f

SIZE = 1024               # 先按 1024 绘制，再交给 Pillow 逐尺寸降采样
GLYPH = "茜"


def vertical_gradient(size: int, top: tuple, bottom: tuple) -> Image.Image:
    gradient = Image.new("RGB", (1, size))
    pixels = gradient.load()
    for y in range(size):
        ratio = y / max(1, size - 1)
        pixels[0, y] = tuple(round(top[i] + (bottom[i] - top[i]) * ratio) for i in range(3))
    return gradient.resize((size, size), Image.Resampling.NEAREST)


def build() -> Image.Image:
    if not FONT.exists():
        raise SystemExit(f"找不到字体文件：{FONT}")
    radius = round(SIZE * 0.22)
    # 圆角遮罩：四倍超采样后缩放，让圆角边缘平滑。
    mask = Image.new("L", (SIZE * 4, SIZE * 4), 0)
    ImageDraw.Draw(mask).rounded_rectangle(
        (0, 0, SIZE * 4 - 1, SIZE * 4 - 1), radius=radius * 4, fill=255)
    mask = mask.resize((SIZE, SIZE), Image.Resampling.LANCZOS)

    icon = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    icon.paste(vertical_gradient(SIZE, TOP, BOTTOM), (0, 0), mask)

    draw = ImageDraw.Draw(icon)
    # 极细金色内描边，暗示"茶馆"的器物感；小尺寸下降采样后自然淡出，不会糊成脏边。
    inset = round(SIZE * 0.055)
    draw.rounded_rectangle((inset, inset, SIZE - inset - 1, SIZE - inset - 1),
                           radius=round(radius * 0.78), outline=GOLD + (110,),
                           width=max(2, round(SIZE * 0.006)))
    # 中央"茜"字：按字形实际包围盒居中，而不是按行高居中。
    font = ImageFont.truetype(str(FONT), round(SIZE * 0.62))
    box = draw.textbbox((0, 0), GLYPH, font=font)
    draw.text(((SIZE - (box[2] - box[0])) / 2 - box[0], (SIZE - (box[3] - box[1])) / 2 - box[1]),
              GLYPH, font=font, fill=CREAM)
    return icon


def main() -> None:
    icon = build()
    sizes = [(s, s) for s in (256, 128, 64, 48, 32, 24, 16)]
    icon.save(OUTPUT, format="ICO", sizes=sizes)
    icon.resize((256, 256), Image.Resampling.LANCZOS).save(OUTPUT.with_suffix(".png"))
    print(f"已生成 {OUTPUT}（{OUTPUT.stat().st_size} 字节）")
    print(f"已生成 {OUTPUT.with_suffix('.png')}（预览用 256x256 PNG）")


if __name__ == "__main__":
    main()
