import os

import pytest

from conftest import HELLO
import validate

PNG = b'\x89PNG\r\n\x1a\n' + b'\0' * 32
GIF = b'GIF89a' + b'\0' * 32


def one(app, rule, fragment):
    """The app fails exactly under `rule`, with a message containing `fragment`."""
    probs = list(app.check())
    assert probs, 'expected a failure'
    assert {r for r, _, _ in probs} == {rule}, probs
    assert any(fragment in m for _, _, m in probs), probs
    for _, _, m in probs:
        assert '\n' not in m


def test_hello_passes():
    p, fields = validate.validate([HELLO])
    assert not p, p.lines()
    assert fields['hello']['id'] == 'hello'
    assert fields['hello']['version'] == '0.1'


def test_copy_passes(app):
    assert not app.check()


# ---- bundle


def test_missing_app_json(app):
    (app.dir / 'app.json').unlink()
    one(app, 'bundle', 'app.json is missing')


def test_missing_license(app):
    (app.dir / 'LICENSE').unlink()
    probs = app.messages()
    assert any('LICENSE is missing' in m for m in probs)


def test_no_c_file(app):
    (app.dir / 'hello.c').rename(app.dir / 'hello.txt')
    assert any('no .c source' in m for m in app.messages())


def test_binary_extension(app):
    (app.dir / 'tool.exe').write_bytes(b'MZ')
    one(app, 'bundle', 'this file type is not allowed')


def test_lap_file_rejected(app):
    (app.dir / 'hello.lap').write_bytes(b'\x7fELF')
    one(app, 'bundle', 'not allowed')


def test_hidden_file(app):
    (app.dir / '.secret').write_text('x')
    one(app, 'bundle', 'hidden file')


def test_extra_folder(app):
    (app.dir / 'build').mkdir()
    (app.dir / 'build' / 'x.c').write_text('int x;')
    one(app, 'bundle', 'build/ is not allowed')


def test_nested_folder_in_src(app):
    (app.dir / 'src' / 'deep').mkdir(parents=True)
    one(app, 'bundle', 'src/deep/ is not allowed')


def test_space_in_name(app):
    (app.dir / 'my notes.md').write_text('x')
    one(app, 'bundle', 'no spaces')


def test_symlink(app):
    try:
        os.symlink(app.dir / 'hello.c', app.dir / 'link.c')
    except (OSError, NotImplementedError):
        pytest.skip('cannot make symlinks here')
    one(app, 'bundle', 'is a link')


def test_too_many_files(app):
    for i in range(70):
        (app.dir / ('n%d.txt' % i)).write_text('x')
    one(app, 'bundle', 'too many files')


def test_too_big(app):
    for i in range(3):
        (app.dir / ('big%d.txt' % i)).write_text('x' * (800 * 1024))
    one(app, 'bundle', 'at most 2 MB')


def test_not_utf8(app):
    (app.dir / 'data.txt').write_bytes(b'\xff\xfe\0\0')
    one(app, 'bundle', 'not plain UTF-8')


def test_src_folder_ok(app):
    (app.dir / 'src').mkdir()
    (app.dir / 'src' / 'util.c').write_text('int util(void) { return 1; }\n')
    assert not app.check()


def test_src_duplicate_name(app):
    (app.dir / 'src').mkdir()
    (app.dir / 'src' / 'hello.c').write_text('int x;\n')
    assert any('same name' in m for m in app.messages())


def test_media_ok(app):
    (app.dir / 'media').mkdir()
    (app.dir / 'media' / 'thumb.png').write_bytes(PNG)
    (app.dir / 'media' / 'screen-1.png').write_bytes(PNG)
    (app.dir / 'media' / 'screen-2.gif').write_bytes(GIF)
    (app.dir / 'media' / 'clip.gif').write_bytes(GIF)
    assert not app.check()


def test_fake_png(app):
    (app.dir / 'media').mkdir()
    (app.dir / 'media' / 'thumb.png').write_bytes(b'not a picture')
    one(app, 'bundle', 'not a real PNG')


def test_screen_too_big(app):
    (app.dir / 'media').mkdir()
    (app.dir / 'media' / 'screen-1.png').write_bytes(PNG + b'\0' * (301 * 1024))
    one(app, 'bundle', 'over 300 KB')


def test_too_many_screens(app):
    (app.dir / 'media').mkdir()
    for i in range(1, 6):
        (app.dir / 'media' / ('screen-%d.png' % i)).write_bytes(PNG)
    one(app, 'bundle', 'too many screenshots')


def test_unknown_media(app):
    (app.dir / 'media').mkdir()
    (app.dir / 'media' / 'photo.jpg').write_bytes(b'\xff\xd8')
    one(app, 'bundle', 'media/ may hold only')


def test_bad_folder_name(make_app):
    a = make_app('Bad.Name')
    assert any('not a valid app id' in m for m in a.messages())


# ---- app.json


def test_invalid_json(app):
    (app.dir / 'app.json').write_text('{"schema": 2,', encoding='utf-8')
    one(app, 'app.json', 'not valid JSON')


def test_schema_1_rejected(app):
    app.write_meta({'category': 'games', 'description': 'An old style first-party app.json.'})
    one(app, 'app.json', '"schema" must be 2')


def test_unknown_field(app):
    app.set(licence='MIT')
    one(app, 'app.json', 'unknown field "licence"')


def test_bad_category(app):
    app.set(category='fun')
    one(app, 'app.json', '"category" must be one of')


