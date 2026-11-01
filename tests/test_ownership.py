import json
import shutil
import subprocess
import sys

import pytest

from conftest import HELLO, REPO
import validate

BOT = 'lakeshark-store[bot]'
ME = {'github_id': 100, 'login': 'alice'}
BOB = {'github_id': 200, 'login': 'bob'}


def entry(who, since='2026-11-01'):
    return {'github_id': who['github_id'], 'login': who['login'], 'since': since}


def own(changed, base, head, submitter=ME, removed=()):
    p = validate.Problems()
    validate.check_ownership(set(changed), set(removed), base, head, submitter, p)
    return [m for _, _, m in p]


# ---- owners.json rules


def test_new_app_with_own_entry():
    assert own({'dice'}, {}, {'dice': entry(ME)}) == []


def test_new_app_without_entry():
    msgs = own({'dice'}, {}, {})
    assert len(msgs) == 1 and 'add "dice": {"github_id": 100, "login": "alice"' in msgs[0]


def test_update_own_app():
    assert own({'dice'}, {'dice': entry(ME)}, {'dice': entry(ME)}) == []


def test_update_someone_elses_app():
    msgs = own({'dice'}, {'dice': entry(BOB)}, {'dice': entry(BOB)})
    assert len(msgs) == 1 and 'belongs to bob' in msgs[0]


def test_take_over_by_editing_owners():
    msgs = own({'dice'}, {'dice': entry(BOB)}, {'dice': entry(ME)})
    assert any('belongs to someone else and was changed' in m for m in msgs)
    assert any('belongs to bob' in m for m in msgs)


def test_remove_entry():
    msgs = own(set(), {'dice': entry(BOB)}, {})
    assert any('was removed' in m for m in msgs)


def test_add_entry_for_app_not_in_pr():
    msgs = own({'dice'}, {}, {'dice': entry(ME), 'snake': entry(ME)})
    assert any('"snake" was added but this PR does not add apps/snake/' in m for m in msgs)


def test_new_entry_with_other_id():
    msgs = own({'dice'}, {}, {'dice': entry(BOB)})
    assert any('must have your github_id (100)' in m for m in msgs)


def test_new_entry_with_other_login():
    msgs = own({'dice'}, {}, {'dice': {'github_id': 100, 'login': 'mallory', 'since': '2026-11-01'}})
    assert any('must have your login (alice)' in m for m in msgs)


def test_own_entry_login_rename_ok():
    base = {'dice': entry(ME)}
    head = {'dice': {'github_id': 100, 'login': 'alice2', 'since': '2026-11-01'}}
    assert own({'dice'}, base, head, {'github_id': 100, 'login': 'alice2'}) == []


def test_bad_entry_shape():
    msgs = own({'dice'}, {}, {'dice': {'github_id': '100', 'login': 'alice', 'since': '2026-11-01'}})
    assert any('numeric "github_id"' in m for m in msgs)


def test_bad_since():
    msgs = own({'dice'}, {}, {'dice': {'github_id': 100, 'login': 'alice', 'since': 'today'}})
    assert any('YYYY-MM-DD' in m for m in msgs)


def test_removed_app_by_other():
    msgs = own(set(), {'dice': entry(BOB)}, {'dice': entry(BOB)}, removed={'dice'})
    assert any('belongs to bob' in m for m in msgs)


def test_other_entries_untouched_ok():
    base = {'snake': entry(BOB)}
    head = {'snake': entry(BOB), 'dice': entry(ME)}
    assert own({'dice'}, base, head) == []


# ---- who is the submitter


MARK = '<!-- lakeshark-submitter github_id=100 login=alice -->'


def test_author_is_submitter():
    s, why = validate.resolve_submitter('alice', 100, 'User', '', BOT)
    assert s == ME and why is None


def test_bot_marker_trusted_for_bot():
    s, why = validate.resolve_submitter(BOT, 9999, 'Bot', 'Table...\n' + MARK + '\n', BOT)
    assert s == ME and why is None


def test_marker_ignored_for_users():
    body = '<!-- lakeshark-submitter github_id=200 login=bob -->'
    s, _ = validate.resolve_submitter('alice', 100, 'User', body, BOT)
    assert s == ME


def test_marker_ignored_for_lookalike_login():
    body = '<!-- lakeshark-submitter github_id=200 login=bob -->'
    s, _ = validate.resolve_submitter('lakeshark-store', 300, 'User', body, BOT)
    assert s == {'github_id': 300, 'login': 'lakeshark-store'}


def test_marker_ignored_when_type_is_not_bot():
    body = '<!-- lakeshark-submitter github_id=200 login=bob -->'
    s, _ = validate.resolve_submitter(BOT, 300, 'User', body, BOT)
    assert s['github_id'] == 300


def test_marker_ignored_for_other_bot():
    body = '<!-- lakeshark-submitter github_id=200 login=bob -->'
    s, _ = validate.resolve_submitter('dependabot[bot]', 49699333, 'Bot', body, BOT)
    assert s['github_id'] == 49699333


def test_bot_without_marker():
    s, why = validate.resolve_submitter(BOT, 9999, 'Bot', 'no marker here', BOT)
    assert s is None and 'exactly one submitter marker (found 0)' in why


