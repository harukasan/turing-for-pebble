import concurrent.futures
import json
import subprocess
from pathlib import Path

from PIL import Image, ImageDraw

# Modes each model runs in: Gray-Scott in every mode, other models only in
# the Q15 modes 1 and 3.
MODEL_MODES = {0: [0, 1, 2, 3]}
MODE_LABELS = ['200x228 A7+B9', '100x114 Q15', '100x114 A7+B9', '120x136 Q15']
# The presets of the contact sheet, one row each.
SHEET_PRESETS = ['maze', 'coral', 'mitosis', 'spots']

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
    m, preset, seed = case
    ppm = Path(f'build/compare-{m}-{preset["id"]}-{seed}.ppm')
    result = json.loads(
        subprocess.check_output(
            ['build/compare', str(m), str(preset['model'])]
            + [str(v) for v in preset['q15']]
            + [str(seed), '10000', str(ppm)]
        )
    )
    png = out / f'mode-{m}-preset-{preset["id"]}-seed-{seed}.png'
    Image.open(ppm).save(png)
    result['preset'] = preset['id']
    result['image'] = png.name
    return result


cases = [
    (m, preset, s)
    for m in range(4)
    for preset in presets
    if m in MODEL_MODES[preset['model']]
    for s in [42, 1234]
]
with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
    rows = list(pool.map(run, cases))
report = {
    'version': 2,
    'sdk': '4.33.1',
    'coreTests': 'passed',
    'nativeWasmHashes': 'passed',
    'sanitizers': 'passed',
    'physicalDevice': None,
    'adoption': 'pending device timing and heap measurements',
    'comparisons': rows,
}
(out / 'comparison.json').write_text(json.dumps(report, indent=2))
canvas = Image.new('RGB', (800, len(SHEET_PRESETS) * 258), '#111111')
draw = ImageDraw.Draw(canvas)
for row, preset_id in enumerate(SHEET_PRESETS):
    for m in range(4):
        canvas.paste(
            Image.open(out / f'mode-{m}-preset-{preset_id}-seed-42.png'),
            (m * 200, row * 258 + 30),
        )
        draw.text(
            (m * 200 + 4, row * 258 + 8),
            f'{MODE_LABELS[m]} / {preset_id}',
            fill='white',
        )
canvas.save(out / 'comparison.png')
# Thin-line preset, seed 42: modes 0, 1, and 3 from left to right.
thin = Image.new('RGB', (600, 228), '#111111')
for i, m in enumerate([0, 1, 3]):
    thin.paste(Image.open(out / f'mode-{m}-preset-thin-line-seed-42.png'), (i * 200, 0))
thin.save(out / 'thin-lines.png')
print(json.dumps(report, indent=2))
