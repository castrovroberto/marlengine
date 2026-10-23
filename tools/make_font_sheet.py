#!/usr/bin/env python3
"""Rasterizes a monospaced TTF into the glyph sheet BitmapFont loads.

Usage: make_font_sheet.py [font.ttf] [output.png]

Layout: 16 columns of 16x32 pixel cells, first cell top-left, ASCII 32 (space) to 127 in reading
order, so 256x192 pixels. Glyphs are white with an antialiased alpha channel; tint them when drawing.
"""
import sys
from PIL import Image, ImageDraw, ImageFont

FONT = sys.argv[1] if len(sys.argv) > 1 else "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"
OUT = sys.argv[2] if len(sys.argv) > 2 else "Assets/Fonts/mono.png"
CELL_W, CELL_H, COLUMNS, FIRST, LAST = 16, 32, 16, 32, 127
SIZE = 26   # pixel size at which this font's advance (about 0.6 em) just fits a 16 pixel cell

font = ImageFont.truetype(FONT, SIZE)
ascent, descent = font.getmetrics()
baseline = (CELL_H - (ascent + descent)) // 2 + ascent   # center the line box vertically in the cell

rows = (LAST - FIRST) // COLUMNS + 1
sheet = Image.new("RGBA", (COLUMNS * CELL_W, rows * CELL_H), (255, 255, 255, 0))
draw = ImageDraw.Draw(sheet)
for code in range(FIRST, LAST):   # 127 (DEL) is left blank
    index = code - FIRST
    x = (index % COLUMNS) * CELL_W + (CELL_W - font.getlength(chr(code))) / 2
    y = (index // COLUMNS) * CELL_H + baseline
    draw.text((x, y), chr(code), font=font, fill=(255, 255, 255, 255), anchor="ls")

# keep color white and carry the glyph coverage in alpha
alpha = sheet.getchannel("A")
Image.merge("RGBA", (alpha.point(lambda _: 255),) * 3 + (alpha,)).save(OUT)
print(f"wrote {OUT}: {sheet.width}x{sheet.height}, {CELL_W}x{CELL_H} cells, codes {FIRST}-{LAST - 1}")
