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

# The watchfaces of build-pebble.sh: output name, mode, and clock faces,
# both selected by the face setting or the analog face fixed.
variants = [
    ('0', 0, 'both'),
    ('1', 1, 'both'),
    ('3', 3, 'both'),
    ('3-analog', 3, 'analog'),
]


def newest(name, mode, face):
    """The newest physical-watch measurement of a build with a face shown,
    if any. Entries without a build were measured with build-pebble.sh's
    build of their mode, and entries without a face with the digital
    face."""
    return next(
        (
            m
            for m in reversed(hardware)
            if m['mode'] == mode
            and m.get('build', str(mode)) == name
            and m.get('face', 'digital') == face
        ),
        None,
    )


for name, mode, faces in variants:
    core_bytes = next(c['coreBytes'] for c in r['comparisons'] if c['mode'] == mode)
    values = (
        subprocess.check_output(
            ['arm-none-eabi-size', f'build/pebble/mode-{name}.elf'], text=True
        )
        .splitlines()[1]
        .split()
    )
    text, data, bss = map(int, values[:3])
    by_face = {
        face: newest(name, mode, face)
        for face in (('digital', 'analog') if faces == 'both' else (faces,))
    }
    # The physical* fields describe the face a build shows by default.
    physical = next(iter(by_face.values()))
    stack = {}
    for f in Path(f'build/pebble/stack-{name}').glob('*.su'):
        for line in f.read_text().splitlines():
            place, size, kind = line.split('\t')
            stack[place.split(':')[-1]] = int(size)
    # Each app function at most once on the active nonrecursive call path.
    # Sum is deliberately conservative and excludes the OS call stack.
    r['arm'].append(
        {
            'mode': mode,
            'faces': faces,
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
            'physicalMeasurementsByFace': by_face,
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
    {
        # Mode 3 with the face setting analog: the hands at 452 and 1104
        # (15:46) and the date, against cm_build_analog at halo 0. The
        # halo pixels that are not black show the palette's first step, 27
        # on the row under the date and one at the minute hand's tip.
        'screenshot': 'emery-mode-3-analog.png',
        'font': 'leco',
        'face': 'analog',
        'time': '15:46',
        'date': '2026.09.27',
        'glyphPixels': 1556,
        'missing': 0,
        'extra': 0,
        'haloPixels': 965,
        'haloNotBlack': 28,
    },
]
r['browserUiVerification'] = 'unavailable: no connected browser'
r['emulatorTiming'] = (
    'RTC seconds and millisecond ticks are not synchronized. clock_invalid marks discontinuities. Emulator timing cannot qualify hardware.'
)
out.write_text(json.dumps(r, indent=2) + '\n')
