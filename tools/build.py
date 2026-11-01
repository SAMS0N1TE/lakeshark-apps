#!/usr/bin/env python3
"""Build app folders into .lap files with lap.py from the pinned SDK.

    python tools/build.py [--sdk PATH] [--out out/build] apps/<id> [...]

The SDK is found at --sdk, LAKESHARK_SDK, or .sdk/app-sdk (checked out at sdk.lock).
LAP_CC overrides the compiler lap.py would pick. Files in src/ are built as if
they sat next to app.json. lap.py checks every import against exports.txt.
"""
import argparse
import re
import shutil
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import import_lap, sdk_dir  # noqa: E402

MAX_LOG = 1500


def friendly(lap, error):
    """One line a beginner can act on, from what lap.py raised."""
    msg = str(error)
    if isinstance(error, getattr(lap, 'CompileError', ())):
        out = error.output or ''
        lines = [ln for ln in out.splitlines() if 'error' in ln.lower()]
        first = lines[0] if lines else (out.strip().splitlines() or ['compile failed'])[-1]
        m = re.search(r"undefined reference to [`']([A-Za-z_]\w*)'", out)
        if m:
            return '%s is not part of the app API (see exports.txt in the SDK)' % m.group(1)
        return 'the compiler stopped: ' + re.sub(r'^.*?[\\/](?=[^\\/]+:\d+)', '', first.strip())
    m = re.match(r'needs (\S+)', msg)
    if m:
        return '%s is not part of the app API (see exports.txt in the SDK)' % m.group(1)
    return msg


def build_one(lap, app_dir, out_dir):
    """Build one app. Returns (ok, .lap path or None, message, compiler output)."""
    app_dir = Path(app_dir).resolve()
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='lapbuild-') as tmp:
        flat = Path(tmp) / app_dir.name
        flat.mkdir()
        for sub in (app_dir, app_dir / 'src'):
            if sub.is_dir():
                for f in sub.iterdir():
                    if f.is_file() and not f.is_symlink() and f.suffix.lower() in ('.c', '.h'):
                        shutil.copyfile(f, flat / f.name)
        target = out_dir / (app_dir.name + '.lap')
        log = []
        try:
            lap.build(flat, target, log=log)
        except Exception as e:  # lap raises ValueError/CompileError/OSError/CalledProcessError
            text = ''.join(log)
            return False, None, friendly(lap, e), text[-MAX_LOG:]
        m = lap.Elf(target.read_bytes()).manifest
        return True, target, '%s %s, %d bytes' % (m['id'], m['version'], m['size']), ''.join(log)[-MAX_LOG:]


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('apps', nargs='+')
    ap.add_argument('--sdk')
    ap.add_argument('--out', default='out/build')
    a = ap.parse_args(argv)
    lap = import_lap(sdk_dir(a.sdk))
    bad = 0
    for d in a.apps:
        ok, _, msg, log = build_one(lap, d, a.out)
        print('%s apps/%s: %s' % ('PASS' if ok else 'FAIL', Path(d).name, msg))
        if not ok:
            bad += 1
            if log.strip():
                sys.stderr.write(log if log.endswith('\n') else log + '\n')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
