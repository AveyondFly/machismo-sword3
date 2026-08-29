#!/usr/bin/env python3
"""Crop native menu chrome from grim 960x720 shots into assets/host_menu.

Sources (not in git):
  /tmp/sword3-book.png      天书
  /tmp/full-menu1.png       物品 list
"""

from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageFilter

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "assets" / "host_menu"
BOOK = Path("/tmp/sword3-book.png")
ITEMS = Path("/tmp/full-menu1.png")
SPLIT = 282


def jpeg(im: Image.Image, path: Path, quality: int = 86) -> None:
    im.convert("RGB").save(path, quality=quality, optimize=True, progressive=True)


def main() -> None:
    book = Image.open(BOOK).convert("RGBA")
    items = Image.open(ITEMS).convert("RGBA")
    w, h = book.size
    if (w, h) != (960, 720):
        raise SystemExit(f"unexpected book size {w}x{h}")

    OUT.mkdir(parents=True, exist_ok=True)

    jpeg(book.crop((0, 0, SPLIT, h)), OUT / "left.jpg")
    jpeg(book.crop((8, 220, SPLIT - 6, h - 8)), OUT / "paper.jpg")

    right = book.crop((SPLIT, 0, w, h))
    rw, rh = right.size
    patch = book.crop((SPLIT + 20, 340, w - 20, 680))
    dark = Image.new("RGBA", (rw, rh))
    pw, ph = patch.size
    for y in range(0, rh, ph):
        for x in range(0, rw, pw):
            dark.paste(patch, (x, y))
    dark.paste(right.crop((0, 300, rw, rh)), (0, 300))
    top = dark.crop((0, 0, rw, 320)).filter(ImageFilter.GaussianBlur(radius=0.6))
    dark.paste(top, (0, 0))
    jpeg(dark, OUT / "dark.jpg")

    xs = [302, 417, 531, 645, 759]
    names = ["book_save", "book_load", "book_log", "book_opt", "book_leave"]
    for x, name in zip(xs, names):
        book.crop((x - 1, 76, x + 105, 218)).save(OUT / f"{name}.png", optimize=True)

    book.crop((868, 76, 956, 164)).save(OUT / "back.png", optimize=True)
    jpeg(items.crop((SPLIT + 8, 70, 880, 250)), OUT / "item_chrome.jpg")

    for p in sorted(OUT.iterdir()):
        print(f"{p.name:16s} {p.stat().st_size:7d}")


if __name__ == "__main__":
    main()
