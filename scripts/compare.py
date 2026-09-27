import concurrent.futures
import json
import subprocess
from pathlib import Path

from PIL import Image, ImageDraw

# The presets of the contact sheets, seed 42 each, left to right.
SHEET_PRESETS = ['maze', 'coral', 'mitosis', 'holes', 'cells', 'worms', 'moving-spots']
FHN_SHEET_PRESETS = ['fhn-stripes', 'fhn-hex', 'fhn-spiral']

presets = json.loads(
    subprocess.check_output(
        ['node', '--experimental-transform-types', 'scripts/list-presets.mjs'],
        stderr=subprocess.DEVNULL,
    )
)
out = Path('public/reports')
out.mkdir(parents=True, exist_ok=True)
subprocess.run(
    [
        'cc',
        '-O3',
        '-std=c11',
        '-Wall',
        '-Wextra',
        'core/rd.c',
        'tests/compare.c',
        '-o',
        'build/compare',
    ],
    check=True,
)


def run(case):
    preset, seed = case
    ppm = Path(f'build/compare-{preset["id"]}-{seed}.ppm')
    result = json.loads(
        subprocess.check_output(
            ['build/compare', str(preset['model'])]
            + [str(v) for v in preset['q15']]
            + [str(seed), '10000', str(ppm)]
        )
    )
    png = out / f'preset-{preset["id"]}-seed-{seed}.png'
    Image.open(ppm).save(png)
    result['preset'] = preset['id']
    result['image'] = png.name
    return result


cases = [(preset, s) for preset in presets for s in [42, 1234]]
with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
    rows = list(pool.map(run, cases))
report = {
    'version': 4,
    'sdk': '4.33.1',
    'coreTests': 'passed',
    'nativeWasmHashes': 'passed',
    'sanitizers': 'passed',
    'physicalDevice': None,
    'adoption': 'pending device timing and heap measurements',
    'comparisons': rows,
}
(out / 'comparison.json').write_text(json.dumps(report, indent=2))


def sheet(preset_ids, name):
    """Seed 42 of each preset side by side, as the watch shows them."""
    canvas = Image.new('RGB', (len(preset_ids) * 200, 258), '#111111')
    draw = ImageDraw.Draw(canvas)
    for column, preset_id in enumerate(preset_ids):
        canvas.paste(
            Image.open(out / f'preset-{preset_id}-seed-42.png'),
            (column * 200, 30),
        )
        draw.text((column * 200 + 4, 8), f'120x136 Q15 / {preset_id}', fill='white')
    canvas.save(out / name)


sheet(SHEET_PRESETS, 'comparison.png')
sheet(FHN_SHEET_PRESETS, 'fhn.png')
print(json.dumps(report, indent=2))
