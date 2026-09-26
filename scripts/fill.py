"""Compare the growth of the pattern on the candidate grids.

Usage: python scripts/fill.py [grids|seeds] [--presets id,id,...]

Runs tests/fill.c for every configuration, preset, and seed with the LECO
clock mask installed, writes build/fill/<study>.jsonl, one contact sheet per
preset and seed under build/fill/ (rows are configurations, columns
checkpoints, all rendered with interpolation), and prints the first
checkpoint at which each run is complete. `grids` compares the grids with
the seeding of rd_init. `seeds` compares disk counts and radius rules on
the 120 x 136 grid against mode 1. The seeds study reseeds mode 3 in the
harness, so the core keeps the seeding of rd_init. --presets takes preset
ids of lib/presets.ts (default maze and thin-line).
"""

import argparse
import concurrent.futures
import json
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageDraw

# Configurations: label -> (mode, disks, min radius, radius range).
STUDIES = {
    'grids': {
        '200x228 packed': (0, 24, 4, 6),
        '100x114 Q15': (1, 24, 4, 6),
        '120x136 Q15': (3, 24, 4, 6),
    },
    'seeds': {
        '100x114 24 r4-9': (1, 24, 4, 6),
        '120 24 r4-9': (3, 24, 4, 6),
        '120 48 r3-6': (3, 48, 3, 4),
        '120 48 r4-9': (3, 48, 4, 6),
        '120 96 r2-4': (3, 96, 2, 3),
        '120 96 r4-9': (3, 96, 4, 6),
        '120 192 r2-4': (3, 192, 2, 3),
        '120 192 r4-9': (3, 192, 4, 6),
    },
}
parser = argparse.ArgumentParser()
parser.add_argument('study', nargs='?', default='grids', choices=list(STUDIES))
parser.add_argument('--presets', default='maze,thin-line')
args = parser.parse_args()
STUDY = args.study
CONFIGS = STUDIES[STUDY]
# Preset id -> model number and Q15 parameter vector (scripts/list-presets.mjs).
ALL_PRESETS = {
    p['id']: p
    for p in json.loads(
        subprocess.check_output(
            ['node', '--experimental-transform-types', 'scripts/list-presets.mjs'],
            stderr=subprocess.DEVNULL,
        )
    )
}
PRESETS = args.presets.split(',')
unknown = [p for p in PRESETS if p not in ALL_PRESETS]
if unknown:
    sys.exit(f'unknown preset ids: {", ".join(unknown)}')
SEEDS = [42, 1234]
CHECKPOINTS = [200, 300, 400, 600, 800, 1000, 1250, 1500, 1800]
# Complete: visible B in nearly every block, and mean B and stripe width
# settled against the checkpoint about 200 steps earlier.
MIN_BLOCKS = 0.98
MAX_B_CHANGE = 0.05
MAX_STRIPE_CHANGE = 0.10

out = Path('build/fill')
out.mkdir(parents=True, exist_ok=True)
subprocess.run(
    [
        'cc',
        '-O3',
        '-std=c11',
        '-Wall',
        '-Wextra',
        'tests/fill.c',
        '-o',
        'build/fill-run',
    ],
    check=True,
)


def prefix(label, preset, seed):
    name = label.replace(' ', '_')
    return out / f'{STUDY}-{name}-preset-{preset}-seed-{seed}'


def run(case):
    label, preset, seed = case
    mode, disks, min_radius, radius_range = CONFIGS[label]
    model, vector = ALL_PRESETS[preset]['model'], ALL_PRESETS[preset]['q15']
    lines = subprocess.check_output(
        ['build/fill-run', str(mode), str(model)]
        + [str(v) for v in vector]
        + [str(v) for v in (seed, disks, min_radius, radius_range)]
        + [str(prefix(label, preset, seed))]
        + [str(c) for c in CHECKPOINTS],
        text=True,
    )
    return [{**json.loads(line), 'preset': preset} for line in lines.splitlines()]


def complete_at(rows):
    """First checkpoint that meets the completion rule, or None."""
    by_step = {r['step']: r for r in rows}
    for r in rows:
        earlier = [s for s in CHECKPOINTS if s <= r['step'] - 150]
        if not earlier:
            continue
        e = by_step[earlier[-1]]
        if (
            r['blocks'] >= MIN_BLOCKS
            and abs(r['bMean'] - e['bMean']) <= MAX_B_CHANGE * r['bMean']
            and abs(r['stripeWidthPx'] - e['stripeWidthPx'])
            <= MAX_STRIPE_CHANGE * r['stripeWidthPx']
        ):
            return r['step']
    return None


cases = [(c, p, s) for c in CONFIGS for p in PRESETS for s in SEEDS]
with concurrent.futures.ThreadPoolExecutor() as pool:
    results = dict(zip(cases, pool.map(run, cases)))
with open(out / f'{STUDY}.jsonl', 'w') as f:
    for rows in results.values():
        f.writelines(json.dumps(r) + '\n' for r in rows)

label_height = 30
for preset in PRESETS:
    for seed in SEEDS:
        sheet = Image.new(
            'RGB',
            (200 * len(CHECKPOINTS) + 110, (228 + label_height) * len(CONFIGS)),
            'white',
        )
        draw = ImageDraw.Draw(sheet)
        for row, label in enumerate(CONFIGS):
            top = row * (228 + label_height)
            draw.text((4, top + 100), label, fill='black')
            for col, r in enumerate(results[(label, preset, seed)]):
                left = 110 + col * 200
                image = Image.open(f'{prefix(label, preset, seed)}-{r["step"]}.ppm')
                sheet.paste(image, (left, top))
                draw.text(
                    (left + 2, top + 229),
                    f'{r["step"]} blk {r["blocks"]:.3f} B {r["bMean"]:.3f}\n'
                    f'stripe {r["stripeWidthPx"]:.1f}px',
                    fill='black',
                )
        sheet.save(out / f'{STUDY}-sheet-preset-{preset}-seed-{seed}.png')

print(
    '| Configuration | Preset | Seed | Complete at | Stripe px at 1800 | Host ms/step |'
)
print('| --- | --- | ---: | ---: | ---: | ---: |')
for (label, preset, seed), rows in results.items():
    last = rows[-1]
    print(
        f'| {label} | {preset} | {seed} | {complete_at(rows) or "-"} '
        f'| {last["stripeWidthPx"]:.1f} | {last["hostMsPerStep"]:.2f} |'
    )
