"""Extract the clock glyphs from the pinned official PebbleOS PBFs.

Usage:
  python scripts/extract-fonts.py PebbleOS
      write public/fonts/clock-fonts.json for every set in SETS
  python scripts/extract-fonts.py PebbleOS --list PATTERN
      print the height and printable characters of the matching PBFs
"""

import contextlib
import hashlib
import io
import json
import subprocess
import sys
import tempfile
from pathlib import Path

# Clock font sets in the order of the CM_FONT_* indices of
# core/clock_mask.h. Each line names its system font, the top of its text
# box on the watch, and the box height. LECO is the original layout. Bitham
# has no small font with digits, so its date uses BITHAM_30_BLACK, and its
# tops centre the digit '0' on the LECO digit centres (rows 105 and 140.5),
# rounded up.
SETS = [
    {
        'name': 'leco',
        'time': {'font': 'LECO_42_NUMBERS', 'top': 78, 'box': 50},
        'date': {'font': 'LECO_20_BOLD_NUMBERS', 'top': 128, 'box': 30},
    },
    {
        'name': 'bitham',
        'time': {'font': 'BITHAM_42_BOLD', 'top': 79, 'box': 50},
        'date': {'font': 'BITHAM_30_BLACK', 'top': 122, 'box': 40},
    },
]
# Characters each line needs: the digits and its separator.
LINE_CHARACTERS = {'time': '0123456789:', 'date': '0123456789.'}
PBF_PATH = 'resources/normal/base/pbf'


def extract(root, name, characters=None):
    """Metrics and 1-bit pixels of the glyphs of one PBF, keyed by character.
    Only the given characters get pixels; the others get metrics only."""
    from pbf_extract import extract_pbf
    from PIL import Image

    source = root / PBF_PATH / f'{name}.pbf'
    with tempfile.TemporaryDirectory() as dest:
        with contextlib.redirect_stdout(io.StringIO()):
            meta = extract_pbf(str(source), dest)
        glyphs = {}
        for g in meta['glyphs']:
            c = chr(g['codepoint'])
            glyphs[c] = {
                k: g[k]
                for k in ['width', 'height', 'left_offset', 'top_offset', 'advance']
            }
            if characters is not None and c in characters:
                image = Image.open(Path(dest) / g['file']).convert('L')
                glyphs[c]['bits'] = ''.join(
                    '1' if v < 128 else '0' for v in image.tobytes()
                )
    return {
        'glyphs': glyphs,
        'height': meta['max_height'],
        'sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
    }


def list_fonts(root, pattern):
    for source in sorted((root / PBF_PATH).glob(f'{pattern}.pbf')):
        font = extract(root, source.stem)
        printable = ''.join(sorted(c for c in font['glyphs'] if ' ' < c <= '~'))
        print(f'{source.stem} max_height={font["height"]} {printable}')


def main():
    root = Path(sys.argv[1]).resolve()
    sys.path.insert(0, str(root / 'tools/font'))
    if len(sys.argv) == 4 and sys.argv[2] == '--list':
        list_fonts(root, sys.argv[3])
        return
    needed = {}
    for font_set in SETS:
        for line, characters in LINE_CHARACTERS.items():
            needed.setdefault(font_set[line]['font'], set()).update(characters)
    fonts = {}
    for name, characters in needed.items():
        font = extract(root, name, characters)
        missing = characters - font['glyphs'].keys()
        if missing:
            sys.exit(f'{name} lacks {"".join(sorted(missing))}')
        font['glyphs'] = {c: font['glyphs'][c] for c in sorted(characters)}
        fonts[name] = font
    revision = subprocess.check_output(
        ['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True
    ).strip()
    out = {
        'fonts': fonts,
        'sets': SETS,
        'source': {
            'repository': 'https://github.com/coredevices/PebbleOS',
            'revision': revision,
            'license': 'Apache-2.0',
            'path': PBF_PATH,
        },
    }
    Path('public/fonts/clock-fonts.json').write_text(json.dumps(out))


main()
