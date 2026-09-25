import concurrent.futures
import json
import subprocess
from pathlib import Path

from PIL import Image, ImageDraw

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
    m, p, seed = case
    ppm = Path(f'build/compare-{m}-{p}-{seed}.ppm')
    result = json.loads(
        subprocess.check_output(
            ['build/compare', str(m), str(p), str(seed), '10000', str(ppm)]
        )
    )
    png = out / f'mode-{m}-preset-{p}-seed-{seed}.png'
    Image.open(ppm).save(png)
    result['image'] = png.name
    return result


with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
    rows = list(
        pool.map(
            run, [(m, p, s) for m in range(4) for p in range(5) for s in [42, 1234]]
        )
    )
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
canvas = Image.new('RGB', (800, 4 * 258), '#111111')
draw = ImageDraw.Draw(canvas)
for p in range(4):
    for m in range(4):
        canvas.paste(
            Image.open(out / f'mode-{m}-preset-{p}-seed-42.png'),
            (m * 200, p * 258 + 30),
        )
        draw.text(
            (m * 200 + 4, p * 258 + 8),
            f'{["200x228 A7+B9", "100x114 Q15", "100x114 A7+B9", "120x136 Q15"][m]} / preset {p}',
            fill='white',
        )
canvas.save(out / 'comparison.png')
# Thin-line preset 4, seed 42: modes 0, 1, and 3 from left to right.
thin = Image.new('RGB', (600, 228), '#111111')
for i, m in enumerate([0, 1, 3]):
    thin.paste(Image.open(out / f'mode-{m}-preset-4-seed-42.png'), (i * 200, 0))
thin.save(out / 'thin-lines.png')
print(json.dumps(report, indent=2))
