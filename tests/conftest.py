import json
import shutil
import sys
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / 'tools'))

import validate  # noqa: E402

HELLO = REPO / 'apps' / 'hello'
GPL3_TEXT = 'GNU GENERAL PUBLIC LICENSE\nVersion 3, 29 June 2007\n...'
LGPL3_TEXT = 'GNU LESSER GENERAL PUBLIC LICENSE\nVersion 3, 29 June 2007\n...'


class App:
    """A copy of apps/hello in a temp folder, easy to break one way at a time."""

    def __init__(self, root, app_id='hello'):
        self.dir = root / 'apps' / app_id
        shutil.copytree(HELLO, self.dir)
        if app_id != 'hello':
            self.set(id=app_id)
            c = self.dir / 'hello.c'
            c.write_text(c.read_text(encoding='utf-8').replace('.id = "hello"', '.id = "%s"' % app_id),
                         encoding='utf-8')

    @property
    def meta(self):
        return json.loads((self.dir / 'app.json').read_text(encoding='utf-8'))

    def write_meta(self, meta):
        (self.dir / 'app.json').write_text(json.dumps(meta, indent=2), encoding='utf-8')

    def set(self, **fields):
        m = self.meta
        m.update(fields)
        self.write_meta(m)

    def edit(self, fn):
        m = self.meta
        fn(m)
        self.write_meta(m)

    def check(self):
        p, _ = validate.validate([self.dir])
        return p

    def messages(self):
        return [msg for _, _, msg in self.check()]

    def rules(self):
        return {rule for rule, _, _ in self.check()}


@pytest.fixture
def app(tmp_path):
    return App(tmp_path)


@pytest.fixture
def make_app(tmp_path):
    return lambda app_id: App(tmp_path, app_id)
