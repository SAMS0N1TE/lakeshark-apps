"""Build and catalog tests. They need the SDK (LAKESHARK_SDK) and the RISC-V compiler;
without them they are skipped."""
import json
import os
import subprocess
import sys
from pathlib import Path

import pytest

from conftest import HELLO, REPO

SDK = os.environ.get('LAKESHARK_SDK')


def lap_or_skip():
    if not SDK or not (Path(SDK) / 'lap.py').is_file():
        pytest.skip('set LAKESHARK_SDK to app-sdk/ to run build tests')
    from common import import_lap
    lap = import_lap(SDK)
    try:
        lap.compiler()
    except ValueError:
        pytest.skip('riscv32-esp-elf-gcc not found')
    return lap


def test_build_hello(tmp_path):
    lap = lap_or_skip()
    from build import build_one
    ok, path, msg, _ = build_one(lap, HELLO, tmp_path)
    assert ok, msg
    assert lap.Elf(path.read_bytes()).manifest['id'] == 'hello'


def test_build_rejects_unknown_import(app, tmp_path):
    lap = lap_or_skip()
    from build import build_one
    c = app.dir / 'hello.c'
    c.write_text(c.read_text(encoding='utf-8').replace('memset(&s, 0, sizeof(s));',
                                                      'memset(&s, 0, sizeof(s)); extern int lap_secret(void); lap_secret();'),
                 encoding='utf-8')
    ok, _, msg, _ = build_one(lap, app.dir, tmp_path / 'b')
    assert not ok and 'lap_secret is not part of the app API' in msg


def test_build_compile_error(app, tmp_path):
    lap = lap_or_skip()
    from build import build_one
    (app.dir / 'broken.c').write_text('int broken( { return 0; }\n')
    ok, _, msg, _ = build_one(lap, app.dir, tmp_path / 'b')
    assert not ok and msg.startswith('the compiler stopped')


def test_catalog_signs_and_lists(tmp_path):
    lap_or_skip()
    pytest.importorskip('cryptography')
    key = tmp_path / 'key.pem'
    subprocess.run([sys.executable, str(Path(SDK) / 'lap.py'), 'keygen', str(key)], check=True,
                   capture_output=True)
    owners = tmp_path / 'owners.json'
    owners.write_text(json.dumps({'hello': {'github_id': 1, 'login': 'someone', 'since': '2026-11-01'}}))
    out = tmp_path / 'out'
    env = dict(os.environ, LAKESHARK_COMMUNITY_KEY_FILE=str(key))
    r = subprocess.run([sys.executable, str(REPO / 'tools' / 'catalog.py'), '--sdk', SDK, '--out', str(out),
                        '--owners', str(owners), str(HELLO)], env=env, capture_output=True, text=True)
    assert r.returncode == 0, r.stdout + r.stderr
    cat = json.loads((out / 'catalog.json').read_text())
    (e,) = cat['apps']
    assert e['file'] == 'files/hello-0.1.lap' and e['signed'] and e['channel'] == 'community'
    assert e['publisher'] == {'login': 'someone', 'github_id': 1}
    assert e['min_firmware'] == '2.20.0' and e['license'] == 'MIT'
    assert e['source'].endswith('/apps/hello')
    assert [f['path'] for f in cat['files']] == ['files/hello-0.1.lap']
    data = (out / 'files' / 'hello-0.1.lap').read_bytes()
    assert data.endswith(b'LSG1') and len(data) == e['size']


def test_catalog_needs_key(tmp_path):
    env = {k: v for k, v in os.environ.items() if k != 'LAKESHARK_COMMUNITY_KEY_FILE'}
    r = subprocess.run([sys.executable, str(REPO / 'tools' / 'catalog.py'), '--out', str(tmp_path)],
                       env=env, capture_output=True, text=True)
    assert r.returncode != 0 and 'LAKESHARK_COMMUNITY_KEY_FILE' in r.stderr
