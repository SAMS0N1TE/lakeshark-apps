import pytest

from conftest import GPL3_TEXT, LGPL3_TEXT
from test_validate import one


def tp(name, lic, use='code'):
    return {'name': name, 'license': lic, 'url': '', 'use': use}


def test_license_not_allowed(app):
    app.set(license='WTFPL')
    one(app, 'license', '"license" must be one of')


@pytest.mark.parametrize('lic', ['proprietary', 'Proprietary', ''])
def test_proprietary(app, lic):
    app.set(license=lic)
    one(app, 'license', 'is not accepted')


def test_cc_license_for_app(app):
    app.set(license='CC-BY-4.0')
    one(app, 'license', 'pictures and data only')


def test_license_text_mismatch(app):
    app.set(license='BSD-3-Clause')
    one(app, 'license', 'does not look like the BSD-3-Clause text')


def test_gpl_app_with_gpl_text(app):
    app.set(license='GPL-3.0-or-later')
    (app.dir / 'LICENSE').write_text(GPL3_TEXT)
    assert not app.check()


def test_lgpl_text_is_not_gpl(app):
    app.set(license='GPL-3.0-only')
    (app.dir / 'LICENSE').write_text(LGPL3_TEXT)
    one(app, 'license', 'does not look like')


def test_third_party_unknown_license(app):
    app.set(third_party=[tp('libfoo', 'SSPL-1.0')])
    one(app, 'license', 'not on the allowed list')


def test_third_party_proprietary(app):
    app.set(third_party=[tp('vendor blob', 'proprietary', 'data')])
    one(app, 'license', 'not on the allowed list')


def test_cc_for_code(app):
    app.set(third_party=[tp('snippet', 'CC-BY-SA-4.0')])
    one(app, 'license', 'not code')


def test_cc_for_asset_ok(app):
    app.set(third_party=[tp('font', 'CC-BY-4.0', 'asset'), tp('tables', 'CC-BY-SA-4.0', 'data')])
    assert not app.check()


def test_reference_any_license_ok(app):
    app.set(third_party=[tp('ETSI spec', 'proprietary', 'reference')])
    assert not app.check()


def test_permissive_code_into_mit_ok(app):
    app.set(third_party=[tp('a', 'Apache-2.0'), tp('b', 'BSD-3-Clause'), tp('c', 'Zlib'), tp('d', 'MPL-2.0')])
    assert not app.check()


def test_gpl_code_into_mit_fails(app):
    app.set(third_party=[tp('gplthing', 'GPL-3.0-or-later')])
    one(app, 'license', 'the app must be licensed GPL-3.0-only or GPL-3.0-or-later')


def test_gpl_data_into_mit_ok(app):
    app.set(third_party=[tp('table', 'GPL-3.0-or-later', 'data')])
    assert not app.check()


@pytest.mark.parametrize('code,app_lic,ok', [
    ('GPL-2.0-or-later', 'GPL-2.0-or-later', True),
    ('GPL-2.0-or-later', 'GPL-3.0-only', True),
    ('GPL-2.0-or-later', 'GPL-3.0-or-later', True),
    ('GPL-2.0-or-later', 'LGPL-2.1-or-later', False),
    ('GPL-3.0-or-later', 'GPL-2.0-or-later', False),
    ('GPL-3.0-or-later', 'GPL-3.0-only', True),
    ('GPL-3.0-only', 'GPL-3.0-or-later', False),
    ('GPL-3.0-only', 'GPL-3.0-only', True),
    ('LGPL-2.1-or-later', 'MIT', False),
    ('LGPL-2.1-or-later', 'LGPL-2.1-or-later', True),
    ('LGPL-2.1-or-later', 'LGPL-3.0-or-later', True),
    ('LGPL-2.1-or-later', 'GPL-2.0-or-later', True),
    ('LGPL-3.0-or-later', 'LGPL-2.1-or-later', False),
    ('LGPL-3.0-or-later', 'GPL-3.0-only', True),
    ('MPL-2.0', 'MIT', True),
    ('Apache-2.0', 'GPL-3.0-only', True),
])
def test_compatibility(app, code, app_lic, ok):
    app.set(license=app_lic, third_party=[tp('dep', code)])
    text = {'MIT': (app.dir / 'LICENSE').read_text()}.get(app_lic)
    if text is None:
        family = 'LESSER ' if app_lic.startswith('LGPL') else ''
        version = 'Version 2.1' if app_lic.startswith('LGPL-2.1') else \
            'Version 2' if app_lic.startswith('GPL-2') else 'Version 3'
        text = 'GNU %sGENERAL PUBLIC LICENSE\n%s\n' % (family, version)
    (app.dir / 'LICENSE').write_text(text)
    if ok:
        assert not app.check(), app.check().lines()
    else:
        one(app, 'license', 'so the app must be licensed')
