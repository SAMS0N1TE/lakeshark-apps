import subprocess
import sys

from conftest import REPO
import post_comment


def test_comment_body_defuses_mentions_and_marker(tmp_path):
    (tmp_path / 'comment.md').write_text(post_comment.MARK + '\nhi @everyone ' + 'x' * 70000)
    text = post_comment.body_text(tmp_path, 'failure')
    assert '@' not in text and post_comment.MARK not in text
    assert len(text) <= post_comment.MAX + 200


def test_comment_body_without_results(tmp_path):
    assert 'could not run' in post_comment.body_text(tmp_path / 'missing', 'cancelled')


def run_lock(tmp_path, text):
    f = tmp_path / 'sdk.lock'
    f.write_text(text)
    return subprocess.run([sys.executable, str(REPO / 'tools' / 'sdk_lock.py'), str(f)],
                          capture_output=True, text=True)


def test_sdk_lock_ok():
    r = subprocess.run([sys.executable, str(REPO / 'tools' / 'sdk_lock.py'), str(REPO / 'sdk.lock')],
                       capture_output=True, text=True)
    assert r.returncode == 0 and 'repo=SAMS0N1TE/LakeShark' in r.stdout and 'path=app-sdk' in r.stdout


def test_sdk_lock_rejects_branch_names(tmp_path):
    r = run_lock(tmp_path, '{"repo": "SAMS0N1TE/LakeShark", "ref": "main", "path": "app-sdk"}')
    assert r.returncode != 0 and '"ref"' in r.stderr


def test_sdk_lock_rejects_injection(tmp_path):
    r = run_lock(tmp_path, '{"repo": "a/b\nx=y", "ref": "%s", "path": "app-sdk"}' % ('0' * 40))
    assert r.returncode != 0
