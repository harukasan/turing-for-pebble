"""Compare the clock glyph pixels against a screenshot.

Usage: python scripts/verify-fonts.py PNG HH:MM YYYY.MM.DD [--font leco|bitham]
       [--halo N]

The white pixels of the screenshot must be exactly the glyph pixels of the
font set in public/fonts/clock-fonts.json. With --halo, every other pixel
within N pixels of a glyph pixel (a square) must be black, the background
of the cells held at the equilibrium.
"""

import argparse
import json

from PIL import Image

parser = argparse.ArgumentParser()
parser.add_argument('png')
parser.add_argument('time')
parser.add_argument('date')
parser.add_argument('--font', default='leco')
parser.add_argument('--halo', type=int)
args = parser.parse_args()

with open('public/fonts/clock-fonts.json') as f:
    data = json.load(f)
font_set = next(s for s in data['sets'] if s['name'] == args.font)
im = Image.open(args.png).convert('RGB')
expected = set()
for text, line in [(args.time, font_set['time']), (args.date, font_set['date'])]:
    glyphs = data['fonts'][line['font']]['glyphs']
    x = (200 - sum(glyphs[c]['advance'] for c in text)) // 2
    for c in text:
        g = glyphs[c]
        for y in range(g['height']):
            for xx in range(g['width']):
                if g['bits'][y * g['width'] + xx] == '1':
                    expected.add(
                        (x + g['left_offset'] + xx, line['top'] + g['top_offset'] + y)
                    )
        x += g['advance']
actual = {
    (x, y)
    for y in range(228)
    for x in range(200)
    if im.getpixel((x, y)) == (255, 255, 255)
}
result = {
    'expectedPixels': len(expected),
    'actualPixels': len(actual),
    'missing': len(expected - actual),
    'extra': len(actual - expected),
}
halo_pixels = set()
if args.halo is not None:
    h = args.halo
    halo_pixels = {
        (x + dx, y + dy)
        for x, y in expected
        for dx in range(-h, h + 1)
        for dy in range(-h, h + 1)
        if 0 <= x + dx < 200 and 0 <= y + dy < 228
    } - expected
    result['haloPixels'] = len(halo_pixels)
    result['haloNotBlack'] = sum(1 for p in halo_pixels if im.getpixel(p) != (0, 0, 0))
print(json.dumps(result))
assert expected == actual
assert not result.get('haloNotBlack')
