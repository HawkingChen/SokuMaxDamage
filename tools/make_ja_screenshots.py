#!/usr/bin/env python3
"""Render the Japanese-localized variants of the two README screenshots.

The MOD draws its companion window into a 1000x290 logical canvas
(src/mod/StyledPanel.hpp) and writes every fixed string through
whm::hudText()/kEffectLabels (src/core/HudText.hpp).  This tool takes the
Chinese screenshots captured on Steam Deck and re-draws only the text
layers, using the exact Japanese strings from those two tables.  Card art,
numbers, frames and the game frame itself are left untouched.

Requires Pillow and the system Noto Sans CJK JP font.  Rerun this whenever
the Chinese source screenshots or the Japanese strings in HudText.hpp change.

Usage:  python3 tools/make_ja_screenshots.py
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
DOCS = ROOT / "docs"
JP_FONT = "/usr/share/fonts/noto-cjk/NotoSansCJK-Bold.ttc"

# COLORREF constants in paintStyledPanel().
CREAM = (252, 236, 208)
BRIGHT = (255, 226, 161)
FADED = (185, 180, 203)
LAVENDER = (206, 181, 224)
FOOTER = (187, 162, 147)
WEATHER = (198, 181, 219)
FIRST_HIT = (238, 177, 104)
GOLD = (230, 182, 94)


@dataclass
class TextItem:
    """One write() call expressed in the 1000x290 logical canvas."""

    rect: tuple[int, int, int, int]  # x, y, w, h (logical)
    text: str
    size: int
    color: tuple[int, int, int]
    align: str = "left"
    # Search window for the old glyph pixels; defaults to the write() rect.
    window: tuple[int, int, int, int] | None = None
    threshold: str = "light"
    dy: float = 0.0
    # "tight" inpaints only the detected glyph box; "row" clears the whole
    # write() rect (or `erase`) using the dark background at both row ends.
    inpaint: str = "tight"
    erase: tuple[int, int, int, int] | None = None


@dataclass
class Shot:
    source: str
    target: str
    # Logical -> pixel mapping: px = ox + x * sx, py = oy + y * sy
    ox: int
    oy: int
    sx: float
    sy: float
    items: list[TextItem] = field(default_factory=list)
    swap_language_buttons: bool = True


def light_pixel(p: tuple[int, int, int], threshold: str) -> bool:
    r, g, b = p
    if threshold == "orange":
        return r >= 195 and 145 <= g <= 205 and 75 <= b <= 135
    # GDI text is bright and warm/cool light; gold frame lines (b=94),
    # the dark purple backdrop and the brown ribbon are all excluded.
    return r >= 150 and g >= 125 and b >= 120


def tight_bbox(img: Image.Image, window: tuple[int, int, int, int],
               threshold: str) -> tuple[int, int, int, int] | None:
    """Smallest box containing the actual light glyph pixels in a window."""
    x0, y0, x1, y1 = window
    pix = img.load()
    rows: dict[int, int] = {}
    cols: dict[int, int] = {}
    for y in range(y0, y1):
        for x in range(x0, x1):
            if light_pixel(pix[x, y][:3], threshold):
                rows[y] = rows.get(y, 0) + 1
                cols[x] = cols.get(x, 0) + 1
    ys = [y for y, n in rows.items() if n >= 3]
    xs = [x for x, n in cols.items() if n >= 2]
    if not ys or not xs:
        return None
    return min(xs), min(ys), max(xs) + 1, max(ys) + 1


def median3(samples):
    return tuple(sorted(s[c] for s in samples)[len(samples) // 2] for c in range(3))


def erase(img: Image.Image, box: tuple[int, int, int, int]) -> None:
    """Inpaint a text box column by column from the background above/below.

    The panel backdrop is drawn as horizontal gradient rows, so vertical
    interpolation preserves the horizontal structure.  Neighbouring glyph
    rows are skipped when sampling the two endpoints.
    """
    pix = img.load()
    x0, y0, x1, y1 = box
    x0, y0 = max(0, x0 - 2), max(0, y0 - 2)
    x1, y1 = min(img.width, x1 + 2), min(img.height, y1 + 2)
    if x0 >= x1 or y0 >= y1:
        return
    for x in range(x0, x1):
        top, bottom = [], []
        for k in range(6):
            yy = y0 - 1 - k
            if yy >= 0 and not light_pixel(pix[x, yy][:3], "light"):
                top.append(pix[x, yy][:3])
            yy = y1 + k
            if yy < img.height and not light_pixel(pix[x, yy][:3], "light"):
                bottom.append(pix[x, yy][:3])
        a = median3(top) if top else pix[x, max(0, y0 - 1)][:3]
        b = median3(bottom) if bottom else pix[x, min(img.height - 1, y1)][:3]
        span = max(1, y1 - y0 - 1)
        for y in range(y0, y1):
            t = (y - y0) / span
            pix[x, y] = tuple(round(a[c] * (1 - t) + b[c] * t) for c in range(3)) + (255,)


def dark_median(samples):
    """Median of the darker half: backdrop pixels win over bright glyphs."""
    if not samples:
        return None
    ordered = sorted(samples, key=sum)
    half = ordered[:max(1, len(ordered) // 2)]
    return tuple(sorted(c[i] for c in half)[len(half) // 2] for i in range(3))


def erase_row(img: Image.Image, box: tuple[int, int, int, int]) -> None:
    """Clear a whole text row by interpolating its two dark background ends.

    Rows of Chinese glyphs with faint anti-aliased edges do not always form a
    complete bright-pixel box; this mode removes them without leaving ghosts.
    """
    pix = img.load()
    x0, y0, x1, y1 = box
    x0, y0 = max(0, x0 - 2), max(0, y0 - 2)
    x1, y1 = min(img.width, x1 + 2), min(img.height, y1 + 2)
    if x0 >= x1 or y0 >= y1:
        return
    for y in range(y0, y1):
        left = [pix[x, y][:3] for x in range(max(0, x0 - 16), x0)]
        right = [pix[x, y][:3] for x in range(x1, min(img.width, x1 + 16))]
        a = dark_median(left) or pix[max(0, x0 - 1), y][:3]
        b = dark_median(right) or pix[min(img.width - 1, x1), y][:3]
        span = max(1, x1 - x0 - 1)
        for x in range(x0, x1):
            t = (x - x0) / span
            pix[x, y] = tuple(round(a[c] * (1 - t) + b[c] * t) for c in range(3)) + (255,)


def logical_box(shot: Shot, rect: tuple[int, int, int, int]) -> tuple[int, int, int, int]:
    x, y, w, h = rect
    return (round(shot.ox + x * shot.sx), round(shot.oy + y * shot.sy),
            round(shot.ox + (x + w) * shot.sx), round(shot.oy + (y + h) * shot.sy))


def draw_text(img: Image.Image, shot: Shot, item: TextItem) -> None:
    px0, py0, px1, py1 = logical_box(shot, item.rect)
    size = max(8, round(item.size * shot.sx))
    font = ImageFont.truetype(JP_FONT, size, index=0)
    layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(layer)
    if item.align == "center":
        x, anchor = (px0 + px1) / 2, "mm"
    else:
        x, anchor = px0, "lm"
    y = (py0 + py1) / 2 + item.dy * shot.sy
    draw.text((x, y), item.text, font=font, fill=item.color + (255,), anchor=anchor)
    img.paste(Image.alpha_composite(img, layer), (0, 0))


def redraw_buttons(img: Image.Image, shot: Shot) -> None:
    """Move the gold selection frame from CN to JP (selected == language)."""
    def box(b: tuple[int, int, int, int]) -> tuple[int, int, int, int]:
        return logical_box(shot, b)

    draw = ImageDraw.Draw(img)
    for rect, caption, active in (
        ((910, 204, 28, 28), "CN", False),
        ((944, 204, 28, 28), "JP", True),
    ):
        x0, y0, x1, y1 = box(rect)
        draw.rounded_rectangle(
            (x0, y0, x1, y1), radius=round(5 * shot.sx),
            fill=(96, 64, 44) if active else (28, 23, 44),
            outline=GOLD if active else (103, 78, 130), width=1,
        )
        font = ImageFont.truetype(JP_FONT, max(8, round(11 * shot.sx)), index=0)
        draw.text(((x0 + x1) / 2, (y0 + y1) / 2), caption, font=font,
                  fill=BRIGHT if active else CREAM, anchor="mm")


def render(shot: Shot) -> None:
    img = Image.open(DOCS / shot.source).convert("RGBA")
    boxes = []
    for item in shot.items:
        if item.inpaint == "row":
            boxes.append((logical_box(shot, item.erase or item.rect), "row"))
            continue
        rect = item.window or item.rect
        found = tight_bbox(img, logical_box(shot, rect), item.threshold)
        if found:
            boxes.append((found, "tight"))
        else:
            print(f"  warning: no glyphs found for {item.text!r} in {rect}")
    for box, mode in boxes:
        (erase_row if mode == "row" else erase)(img, box)
    for item in shot.items:
        draw_text(img, shot, item)
    if shot.swap_language_buttons:
        redraw_buttons(img, shot)
    out = DOCS / shot.target
    img.save(out, optimize=True)
    print(f"wrote {out.relative_to(ROOT)} ({img.width}x{img.height})")


# Strings below are verbatim from src/core/HudText.hpp (ja column),
# kEffectLabels, weatherName() and config/MoveNames.tsv (ja column).
def youmu() -> Shot:
    return Shot(
        source="hud-youmu.png", target="hud-youmu.ja.png",
        ox=0, oy=0, sx=957 / 1000, sy=274 / 290,
        items=[
            TextItem((24, 18, 180, 25), "ダメージ補正", 17, CREAM, "center"),
            TextItem((28, 100, 96, 20), "必殺技: ×1.00", 13, CREAM),
            TextItem((124, 100, 84, 20), "スペカ: ×1.25", 13, CREAM),
            TextItem((83, 129, 116, 23), "霧雨・スペルカード", 12, CREAM,
                     inpaint="row", erase=(83, 128, 116, 24)),
            TextItem((30, 255, 168, 16), "天気 霧雨", 12, WEATHER,
                     inpaint="row", erase=(28, 252, 142, 20)),
            TextItem((256, 10, 309, 28), "最大ダメージ必殺技", 17, CREAM, "center",
                     window=(256, 14, 309, 20)),
            TextItem((327, 56, 250, 37), "弦月斬 · 623C", 20, CREAM),
            TextItem((333, 151, 240, 17), "理論最大値・推定", 11, FOOTER, "center",
                     window=(333, 147, 240, 23)),
            TextItem((640, 10, 309, 28), "最大ダメージ記録", 17, CREAM, "center",
                     window=(640, 14, 309, 20)),
            TextItem((711, 56, 250, 37), "人鬼「未来永劫斬」", 20, CREAM),
            TextItem((717, 151, 240, 17), "この対戦の最大ダメージ", 11, FOOTER, "center",
                     window=(717, 147, 240, 23)),
            TextItem((239, 203, 110, 22), "手札", 17, CREAM),
            TextItem((239, 230, 110, 22), "スペカ最大値", 14, LAVENDER),
        ],
    )


def meiling() -> Shot:
    # The game frame ends at y=706; the HUD keeps the 1000:290 aspect ratio.
    return Shot(
        source="hud-meiling.png", target="hud-meiling.ja.png",
        ox=0, oy=706, sx=949 / 1000, sy=949 / 1000,
        items=[
            TextItem((24, 18, 180, 25), "ダメージ補正", 17, CREAM, "center"),
            TextItem((28, 94, 174, 16), "打撃初段 ×2.67", 11, FIRST_HIT,
                     inpaint="row", erase=(24, 92, 156, 20)),
            TextItem((28, 114, 96, 20), "必殺技: ×1.25", 13, CREAM),
            TextItem((124, 114, 84, 20), "スペカ: ×1.00", 13, CREAM),
            # "制御棒 ×4", "×1.40" and "×1.25" are identical in Japanese.
            # Erase boxes are measured from the actual glyph rows: they stop
            # just short of the unchanged "×1.40" / "×1.25" value rows above.
            TextItem((83, 178, 116, 17), "雹・スキル", 12, CREAM,
                     inpaint="row", erase=(76, 182, 124, 16)),
            TextItem((83, 213, 116, 17), "猛虎内勁・初段", 12, CREAM,
                     inpaint="row", erase=(76, 215, 124, 17)),
            TextItem((83, 230, 116, 17), "×2.67 初段のみ", 16, BRIGHT,
                     inpaint="row", erase=(76, 232, 124, 19)),
            TextItem((30, 255, 168, 16), "天気 雹", 12, WEATHER,
                     inpaint="row", erase=(26, 254, 144, 18)),
            TextItem((256, 10, 309, 28), "最大ダメージ必殺技", 17, CREAM, "center",
                     window=(256, 14, 309, 20)),
            TextItem((640, 10, 309, 28), "最大ダメージ記録", 17, CREAM, "center",
                     window=(640, 14, 309, 20)),
            TextItem((333, 151, 240, 17), "理論最大値・推定", 11, FOOTER, "center",
                     window=(333, 147, 240, 23)),
            TextItem((717, 151, 240, 17), "この対戦の最大ダメージ", 11, FOOTER, "center",
                     window=(717, 147, 240, 23)),
            TextItem((239, 203, 110, 22), "手札", 17, CREAM),
            TextItem((239, 230, 110, 22), "スペカ最大値", 14, LAVENDER),
            TextItem((380, 206, 510, 60), "手札にスペルカードなし", 20, FADED, "center"),
        ],
    )


if __name__ == "__main__":
    render(youmu())
    render(meiling())