def test_short_description(app):
    app.set(description='Hi.')
    one(app, 'app.json', 'too short')


def test_bad_id(app):
    app.set(id='Hello World')
    probs = app.messages()
    assert any('"id" may use only' in m for m in probs)


def test_bad_source_url(app):
    app.set(source_url='ftp://example.com/x')
    one(app, 'app.json', 'full web address')


def test_status_wip_ok(app):
    app.set(status='wip')
    assert not app.check()


def test_bad_status(app):
    app.set(status='beta')
    one(app, 'app.json', '"status" must be one of')


# ---- declarations


@pytest.mark.parametrize('key', sorted(validate.DECLARATION_TEXT))
def test_declaration_false(app, key):
    app.edit(lambda m: m['declarations'].__setitem__(key, False))
    one(app, 'declarations', '"declarations.%s" must be true' % key)


def test_declaration_missing(app):
    app.edit(lambda m: m['declarations'].pop('no_ai_cleanroom'))
    one(app, 'declarations', 'no AI was used to re-create')


def test_declaration_string_true(app):
    app.edit(lambda m: m['declarations'].__setitem__('ethical_sources', 'true'))
    one(app, 'declarations', 'must be true')


# ---- AI disclosure


def test_ai_assisted_ok(app):
    app.set(ai={'use': 'assisted', 'tools': ['Copilot'], 'extent': 'Autocomplete for the OPTIONS table.'})
    assert not app.check()


def test_ai_missing_tools(app):
    app.set(ai={'use': 'substantial', 'tools': [], 'extent': 'Wrote most of the drawing code.'})
    one(app, 'ai', 'list the AI tools you used')


def test_ai_missing_extent(app):
    app.set(ai={'use': 'generated', 'tools': ['Some model'], 'extent': ''})
    one(app, 'ai', 'say in plain words what the AI did')


def test_ai_vague_extent(app):
    app.set(ai={'use': 'assisted', 'tools': ['X'], 'extent': 'helped......'})
    one(app, 'ai', 'a few plain words')


def test_ai_none_with_tools(app):
    app.set(ai={'use': 'none', 'tools': ['X'], 'extent': ''})
    one(app, 'ai', '"ai.use" is "none" but')


def test_ai_bad_use(app):
    app.set(ai={'use': 'some', 'tools': [], 'extent': ''})
    one(app, 'ai', '"ai.use" must be one of')


def test_ai_missing(app):
    app.edit(lambda m: m.pop('ai'))
    one(app, 'ai', '"ai" is missing')


# ---- source


def test_id_mismatch(app):
    app.set(id='other')
    one(app, 'source', 'make them the same')


def test_folder_mismatch(make_app):
    a = make_app('hi')
    c = a.dir / 'hello.c'
    c.write_text(c.read_text(encoding='utf-8').replace('.id = "hi"', '.id = "hello"'), encoding='utf-8')
    a.set(id='hello')
    probs = a.messages()
    assert any('they must match' in m for m in probs)


def test_no_manifest(app):
    c = app.dir / 'hello.c'
    c.write_text(c.read_text(encoding='utf-8').replace('LAP_APP(', 'NOT_APP('), encoding='utf-8')
    one(app, 'source', 'no LAP_APP')


def test_manifest_in_comment_ignored(app):
    c = app.dir / 'hello.c'
    c.write_text('/* LAP_APP(.id = "evil") */\n' + c.read_text(encoding='utf-8'), encoding='utf-8')
    assert not app.check()


def test_two_manifests(app):
    (app.dir / 'two.c').write_text('LAP_APP(.id = "hello", .version = "1");\n')
    one(app, 'source', 'appears 2 times')


def test_include_path(app):
    (app.dir / 'x.c').write_text('#include "/etc/passwd"\nint x;\n')
    one(app, 'source', 'by name only')


def test_include_parent(app):
    (app.dir / 'x.c').write_text('#include "../secret.h"\nint x;\n')
    one(app, 'source', 'by name only')


def test_include_unknown(app):
    (app.dir / 'x.c').write_text('#include <sys/socket.h>\nint x;\n')
    one(app, 'source', 'by name only')


def test_include_not_available(app):
    (app.dir / 'x.c').write_text('#include <unistd.h>\nint x;\n')
    one(app, 'source', 'not available to apps')


def test_include_macro(app):
    (app.dir / 'x.c').write_text('#define F "a.h"\n#include F\nint x;\n')
    one(app, 'source', 'must name a file')


def test_include_local_header_ok(app):
    (app.dir / 'util.h').write_text('int util(void);\n')
    (app.dir / 'x.c').write_text('#include "util.h"\n#include <math.h>\nint util(void) { return 1; }\n')
    assert not app.check()


def test_incbin(app):
    (app.dir / 'x.c').write_text('__asm__(".incbin \\"x\\"");\n')
    one(app, 'source', '.incbin')


def test_bad_version(app):
    c = app.dir / 'hello.c'
    c.write_text(c.read_text(encoding='utf-8').replace('.version = "0.1"', '.version = "../x"'), encoding='utf-8')
    one(app, 'source', '.version')


# ---- CLI


def test_cli_pass(capsys):
    assert validate.main([str(HELLO)]) == 0
    assert 'PASS apps/hello' in capsys.readouterr().out


def test_cli_fail(app, capsys):
    app.set(category='fun')
    assert validate.main(['--repo', str(app.dir.parent.parent), str(app.dir)]) == 1
    out = capsys.readouterr().out
    assert out.startswith('FAIL hello: app.json: "category" must be one of')
