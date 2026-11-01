#!/usr/bin/env python3
"""Print sdk.lock as checked key=value lines (for $GITHUB_OUTPUT).

    python tools/sdk_lock.py [path/to/sdk.lock] >> "$GITHUB_OUTPUT"
"""
import json
import re
import sys

path = sys.argv[1] if len(sys.argv) > 1 else 'sdk.lock'
with open(path, encoding='utf-8') as f:
    lock = json.load(f)
checks = {
    'repo': r'[A-Za-z0-9-]+/[A-Za-z0-9._-]+',
    'ref': r'[0-9a-f]{40}',
    'path': r'[A-Za-z0-9._-]+(/[A-Za-z0-9._-]+)*',
}
for key, pattern in checks.items():
    value = lock.get(key)
    if not isinstance(value, str) or not re.fullmatch(pattern, value) or '..' in value:
        sys.exit('sdk.lock: "%s" is missing or malformed' % key)
    print('%s=%s' % (key, value))
