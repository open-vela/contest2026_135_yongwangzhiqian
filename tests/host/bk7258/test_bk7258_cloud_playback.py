# SPDX-License-Identifier: Apache-2.0
"""Real SpeexDSP + turn arbiter: sample count, alias rejection and ownership."""
import argparse
from pathlib import Path
import math
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
parser = argparse.ArgumentParser()
parser.add_argument('--speex-root', type=Path, required=True,
                    help='SpeexDSP source populated by the OpenVela build')
args = parser.parse_args()
with tempfile.TemporaryDirectory() as directory:
    executable = Path(directory) / 'playback'
    resampler = Path(directory) / 'resampler.o'
    subprocess.run(['cc', '-std=gnu11', '-DOUTSIDE_SPEEX',
                    '-DRANDOM_PREFIX=nuttx', '-DFIXED_POINT',
                    '-I', str(args.speex_root / 'include/speex'),
                    '-c', str(args.speex_root / 'libspeexdsp/resample.c'),
                    '-o', str(resampler)], check=True)
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-DOUTSIDE_SPEEX', '-DRANDOM_PREFIX=nuttx', '-DFIXED_POINT',
                    '-I', str(ROOT / 'app/bk7258'),
                    '-I', str(args.speex_root / 'include/speex'),
                    str(ROOT / 'app/bk7258/bk7258_cloud_playback.c'),
                    str(ROOT / 'app/bk7258/bk7258_voice_turn.c'),
                    str(ROOT / 'tests/host/bk7258/test_bk7258_cloud_playback.c'),
                    str(resampler),
                    '-lm', '-o', str(executable)], check=True)
    def run(samples, chunk, hz=1000, fail=0):
        return subprocess.check_output([str(executable), str(samples), str(chunk),
                                         str(hz), str(fail)])
    for samples in (1, 2, 3, 479, 480, 481, 24000, 24001):
        reference = run(samples, samples * 2)
        for chunk in (1, 2, 3, 7, 14, 319, 320, 479, 960):
            assert run(samples, chunk) == reference, (samples, chunk)
    def rms(raw):
        samples = struct.unpack('<' + 'h' * (len(raw) // 2), raw)[320:-320]
        return math.sqrt(sum(x*x for x in samples) / len(samples))
    voice = run(24000, 960, 1000)
    rejected = run(24000, 960, 10000)
    assert 6500 < rms(voice) < 7500
    assert rms(rejected) < rms(voice) * 0.02
    assert run(24000, 960, fail=1) == b''
    assert run(1, 1, fail=3) == b''
    assert run(1, 1, fail=2) == b''
    # A truncated final sample must cancel even after prior frames reached DAC.
    run(24000, 319, fail=2)
    print('PASS: 8 lengths x 10 chunkings, split/truncated samples, tail duration, alias rejection, DAC drain and cancel')
