"""Shared helpers for the catalog tools. Standard library only."""
import json
import os
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent


def load_json(path):
    with open(path, encoding='utf-8') as f:
        return json.load(f)


def sdk_dir(arg=None, repo=REPO):
    """Where app-sdk/ is: --sdk, then LAKESHARK_SDK, then <repo>/.sdk/<path from sdk.lock>."""
    if arg:
        return Path(arg).resolve()
    if os.environ.get('LAKESHARK_SDK'):
        return Path(os.environ['LAKESHARK_SDK']).resolve()
    lock = load_json(Path(repo) / 'sdk.lock')
    return (Path(repo) / '.sdk' / lock.get('path', 'app-sdk')).resolve()


def import_lap(sdk):
    """Import lap.py from the SDK. LAP_CC, when set, names the compiler to use."""
    sdk = Path(sdk)
    if not (sdk / 'lap.py').is_file():
        raise SystemExit('SDK not found at %s (set LAKESHARK_SDK or pass --sdk)' % sdk)
    if str(sdk) not in sys.path:
        sys.path.insert(0, str(sdk))
    import lap  # noqa: E402
    cc = os.environ.get('LAP_CC')
    if cc:
        lap.compiler = lambda: Path(cc)
    return lap


def app_dirs(repo=REPO):
    root = Path(repo) / 'apps'
    if not root.is_dir():
        return []
    return sorted(p for p in root.iterdir() if p.is_dir() and not p.name.startswith('.'))


def changed_paths(repo, base, head='HEAD'):
    """Paths changed between two commits, repo-relative, forward slashes."""
    r = subprocess.run(['git', '-C', str(repo), 'diff', '--name-only', '--no-renames', base, head],
                       capture_output=True, text=True, check=True)
    return [line.strip() for line in r.stdout.splitlines() if line.strip()]
