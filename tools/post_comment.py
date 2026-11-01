#!/usr/bin/env python3
"""Post or update the one check comment on a PR. Runs in the comment workflow.

Environment:
    GH_TOKEN      token with pull-requests: write
    REPO          owner/name
    HEAD_SHA      head commit of the PR the check ran on
    CONCLUSION    the check run's conclusion
    RESULTS       folder holding comment.md from the check run (may be missing)

comment.md comes from a run that compiled PR code, so it is treated as text
from a stranger: length capped, mentions defused, and the PR is found by its
head commit through the API, never from the artifact.
"""
import json
import os
import sys
import urllib.error
import urllib.request
from pathlib import Path

MARK = '<!-- lakeshark-check -->'
API = 'https://api.github.com'
MAX = 60000
ALLOWED_TOP = ('apps/',)
ALLOWED_FILES = ('owners.json',)


def call(method, path, body=None):
    req = urllib.request.Request(API + path, method=method,
                                 data=json.dumps(body).encode() if body is not None else None)
    req.add_header('Authorization', 'Bearer ' + os.environ['GH_TOKEN'])
    req.add_header('Accept', 'application/vnd.github+json')
    req.add_header('X-GitHub-Api-Version', '2022-11-28')
    if body is not None:
        req.add_header('Content-Type', 'application/json')
    with urllib.request.urlopen(req, timeout=30) as r:
        return json.loads(r.read() or b'null')


def find_pr(repo, sha):
    try:
        for pr in call('GET', '/repos/%s/commits/%s/pulls' % (repo, sha)) or []:
            if pr.get('state') == 'open' and pr['head']['sha'] == sha:
                return pr['number']
    except urllib.error.HTTPError:
        pass
    for page in range(1, 6):
        prs = call('GET', '/repos/%s/pulls?state=open&per_page=100&page=%d' % (repo, page))
        for pr in prs:
            if pr['head']['sha'] == sha:
                return pr['number']
        if len(prs) < 100:
            break
    return None


def outside_files(repo, number):
    """Paths the PR changes outside apps/ and owners.json, from the API (trusted)."""
    bad = []
    for page in range(1, 31):
        files = call('GET', '/repos/%s/pulls/%d/files?per_page=100&page=%d' % (repo, number, page))
        bad += [f['filename'] for f in files
                if not f['filename'].startswith(ALLOWED_TOP) and f['filename'] not in ALLOWED_FILES]
        if len(files) < 100:
            break
    return bad


def body_text(results, conclusion):
    path = Path(results) / 'comment.md'
    if path.is_file():
        text = path.read_text(encoding='utf-8', errors='replace')[:MAX]
    else:
        text = '### LakeShark app checks: could not run\n\nThe check run ended with "%s" before it wrote ' \
               'results. A maintainer will look at the Actions log.\n' % conclusion
    text = text.replace(MARK, '').replace('@', '&#64;')
    return text


def main():
    repo, sha = os.environ['REPO'], os.environ['HEAD_SHA']
    number = find_pr(repo, sha)
    if number is None:
        print('no open PR has head %s; nothing to comment on' % sha)
        return 0
    text = body_text(os.environ.get('RESULTS', ''), os.environ.get('CONCLUSION', 'unknown'))
    bad = outside_files(repo, number)
    if bad:
        text = ('> **Not a plain app submission.** This PR changes %s. A PR can change how its '
                'own checks run, so the results below are not proof. A maintainer reviews it by hand.\n\n'
                % ', '.join('`%s`' % b.replace('`', "'") for b in bad[:10])) + text
    text = MARK + '\n' + text
    mine = None
    for page in range(1, 11):
        comments = call('GET', '/repos/%s/issues/%d/comments?per_page=100&page=%d' % (repo, number, page))
        for c in comments:
            if c['user']['login'] == 'github-actions[bot]' and c['body'].startswith(MARK):
                mine = c['id']
        if len(comments) < 100:
            break
    if mine:
        call('PATCH', '/repos/%s/issues/comments/%d' % (repo, mine), {'body': text})
        print('updated the check comment on PR #%d' % number)
    else:
        call('POST', '/repos/%s/issues/%d/comments' % (repo, number), {'body': text})
        print('posted the check comment on PR #%d' % number)
    return 0


if __name__ == '__main__':
    sys.exit(main())
