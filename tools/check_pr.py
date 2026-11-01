#!/usr/bin/env python3
"""Run every PR check and write the results for the sticky comment.

    python tools/check_pr.py --repo PR_CHECKOUT --sdk SDK --out results \
        --author LOGIN --author-id N --author-type User --body-file BODY --bot-login NAME

PR_CHECKOUT is the PR merge commit with its first parent (the base) fetched.
Writes results/results.json and results/comment.md; exits 1 if anything failed.
This script runs from the base branch, never from the PR.
"""
import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import validate  # noqa: E402
from common import changed_paths, import_lap, sdk_dir  # noqa: E402

MARK = '<!-- lakeshark-check -->'


def git_show_json(repo, rev, path):
    r = subprocess.run(['git', '-C', str(repo), 'show', '%s:%s' % (rev, path)], capture_output=True, text=True)
    if r.returncode:
        return {}
    try:
        return json.loads(r.stdout)
    except json.JSONDecodeError:
        return {}


def esc(text):
    """Plain text for a Markdown table or list: no tags, no pings, no table breaks."""
    text = re.sub(r'[\r\n]+', ' ', str(text))
    for a, b in (('&', '&amp;'), ('<', '&lt;'), ('>', '&gt;'), ('|', '\\|'), ('@', '&#64;'), ('`', "'")):
        text = text.replace(a, b)
    return text[:400]


def comment(results):
    ok = results['ok']
    lines = [MARK, '### LakeShark app checks: %s' % ('all passed' if ok else 'changes needed'), '']
    if results['apps']:
        lines += ['Apps in this PR: ' + ', '.join(esc(a) for a in results['apps']), '']
    lines += ['| Check | Result |', '|---|---|']
    for rule in validate.RULES + ['build', 'sim']:
        r = results['rules'].get(rule)
        if r is None:
            continue
        lines.append('| %s | %s |' % (validate.RULE_TITLES[rule], esc(r)))
    if results['problems']:
        lines += ['', '**What to fix**', '']
        lines += ['- %s' % esc(p) for p in results['problems'][:60]]
        if len(results['problems']) > 60:
            lines.append('- ... and %d more' % (len(results['problems']) - 60))
    if results.get('notes'):
        lines += ['', '**Notes**', ''] + ['- %s' % esc(n) for n in results['notes'][:20]]
    lines += ['', 'Rules: RULES.md. Push a new commit to run the checks again.' if not ok else
              'A maintainer reviews it next.']
    return '\n'.join(lines) + '\n'


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--repo', required=True)
    ap.add_argument('--base-rev', default='HEAD^1')
    ap.add_argument('--sdk')
    ap.add_argument('--out', default='results')
    ap.add_argument('--author', required=True)
    ap.add_argument('--author-id', type=int, required=True)
    ap.add_argument('--author-type', default='User')
    ap.add_argument('--body-file')
    ap.add_argument('--bot-login', default='')
    ap.add_argument('--no-sim', action='store_true')
    a = ap.parse_args(argv)
    repo = Path(a.repo).resolve()
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)

    changed = changed_paths(repo, a.base_rev, 'HEAD')
    ids = sorted({c.split('/')[1] for c in changed if c.startswith('apps/') and c.count('/') >= 2})
    dirs = [repo / 'apps' / i for i in ids if (repo / 'apps' / i).is_dir()]
    body = Path(a.body_file).read_text(encoding='utf-8', errors='replace') if a.body_file else ''
    submitter, why = validate.resolve_submitter(a.author, a.author_id, a.author_type, body, a.bot_login or None)
    base_owners = git_show_json(repo, a.base_rev, 'owners.json')
    head_owners = git_show_json(repo, 'HEAD', 'owners.json')
    p, _ = validate.validate(dirs, changed, base_owners, head_owners, submitter)
    if why:
        p.items.insert(0, ('ownership', '', why))
    if not changed:
        p.add('changes', '', 'this PR changes no files')

    rules = {r: 'pass' for r in validate.RULES}
    for rule, _, _ in p:
        rules[rule] = 'fail'
    problems = p.lines()
    notes = []
    if submitter:
        notes.append('Submitter: %s (id %d)' % (submitter['login'], submitter['github_id']))

    good = [d for d in dirs if not p.for_app(d.name)]
    if dirs:
        build_ok = True
        built = []
        if good:
            from build import build_one
            lap = import_lap(sdk_dir(a.sdk))
            for d in good:
                ok, _, msg, log = build_one(lap, d, out / 'build')
                if ok:
                    built.append(d)
                    notes.append('apps/%s built: %s' % (d.name, msg))
                else:
                    build_ok = False
                    problems.append('%s: build: %s' % (d.name, msg))
        skipped = len(good) < len(dirs)
        rules['build'] = ('fail' if not build_ok else
                          'skipped for apps that failed the checks above' if skipped else 'pass')
        if not a.no_sim and built:
            import sim_check
            sdk = sdk_dir(a.sdk)
            why_not = sim_check.unavailable(sdk)
            if why_not:
                rules['sim'] = 'skipped (%s)' % why_not
            else:
                import lsdk
                res = [sim_check.run_one(lsdk, d, out / 'sim') for d in built]
                bad = [m for s, m in res if s == 'FAIL']
                rules['sim'] = 'pass' if not bad else 'did not pass (not required): ' + bad[0]
        elif not a.no_sim:
            rules['sim'] = 'skipped'
    ok = all(v != 'fail' for v in rules.values())
    results = {'ok': ok, 'apps': [d.name for d in dirs] + sorted(set(ids) - {d.name for d in dirs}),
               'rules': rules, 'problems': problems, 'notes': notes, 'changed': changed}
    (out / 'results.json').write_text(json.dumps(results, indent=1) + '\n', encoding='utf-8')
    (out / 'comment.md').write_text(comment(results), encoding='utf-8')
    print(comment(results))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
