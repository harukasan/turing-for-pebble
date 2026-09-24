"""Merge measured ARM sections and conservative application stack analysis."""

import json
import subprocess
from pathlib import Path

out = Path('public/reports/comparison.json')
r = json.loads(out.read_text())
r['arm'] = []
measurements = json.loads(Path('docs/emulator-measurements.json').read_text())[
    'measurements'
]
for mode in [0, 1]:
    core_bytes = next(c['coreBytes'] for c in r['comparisons'] if c['mode'] == mode)
    values = (
        subprocess.check_output(
            ['arm-none-eabi-size', f'build/pebble/mode-{mode}.elf'], text=True
        )
        .splitlines()[1]
        .split()
    )
    text, data, bss = map(int, values[:3])
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
            'minimumFreeHeapBytes': measurements[mode]['minimumFreeHeapBytes'],
            'emulatorObservation': measurements[mode],
            'physicalComputeAndDrawMs': None,
        }
    )
r['fontVerification'] = {
    'screenshot': 'emery-mode-0.png',
    'time': '14:50',
    'date': '2026.09.24',
    'expectedWhitePixels': 2427,
    'actualWhitePixels': 2427,
    'missing': 0,
    'extra': 0,
}
r['browserUiVerification'] = 'unavailable: no connected browser'
r['emulatorTiming'] = (
    'RTC seconds and millisecond ticks are not synchronized. clock_invalid marks discontinuities. Emulator timing cannot qualify hardware.'
)
out.write_text(json.dumps(r, indent=2) + '\n')
