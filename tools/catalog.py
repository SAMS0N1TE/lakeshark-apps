#!/usr/bin/env python3
"""Build, sign and package every app for the store.

    LAKESHARK_COMMUNITY_KEY_FILE=/path/key.pem python tools/catalog.py [--sdk PATH] [--out out] [apps/<id> ...]

Writes, for apps.terminalbay.com:
    out/catalog.json               the community catalog
    out/files/<id>-<version>.lap   signed apps
    out/media/<id>/...             thumb.png, screen-N.png|gif, clip.gif

Entries have the first-party catalog shape (from what each signed file says)
plus channel, publisher, license, ai and source. catalog.json also lists every
file under out/ with its sha256 in "files", so the ingest side can refuse
anything else. Apps with no owners.json entry are skipped with a note.
"""
import argparse
import hashlib
import json
import os
import shutil
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import validate  # noqa: E402
from build import build_one  # noqa: E402
from common import REPO, app_dirs, import_lap, load_json, sdk_dir  # noqa: E402

KEY_ENV = 'LAKESHARK_COMMUNITY_KEY_FILE'
SOURCE = 'https://github.com/SAMS0N1TE/lakeshark-apps/tree/main/apps/'


def screen_order(name):
    return int(name.split('-')[1].split('.')[0])


def media_for(app_dir, app_id, out, sim_dir):
    """Copy the app's pictures to out/media/<id>/. Returns entry fields."""
    src = Path(app_dir) / 'media'
    dest = Path(out) / 'media' / app_id
    fields = {}
    thumb = src / 'thumb.png'
    if not thumb.is_file() and sim_dir and (Path(sim_dir) / app_id / 'thumb.png').is_file():
        thumb = Path(sim_dir) / app_id / 'thumb.png'
    if thumb.is_file():
        dest.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(thumb, dest / 'thumb.png')
        fields['thumb'] = 'media/%s/thumb.png' % app_id
    if src.is_dir():
        screens = sorted((f.name for f in src.iterdir() if validate.SCREEN_RE.match(f.name)), key=screen_order)
        if screens:
            dest.mkdir(parents=True, exist_ok=True)
            for s in screens:
                shutil.copyfile(src / s, dest / s)
            fields['screens'] = ['media/%s/%s' % (app_id, s) for s in screens]
        if (src / 'clip.gif').is_file():
            dest.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(src / 'clip.gif', dest / 'clip.gif')
            fields['clip'] = 'media/%s/clip.gif' % app_id
    return fields


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('apps', nargs='*', help='app folders (default: every folder in apps/)')
    ap.add_argument('--sdk')
    ap.add_argument('--out', default='out')
    ap.add_argument('--min-firmware', default='2.20.0', help='first release carrying the community key')
    ap.add_argument('--sim-thumbs', help='folder from sim_check.py with <id>/thumb.png for apps without one')
    a = ap.parse_args(argv)

    key = os.environ.get(KEY_ENV)
    if not key or not Path(key).is_file():
        sys.exit('catalog: set %s to the community signing key file' % KEY_ENV)
    if Path(key).resolve().is_relative_to(REPO):
        sys.exit('catalog: the signing key must not live inside the repository')

    dirs = [Path(d).resolve() for d in a.apps] or app_dirs()
    owners = load_json(REPO / 'owners.json')
    p, _ = validate.validate(dirs)
    if p:
        for line in p.lines():
            print('FAIL ' + line)
        sys.exit('catalog: fix the problems above first')

    lap = import_lap(sdk_dir(a.sdk))
    out = Path(a.out).resolve()
    for sub in ('files', 'media'):
        shutil.rmtree(out / sub, ignore_errors=True)
    (out / 'catalog.json').unlink(missing_ok=True)
    (out / 'files').mkdir(parents=True)

    entries = []
    with tempfile.TemporaryDirectory(prefix='lapcat-') as tmp:
        for d in dirs:
            app_id = d.name
            owner = owners.get(app_id)
            if not owner:
                print('skip %-12s no owners.json entry' % app_id)
                continue
            meta = load_json(d / 'app.json')
            ok, built, msg, log = build_one(lap, d, tmp)
            if not ok:
                sys.stderr.write(log)
                sys.exit('catalog: apps/%s does not build: %s' % (app_id, msg))
            lap.sign(built, key)
            data = built.read_bytes()
            m = lap.Elf(data).manifest
            if not m.get('signature_present'):
                sys.exit('catalog: apps/%s: signing did not take' % app_id)
            if m['id'] != app_id or not validate.VERSION_RE.match(m['version']) or '..' in m['version']:
                sys.exit('catalog: apps/%s: the built manifest says id %r version %r' % (app_id, m['id'],
                                                                                         m['version']))
            name = '%s-%s.lap' % (m['id'], m['version'])
            (out / 'files' / name).write_bytes(data)
            entry = {
                'id': m['id'], 'name': m['name'], 'sub': m['sub'], 'version': m['version'],
                'author': m['author'], 'category': meta['category'], 'color': m['color'],
                'icon_art': m['icon_art'], 'description': meta['description'],
                'size': len(data), 'sha256': hashlib.sha256(data).hexdigest(), 'signed': True,
                'file': 'files/' + name, 'min_firmware': a.min_firmware,
            }
            if meta.get('status') == 'wip':
                entry['status'] = 'wip'
            entry.update(media_for(d, app_id, out, a.sim_thumbs))
            entry.update({
                'channel': 'community',
                'publisher': {'login': owner['login'], 'github_id': owner['github_id']},
                'license': meta['license'],
                'ai': meta['ai'],
                'source': SOURCE + app_id,
            })
            entries.append(entry)
            print('%-12s %-8s %6d B  %s' % (m['id'], m['version'], len(data), name))

    files = []
    for f in sorted(out.rglob('*')):
        rel = f.relative_to(out).as_posix()
        if f.is_file() and rel.split('/')[0] in ('files', 'media'):
            data = f.read_bytes()
            files.append({'path': rel, 'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
    cat = {'schema': 1, 'channel': 'community',
           'generated': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
           'apps': entries, 'files': files}
    (out / 'catalog.json').write_text(json.dumps(cat, indent=1) + '\n', encoding='utf-8', newline='\n')
    print('catalog: %d apps, %d files' % (len(entries), len(files)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
