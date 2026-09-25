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
hardware = json.loads(Path('docs/hardware-measurements.json').read_text())[
    'measurements'
]
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
    emulator = next((m for m in measurements if m.get('mode') == mode), None)
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
            'minimumFreeHeapBytes': None
            if emulator is None
            else emulator['minimumFreeHeapBytes'],
            'emulatorObservation': emulator,
            'physicalComputeAndDrawMs': None
            if physical is None
            else round(
                (
                    physical['avgStepUs'] * 8
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
