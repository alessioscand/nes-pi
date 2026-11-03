#!/usr/bin/env python3
"""neo-pi - turn neopi/PROFILE.TXT into time per function and per part.

Usage: resolve_profile.py <kernel .elf that produced the profile> <PROFILE.TXT> [top N]

PROFILE.TXT lists 16-byte code buckets as "address samples_all samples_slow".
Each address is matched to the function that contains it (symbols from the
.elf via nm). "slow" = samples taken in frames of 16 ms or more.
"""
import bisect
import collections
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
NM_LOCAL = os.path.join(HERE, '..', 'toolchain', 'xpack-aarch64-none-elf-gcc-14.2.1-1.1',
                        'bin', 'aarch64-none-elf-nm')

# (substring of the function name, part of the system), first match wins
PARTS = [
    ('lspc', 'video chip (sprites, fix layer)'),
    ('geo_m68k', '68000 memory and I/O'),
    ('m68k', '68000 CPU'),
    ('m68ki', '68000 CPU'),
    ('geo_z80', 'Z80 memory and I/O'),
    ('z80', 'Z80 CPU'),
    ('exec_opcode', 'Z80 CPU'),
    ('ymfm', 'YM2610 sound chip'),
    ('ym2610', 'YM2610 sound chip'),
    ('fm_', 'YM2610 sound chip'),
    ('ssg_', 'YM2610 sound chip'),
    ('adpcm', 'YM2610 sound chip'),
    ('resampler', 'resampler (speex)'),
    ('AudioCallback', 'resampler (linear)'),
    ('geo_mixer', 'mixer'),
    ('CVideoOut', 'drawing to HDMI'),
    ('geo_rtc', 'RTC clock'),
    ('geo_exec', 'main loop'),
    ('geo_watchdog', 'main loop'),
    ('memcpy', 'memcpy / memset'),
    ('memset', 'memcpy / memset'),
]


def part_of(name):
    for key, part in PARTS:
        if key in name:
            return part
    return 'other'


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    elf, prof = sys.argv[1], sys.argv[2]
    top = int(sys.argv[3]) if len(sys.argv) > 3 else 25
    nm = NM_LOCAL if os.path.exists(NM_LOCAL) else shutil.which('aarch64-none-elf-nm') or 'nm'

    syms = []
    out = subprocess.run([nm, '-n', '-C', elf], capture_output=True, text=True, check=True).stdout
    for line in out.splitlines():
        f = line.split(' ', 2)
        if len(f) == 3 and f[1] in 'tTwW':
            syms.append((int(f[0], 16), f[2]))
    addrs = [a for a, _ in syms]

    fa, fs = collections.Counter(), collections.Counter()
    pa, ps = collections.Counter(), collections.Counter()
    for line in open(prof):
        if line.startswith('#') or not line.strip():
            continue
        a, x, y = line.split()
        a, x, y = int(a, 16), int(x), int(y)
        i = bisect.bisect_right(addrs, a) - 1
        name = syms[i][1] if i >= 0 else '?'
        fa[name] += x
        fs[name] += y
        pa[part_of(name)] += x
        ps[part_of(name)] += y

    total = sum(fa.values()) or 1
    slow = sum(fs.values()) or 1
    print('PARTS                               all %   slow %')
    for p, v in pa.most_common():
        print(f'  {p:32s} {100 * v / total:6.1f}   {100 * ps[p] / slow:6.1f}')
    print(f'\nTOP {top} FUNCTIONS                   all %   slow %')
    for n, v in fa.most_common(top):
        print(f'  {n[:32]:32s} {100 * v / total:6.1f}   {100 * fs[n] / slow:6.1f}')


if __name__ == '__main__':
    main()