def test_bot_with_two_markers():
    body = MARK + '\n<!-- lakeshark-submitter github_id=200 login=bob -->'
    s, why = validate.resolve_submitter(BOT, 9999, 'Bot', body, BOT)
    assert s is None and 'found 2' in why


def test_no_bot_configured():
    s, _ = validate.resolve_submitter(BOT, 9999, 'Bot', MARK, None)
    assert s['github_id'] == 9999


# ---- changed paths


def test_changes_inside_apps_ok():
    p = validate.Problems()
    ids = validate.check_changes(['apps/dice/app.json', 'apps/dice/src/x.c', 'owners.json'], p)
    assert ids == {'dice'} and not p


@pytest.mark.parametrize('path', ['tools/validate.py', '.github/workflows/check.yml', 'sdk.lock', 'README.md',
                                  'apps/loose.txt'])
def test_changes_outside_apps(path):
    p = validate.Problems()
    validate.check_changes(['apps/dice/app.json', path], p)
    assert [r for r, _, _ in p] == ['changes']


# ---- the whole PR check on a real git repo


def git(repo, *args):
    subprocess.run(['git', '-C', str(repo), *args], check=True, capture_output=True)


@pytest.fixture
def pr_repo(tmp_path):
    repo = tmp_path / 'repo'
    (repo / 'apps').mkdir(parents=True)
    git(repo, 'init', '-q', '-b', 'main')
    git(repo, 'config', 'user.email', 't@example.com')
    git(repo, 'config', 'user.name', 't')
    shutil.copytree(HELLO, repo / 'apps' / 'snake')
    c = repo / 'apps' / 'snake' / 'hello.c'
    c.write_text(c.read_text(encoding='utf-8').replace('.id = "hello"', '.id = "snake"'), encoding='utf-8')
    m = json.loads((repo / 'apps' / 'snake' / 'app.json').read_text())
    m['id'] = 'snake'
    (repo / 'apps' / 'snake' / 'app.json').write_text(json.dumps(m))
    (repo / 'owners.json').write_text(json.dumps({'snake': entry(BOB)}))
    git(repo, 'add', '-A')
    git(repo, 'commit', '-qm', 'base')
    return repo


def run_check(repo, out, author='alice', author_id=100, author_type='User', body=''):
    bodyf = out / 'body.txt'
    out.mkdir(parents=True, exist_ok=True)
    bodyf.write_text(body)
    r = subprocess.run([sys.executable, str(REPO / 'tools' / 'check_pr.py'), '--repo', str(repo),
                        '--out', str(out), '--author', author, '--author-id', str(author_id),
                        '--author-type', author_type, '--body-file', str(bodyf), '--bot-login', BOT,
                        '--no-sim'], capture_output=True, text=True)
    return r.returncode, json.loads((out / 'results.json').read_text()), (out / 'comment.md').read_text()


def test_pr_hijack_other_app(pr_repo, tmp_path):
    (pr_repo / 'apps' / 'snake' / 'README.md').write_text('mine now')
    git(pr_repo, 'add', '-A')
    git(pr_repo, 'commit', '-qm', 'pr')
    code, res, text = run_check(pr_repo, tmp_path / 'out')
    assert code == 1
    assert res['rules']['ownership'] == 'fail'
    assert res['rules']['build'].startswith('skipped')
    assert 'belongs to bob' in text and '<!-- lakeshark-check -->' in text


def test_pr_bot_marker_owner_passes_ownership(pr_repo, tmp_path):
    (pr_repo / 'apps' / 'snake' / 'README.md').write_text('update')
    (pr_repo / 'apps' / 'snake' / 'bad.exe').write_bytes(b'MZ')    # keep it from reaching the build
    git(pr_repo, 'add', '-A')
    git(pr_repo, 'commit', '-qm', 'pr')
    body = '<!-- lakeshark-submitter github_id=200 login=bob -->'
    code, res, _ = run_check(pr_repo, tmp_path / 'out', author=BOT, author_id=1, author_type='Bot', body=body)
    assert res['rules']['ownership'] == 'pass'
    assert res['rules']['bundle'] == 'fail'
    assert code == 1


def test_pr_user_marker_not_trusted(pr_repo, tmp_path):
    (pr_repo / 'apps' / 'snake' / 'README.md').write_text('update')
    git(pr_repo, 'add', '-A')
    git(pr_repo, 'commit', '-qm', 'pr')
    body = '<!-- lakeshark-submitter github_id=200 login=bob -->'
    code, res, _ = run_check(pr_repo, tmp_path / 'out', body=body)
    assert res['rules']['ownership'] == 'fail' and code == 1


def test_pr_touching_tools(pr_repo, tmp_path):
    (pr_repo / 'tools').mkdir()
    (pr_repo / 'tools' / 'validate.py').write_text('print("ok")')
    git(pr_repo, 'add', '-A')
    git(pr_repo, 'commit', '-qm', 'pr')
    code, res, text = run_check(pr_repo, tmp_path / 'out')
    assert code == 1 and res['rules']['changes'] == 'fail'
    assert 'tools/validate.py is outside apps/' in text


def test_comment_escapes_mentions_and_html():
    import check_pr
    assert check_pr.esc('@octocat <img src=x> a|b') == '&#64;octocat &lt;img src=x&gt; a\\|b'
