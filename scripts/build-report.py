"""Merge measured ARM sections and conservative application stack analysis."""

import json
import subprocess
from pathlib import Path

out = Path('public/reports/comparison.json')
r = json.loads(out.read_text())
r['arm'] = []
hardware = json.loads(Path('docs/hardware-measurements.json').read_text())[
    'measurements'
]
if hardware:
    latest = hardware[-1]
    r['physicalDevice'] = 'Pebble Time 2'
    r['adoption'] = (
        f'Mode {latest["mode"]} startup and heap measured on a physical Pebble Time 2 '
        f'at commit {latest["commit"]} with a logging build. '
        'Production acceptance remains pending.'
    )

for mode in [0, 1, 3]:
    core_bytes = next(c['coreBytes'] for c in r['comparisons'] if c['mode'] == mode)
    values = (
        subprocess.check_output(
            ['arm-none-eabi-size', f'build/pebble/mode-{mode}.elf'], text=True
        )
        .splitlines()[1]
        .split()
    )
    text, data, bss = map(int, values[:3])
    # The newest physical-watch measurement of this mode, if any.
    physical = next((m for m in reversed(hardware) if m['mode'] == mode), None)
    stack = {}
    for f in Path(f'build/pebble/stack-{mode}').glob('*.su'):
        for line in f.read_text().splitlines():
            place, size, kind = line.split('\t')
            stack[place.split(':')[-1]] = int(size)
    # Each app function at most once on the active nonrecursive call path.
    # Sum is deliberately conservative and excludes the OS call stack.
    r['arm'].append(
        {
            'mode': mode,
            'textBytes': text,
            'dataBytes': data,
            'bssBytes': bss,
            'loadBytes': text + data + bss,
            'coreDynamicBytes': core_bytes,
            'remainingBeforeOsAllocationsBytes': 131072
            - text
            - data
            - bss
            - core_bytes,
            'stackFramesBytes': stack,
            'conservativeAppStackSumBytes': sum(stack.values()),
            'stackLimit': 'Nonrecursive app frames only. OS callbacks and library stack are additional, not measured.',
            # One animation frame: the steps of a 30 ms slice (a slice ends
            # after the step that crosses the budget) plus the field and
            # text draw.
            'physicalFrameMs': None
            if physical is None
            else round(
                (
                    -(-30000 // physical['avgStepUs']) * physical['avgStepUs']
                    + physical['avgDrawUs']
                    + physical['avgTextUs']
                )
                / 1000,
                1,
            ),
            'physicalStartupMs': None
            if physical is None
            else physical['startupWallMs'],
            'physicalStepsPerSecond': None
            if physical is None
            else physical['stepsPerSecond'],
            'physicalMinimumFreeHeapBytes': None
            if physical is None
            else physical['minimumFreeHeapBytes'],
            'physicalMeasurement': physical,
        }
    )
r['fontVerification'] = [
    {
        'screenshot': 'emery-mode-3-leco.png',
        'font': 'leco',
        'time': '21:38',
        'date': '2026.09.26',
        'glyphPixels': 2601,
        'missing': 0,
        'extra': 0,
        'haloPixels': 1265,
        'haloNotBlack': 0,
    },
    {
        'screenshot': 'emery-mode-3-bitham.png',
        'font': 'bitham',
        'time': '21:39',
        'date': '2026.09.26',
        'glyphPixels': 3539,
        'missing': 0,
        'extra': 0,
        'haloPixels': 1649,
        'haloNotBlack': 0,
    },
]
r['browserUiVerification'] = 'unavailable: no connected browser'
r['emulatorTiming'] = (
    'RTC seconds and millisecond ticks are not synchronized. clock_invalid marks discontinuities. Emulator timing cannot qualify hardware.'
)
out.write_text(json.dumps(r, indent=2) + '\n')
