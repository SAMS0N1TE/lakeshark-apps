#!/usr/bin/env python3
"""Best-effort smoke run in lssim, plus a thumbnail when the app has none.

    python tools/sim_check.py [--sdk PATH] [--out out/sim] apps/<id> [...]

Needs the full LakeShark checkout around the SDK (bench/), CMake, Ninja and a
host C compiler. When any of that is missing it prints SKIP and exits 0. A
failed run prints FAIL but still exits 0 unless --strict is given: the
simulator is advice, not a gate.
"""
import argparse
import os
import shutil
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import sdk_dir  # noqa: E402

FRAMES = 60


def unavailable(sdk):
    """Why the simulator cannot run here, or None."""
    if not (sdk / 'lsdk.py').is_file():
        return 'the SDK has no lsdk.py'
    if not (sdk.parent / 'bench' / 'CMakeLists.txt').is_file():
        return 'no LakeShark bench/ next to the SDK (CI checks out app-sdk only)'
    sys.path.insert(0, str(sdk))
    try:
        import lsdk  # noqa: F401
    except Exception as e:  # an older or partial SDK
        return 'cannot load lsdk.py: %s' % e
    env = lsdk.host_env()
    for tool in ('cmake', 'ninja', 'gcc'):
        if not shutil.which(tool, path=env.get('PATH')):
            return '%s not found' % tool
    return None


def run_one(lsdk, app_dir, out_dir):
    """Returns (status, message). status is PASS, FAIL."""
    app_dir = Path(app_dir).resolve()
    with tempfile.TemporaryDirectory(prefix='lapsim-') as tmp:
        work = Path(tmp) / app_dir.name            # lssim names the app after its folder
        work.mkdir()
        for sub in (app_dir, app_dir / 'src'):
            if sub.is_dir():
                for f in sub.iterdir():
                    if f.is_file() and not f.is_symlink() and f.suffix.lower() in ('.c', '.h'):
                        shutil.copyfile(f, work / f.name)
        try:
            r = lsdk.sim_build(work, say=lambda *_: None)
            if not r.ok:
                first = r.problems[0]['message'] if r.problems else 'simulator build failed'
                return 'FAIL', first
            frames = lsdk.sim_frames(work, frames=FRAMES)
        except Exception as e:
            return 'FAIL', str(e).strip().splitlines()[0] if str(e).strip() else type(e).__name__
        dest = Path(out_dir) / app_dir.name
        dest.mkdir(parents=True, exist_ok=True)
        last = frames / 'last.png'
        if last.is_file() and not (app_dir / 'media' / 'thumb.png').is_file():
            shutil.copyfile(last, dest / 'thumb.png')
            return 'PASS', 'ran %d frames; made a thumbnail' % FRAMES
        return 'PASS', 'ran %d frames' % FRAMES


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('apps', nargs='+')
    ap.add_argument('--sdk')
    ap.add_argument('--out', default='out/sim')
    ap.add_argument('--strict', action='store_true', help='exit 1 when a run fails')
    a = ap.parse_args(argv)
    sdk = sdk_dir(a.sdk)
    out = Path(a.out).resolve()
    # Keep the simulator build out of the SDK's own bench/build-lsdk.
    os.environ.setdefault('LSDK_SIM_BUILD', str(out / 'build'))
    why = unavailable(sdk)
    if why:
        for d in a.apps:
            print('SKIP apps/%s: %s' % (Path(d).name, why))
        return 0
    import lsdk
    failed = 0
    for d in a.apps:
        status, msg = run_one(lsdk, d, out)
        print('%s apps/%s: %s' % (status, Path(d).name, msg))
        failed += status == 'FAIL'
    return 1 if failed and a.strict else 0


if __name__ == '__main__':
    sys.exit(main())
