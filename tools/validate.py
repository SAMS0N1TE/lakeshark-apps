#!/usr/bin/env python3
"""Check community app folders against the catalog rules. No network.

    python tools/validate.py apps/<id> [apps/<id> ...]
    python tools/validate.py --changed-from origin/main

PR checks also pass who opened the PR, so ownership can be checked:
    --author LOGIN --author-id N [--author-type Bot --body-file BODY --bot-login NAME]
    --base-owners FILE   owners.json as it is on main

Prints one line per problem and exits 1 if there is any.
"""
import argparse
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import REPO, changed_paths, load_json  # noqa: E402

SCHEMA = REPO / 'schema' / 'app.schema.json'

MAX_FILES = 64
MAX_TOTAL = 2 * 1024 * 1024
MAX_SCREEN = 300 * 1024
MAX_THUMB = 300 * 1024
MAX_CLIP = 1536 * 1024
MAX_SCREENS = 4
TOP_EXT = {'.c', '.h', '.json', '.md', '.txt'}
SRC_EXT = {'.c', '.h'}
NAME_RE = re.compile(r'^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$')
SCREEN_RE = re.compile(r'^screen-[0-9]{1,2}\.(png|gif)$')
ID_RE = re.compile(r'^[a-z0-9][a-z0-9_-]{0,30}$')
VERSION_RE = re.compile(r'^[0-9A-Za-z][0-9A-Za-z.+_-]{0,14}$')
LOGIN_RE = r'[A-Za-z0-9](?:[A-Za-z0-9-]{0,38})(?:\[bot\])?'
MARKER_RE = re.compile(r'<!--\s*lakeshark-submitter\s+github_id=(\d+)\s+login=(' + LOGIN_RE + r')\s*-->')
DATE_RE = re.compile(r'^\d{4}-\d{2}-\d{2}$')

CODE_LICENSES = ['MIT', 'BSD-2-Clause', 'BSD-3-Clause', 'ISC', 'Apache-2.0', 'Zlib', 'Unlicense', 'CC0-1.0',
                 '0BSD', 'MPL-2.0', 'GPL-2.0-or-later', 'GPL-3.0-only', 'GPL-3.0-or-later',
                 'LGPL-2.1-or-later', 'LGPL-3.0-or-later']
ASSET_ONLY_LICENSES = ['CC-BY-4.0', 'CC-BY-SA-4.0']
ALL_LICENSES = CODE_LICENSES + ASSET_ONLY_LICENSES

# A third-party "code" item under one of these licenses limits what the app itself may be licensed as.
COPYLEFT_NEEDS = {
    'GPL-2.0-or-later': {'GPL-2.0-or-later', 'GPL-3.0-only', 'GPL-3.0-or-later'},
    'GPL-3.0-or-later': {'GPL-3.0-only', 'GPL-3.0-or-later'},
    'GPL-3.0-only': {'GPL-3.0-only'},
    'LGPL-2.1-or-later': {'LGPL-2.1-or-later', 'LGPL-3.0-or-later', 'GPL-2.0-or-later', 'GPL-3.0-only',
                          'GPL-3.0-or-later'},
    'LGPL-3.0-or-later': {'LGPL-3.0-or-later', 'GPL-3.0-only', 'GPL-3.0-or-later'},
}

# Phrases the LICENSE file must contain for each app license (compared lowercase, spaces squeezed).
LICENSE_TEXT = {
    'MIT': (['permission is hereby granted, free of charge'], []),
    'BSD-2-Clause': (['redistribution and use in source and binary forms'], ['neither the name']),
    'BSD-3-Clause': (['redistribution and use in source and binary forms', 'neither the name'], []),
    'ISC': (['permission to use, copy, modify, and/or distribute this software for any purpose',
             'provided that the above copyright notice'], []),
    '0BSD': (['permission to use, copy, modify, and/or distribute this software for any purpose'],
             ['provided that the above copyright notice']),
    'Apache-2.0': (['apache license', 'version 2.0'], []),
    'Zlib': (["this software is provided 'as-is'", 'altered source versions must be plainly marked'], []),
    'Unlicense': (['this is free and unencumbered software released into the public domain'], []),
    'CC0-1.0': (['cc0 1.0 universal'], []),
    'MPL-2.0': (['mozilla public license', '2.0'], []),
    'GPL-2.0-or-later': (['gnu general public license', 'version 2'], ['lesser general public license']),
    'GPL-3.0-only': (['gnu general public license', 'version 3'], ['lesser general public license']),
    'GPL-3.0-or-later': (['gnu general public license', 'version 3'], ['lesser general public license']),
    'LGPL-2.1-or-later': (['gnu lesser general public license', 'version 2.1'], []),
    'LGPL-3.0-or-later': (['gnu lesser general public license', 'version 3'], []),
}

SDK_HEADERS = {'lakeshark_app.h', 'lakeshark_ui.h', 'lap3d.h', 'lap_canvas.h'}
STD_HEADERS = {'assert.h', 'ctype.h', 'errno.h', 'float.h', 'inttypes.h', 'iso646.h', 'limits.h', 'math.h',
               'stdalign.h', 'stdarg.h', 'stdbool.h', 'stddef.h', 'stdint.h', 'stdio.h', 'stdlib.h',
               'stdnoreturn.h', 'string.h'}

DECLARATION_TEXT = {
    'own_work_or_licensed': 'I wrote this or have the right to publish every part of it',
    'no_ai_cleanroom': "no AI was used to re-create someone else's code or design",
    'licenses_respected': "every dependency's license allows this use and is credited",
    'ethical_sources': 'no leaked, stolen, scraped-against-terms or proprietary material',
}

RULES = ['changes', 'bundle', 'app.json', 'ai', 'declarations', 'license', 'source', 'ownership']
RULE_TITLES = {
    'changes': 'Only app files changed',
    'bundle': 'Folder layout and file types',
    'app.json': 'app.json fields',
    'ai': 'AI use disclosed',
    'declarations': 'Declarations ticked',
    'license': 'Licenses allowed and compatible',
    'source': 'Source matches app.json',
    'ownership': 'App id owned by the submitter',
    'build': 'Builds with the SDK',
    'sim': 'Simulator smoke run',
}


class Problems:
    """Collected failures: (rule, app id or '', message)."""

    def __init__(self):
        self.items = []

    def add(self, rule, app, message):
        self.items.append((rule, app, message))

    def for_app(self, app):
        return [i for i in self.items if i[1] == app]

    def __bool__(self):
        return bool(self.items)

    def __iter__(self):
        return iter(self.items)

    def lines(self):
        return ['%s: %s' % (app or 'repo', msg) for rule, app, msg in self.items]


# ---------------------------------------------------------------- JSON Schema (the subset we use)

def _type_ok(v, t):
    return {'object': isinstance(v, dict), 'array': isinstance(v, list), 'string': isinstance(v, str),
            'boolean': isinstance(v, bool),
            'integer': isinstance(v, int) and not isinstance(v, bool)}.get(t, True)


def schema_errors(value, schema, path=''):
    """Yield (path, keyword, schema-node) for every failure. Supports type, const, enum, required,
    properties, additionalProperties: false, items, min/maxItems, min/maxLength, pattern, if/then/else."""
    if 'type' in schema and not _type_ok(value, schema['type']):
        yield path, 'type', schema
        return
    if 'const' in schema and (value != schema['const'] or type(value) is not type(schema['const'])):
        yield path, 'const', schema
    if 'enum' in schema and value not in schema['enum']:
        yield path, 'enum', schema
    if isinstance(value, str):
        if len(value) < schema.get('minLength', 0):
            yield path, 'minLength', schema
        if 'maxLength' in schema and len(value) > schema['maxLength']:
            yield path, 'maxLength', schema
        if 'pattern' in schema and not re.search(schema['pattern'], value):
            yield path, 'pattern', schema
    if isinstance(value, list):
        if len(value) < schema.get('minItems', 0):
            yield path, 'minItems', schema
        if 'maxItems' in schema and len(value) > schema['maxItems']:
            yield path, 'maxItems', schema
        if 'items' in schema:
            for i, item in enumerate(value):
                yield from schema_errors(item, schema['items'], '%s[%d]' % (path, i))
    if isinstance(value, dict):
        props = schema.get('properties', {})
        for key in schema.get('required', []):
            if key not in value:
                yield _join(path, key), 'required', schema
        for key, item in value.items():
            if key in props:
                yield from schema_errors(item, props[key], _join(path, key))
            elif schema.get('additionalProperties') is False:
                yield _join(path, key), 'additionalProperties', schema
    if 'if' in schema:
        branch = 'then' if not list(schema_errors(value, schema['if'], path)) else 'else'
        if branch in schema:
            yield from _merge_branch(value, schema, schema[branch], path)


def _merge_branch(value, parent, branch, path):
    # A then/else branch only narrows properties; check them against the merged rule so the
    # message can show both limits.
    if not isinstance(value, dict):
        return
    for key, sub in branch.get('properties', {}).items():
        if key in value:
            merged = dict(parent.get('properties', {}).get(key, {}))
            merged.update(sub)
            for err in schema_errors(value[key], sub, _join(path, key)):
                yield err[0], err[1], merged


def _join(path, key):
    return '%s.%s' % (path, key) if path else key


def _rule_for(path):
    top = re.split(r'[.\[]', path)[0]
    return {'ai': 'ai', 'declarations': 'declarations', 'license': 'license',
            'third_party': 'license'}.get(top, 'app.json')


def schema_message(path, keyword, node):
    leaf = path.rsplit('.', 1)[-1]
    if path.startswith('declarations.') and keyword in ('const', 'required', 'type'):
        text = DECLARATION_TEXT.get(leaf)
        if text:
            return '"%s" must be true: tick "%s"' % (path, text)
    if path == 'ai.tools' and keyword == 'minItems':
        return '"ai.tools" is empty: list the AI tools you used (or set "ai.use" to "none")'
    if path == 'ai.tools' and keyword == 'maxItems':
        return '"ai.use" is "none" but "ai.tools" lists tools: set "ai.use" or empty the list'
    if path == 'ai.extent' and keyword == 'minLength':
        return '"ai.extent" is missing: say in plain words what the AI did'
    if path == 'license' and keyword == 'enum':
        return '"license" must be one of: %s' % ', '.join(CODE_LICENSES)
    if keyword == 'required':
        return '"%s" is missing' % path
    if keyword == 'additionalProperties':
        return 'unknown field "%s" (check the spelling)' % path
    if keyword == 'type':
        return '"%s" must be %s %s' % (path, 'an' if node['type'][0] in 'aeiou' else 'a', node['type'])
    if keyword == 'const':
        return '"%s" must be %s' % (path, json.dumps(node['const']))
    if keyword == 'enum':
        return '"%s" must be one of: %s' % (path, ', '.join(map(str, node['enum'])))
    if keyword == 'minLength':
        return '"%s" is too short (at least %d characters)' % (path, node['minLength'])
    if keyword == 'maxLength':
        return '"%s" is too long (at most %d characters)' % (path, node['maxLength'])
    if keyword == 'pattern':
        if path == 'id':
            return '"id" may use only a-z, 0-9, _ and -, up to 31 characters, starting with a letter or digit'
        if leaf in ('url', 'source_url'):
            return '"%s" must be empty or a full web address (https://...)' % path
        return '"%s" has characters that are not allowed' % path
    if keyword == 'minItems':
        return '"%s" needs at least %d item(s)' % (path, node['minItems'])
    if keyword == 'maxItems':
        return '"%s" may have at most %d item(s)' % (path, node['maxItems'])
    return '"%s" is not valid (%s)' % (path, keyword)


# ---------------------------------------------------------------- bundle

def _is_png(data):
    return data[:8] == b'\x89PNG\r\n\x1a\n'


def _is_gif(data):
    return data[:6] in (b'GIF87a', b'GIF89a')


def check_bundle(app_dir, p, tag):
    """Folder layout, names, types, sizes. Returns the list of files (relative paths) or None."""
    app_dir = Path(app_dir)
    files, total, seen = [], 0, {}
    stack = [(app_dir, '')]
    while stack:
        here, rel = stack.pop()
        try:
            entries = sorted(here.iterdir(), key=lambda e: e.name)
        except OSError as e:
            p.add('bundle', tag, 'cannot read %s: %s' % (rel or 'the folder', e.strerror))
            return None
        for e in entries:
            r = rel + e.name
            if e.is_symlink():
                p.add('bundle', tag, '%s is a link; links are not allowed, copy the file in' % r)
                continue
            if e.name.startswith('.'):
                p.add('bundle', tag, '%s is a hidden file; remove it' % r)
                continue
            if not NAME_RE.match(e.name):
                p.add('bundle', tag, '%s: file names may use only letters, digits, . _ and - (no spaces)' % r)
                continue
            low = r.lower()
            if low in seen:
                p.add('bundle', tag, '%s and %s differ only by upper/lower case' % (seen[low], r))
                continue
            seen[low] = r
            if e.is_dir():
                if rel == '' and e.name in ('src', 'media'):
                    stack.append((e, r + '/'))
                else:
                    p.add('bundle', tag, '%s/ is not allowed; only src/ and media/ folders may be used' % r)
                continue
            if not e.is_file():
                p.add('bundle', tag, '%s is not a regular file' % r)
                continue
            size = e.stat().st_size
            total += size
            files.append((r, size))
    names = [f for f, _ in files]
    for r, size in files:
        folder, _, name = r.rpartition('/')
        ext = Path(name).suffix.lower()
        if folder == '':
            if name in ('LICENSE', 'app.json'):
                pass
            elif ext not in TOP_EXT:
                p.add('bundle', tag, '%s: this file type is not allowed (use .c .h .json .md .txt, '
                                     'pictures go in media/)' % r)
        elif folder == 'src':
            if ext not in SRC_EXT:
                p.add('bundle', tag, '%s: only .c and .h files may go in src/' % r)
        elif folder == 'media':
            data = (app_dir / r).read_bytes()
            if name == 'thumb.png':
                if not _is_png(data):
                    p.add('bundle', tag, 'media/thumb.png is not a real PNG picture')
                elif size > MAX_THUMB:
                    p.add('bundle', tag, 'media/thumb.png is over 300 KB')
            elif SCREEN_RE.match(name):
                if not (_is_png(data) if name.endswith('.png') else _is_gif(data)):
                    p.add('bundle', tag, '%s is not a real %s picture' % (r, ext[1:].upper()))
                elif size > MAX_SCREEN:
                    p.add('bundle', tag, '%s is over 300 KB' % r)
            elif name == 'clip.gif':
                if not _is_gif(data):
                    p.add('bundle', tag, 'media/clip.gif is not a real GIF')
                elif size > MAX_CLIP:
                    p.add('bundle', tag, 'media/clip.gif is over 1.5 MB')
            else:
                p.add('bundle', tag, '%s: media/ may hold only thumb.png, screen-1.png ... screen-4.png '
                                     '(or .gif) and clip.gif' % r)
        if ext in SRC_EXT or ext in ('.md', '.txt', '.json') or name == 'LICENSE':
            raw = (app_dir / r).read_bytes()
            try:
                raw.decode('utf-8')
                ok = b'\0' not in raw
            except UnicodeDecodeError:
                ok = False
            if not ok:
                p.add('bundle', tag, '%s is not plain UTF-8 text' % r)
    screens = [n for n in names if n.startswith('media/screen-')]
    if len(screens) > MAX_SCREENS:
        p.add('bundle', tag, 'too many screenshots (%d); at most %d' % (len(screens), MAX_SCREENS))
    if len(files) > MAX_FILES:
        p.add('bundle', tag, 'too many files (%d); at most %d' % (len(files), MAX_FILES))
    if total > MAX_TOTAL:
        p.add('bundle', tag, 'the app folder is %.1f MB; at most 2 MB' % (total / 1048576))
    if 'app.json' not in names:
        p.add('bundle', tag, 'app.json is missing')
    if 'LICENSE' not in names:
        p.add('bundle', tag, 'LICENSE is missing; add the full text of your license in a file named LICENSE')
    sources = [n for n in names if n.lower().endswith('.c')]
    if not sources:
        p.add('bundle', tag, 'no .c source file found (put them next to app.json or in src/)')
    base = {}
    for n in names:
        if n.lower().endswith(('.c', '.h')):
            b = n.rpartition('/')[2]
            if b in base:
                p.add('bundle', tag, '%s and %s have the same name; src/ files are built next to the '
                                     'top-level ones, rename one' % (base[b], n))
            base[b] = n
    return names


# ---------------------------------------------------------------- app.json

def load_app_json(app_dir, p, tag):
    path = Path(app_dir) / 'app.json'
    if not path.is_file():
        return None
    try:
        meta = json.loads(path.read_text(encoding='utf-8'))
    except UnicodeDecodeError:
        p.add('app.json', tag, 'app.json is not UTF-8 text')
        return None
    except json.JSONDecodeError as e:
        p.add('app.json', tag, 'app.json is not valid JSON (line %d: %s)' % (e.lineno, e.msg))
        return None
    if not isinstance(meta, dict):
        p.add('app.json', tag, 'app.json must be an object { ... }')
        return None
    return meta


def check_app_json(meta, p, tag, schema=None):
    schema = schema or load_json(SCHEMA)
    if meta.get('schema') != 2:
        p.add('app.json', tag, '"schema" must be 2 (community apps use app.json schema 2)')
        return
    for path, keyword, node in schema_errors(meta, schema):
        if path == 'schema':
            continue
        if path == 'license' and keyword == 'enum' and (
                meta.get('license') in ASSET_ONLY_LICENSES or _no_license(meta.get('license'))):
            continue  # check_licenses explains these
        p.add(_rule_for(path), tag, 'app.json: ' + schema_message(path, keyword, node))


def _no_license(lic):
    return isinstance(lic, str) and lic.strip().lower() in ('proprietary', 'unknown', 'none', '')


def check_licenses(meta, license_text, p, tag):
    lic = meta.get('license')
    if lic in ASSET_ONLY_LICENSES:
        p.add('license', tag, '%s is for pictures and data only; pick a code license for the app' % lic)
    elif _no_license(lic):
        p.add('license', tag, 'the app needs an open license from the list; "%s" is not accepted' % lic)
    if lic in LICENSE_TEXT and license_text is not None:
        text = ' '.join(license_text.lower().replace('’', "'").split())
        need, never = LICENSE_TEXT[lic]
        if not all(n in text for n in need) or any(n in text for n in never):
            p.add('license', tag, 'LICENSE does not look like the %s text; paste the full %s license' % (lic, lic))
    items = meta.get('third_party')
    if not isinstance(items, list):
        return
    for i, item in enumerate(items):
        if not isinstance(item, dict):
            continue
        name = item.get('name') or 'third_party[%d]' % i
        use, ilic = item.get('use'), item.get('license')
        if use == 'reference':
            continue  # a paper or spec read for the idea; its license does not apply to the app
        if not isinstance(ilic, str) or ilic not in ALL_LICENSES:
            p.add('license', tag, 'third-party "%s": license "%s" is not on the allowed list (%s)'
                  % (name, ilic, ', '.join(ALL_LICENSES)))
            continue
        if use == 'code' and ilic in ASSET_ONLY_LICENSES:
            p.add('license', tag, 'third-party "%s": %s is for pictures and data, not code' % (name, ilic))
            continue
        if use == 'code' and ilic in COPYLEFT_NEEDS and lic not in COPYLEFT_NEEDS[ilic]:
            p.add('license', tag, 'third-party "%s" is %s code, so the app must be licensed %s'
                  % (name, ilic, ' or '.join(sorted(COPYLEFT_NEEDS[ilic]))))


def check_ai(meta, p, tag):
    ai = meta.get('ai')
    if not isinstance(ai, dict):
        return
    tools = ai.get('tools')
    if ai.get('use') not in (None, 'none') and isinstance(tools, list) and any(
            not isinstance(t, str) or not t.strip() for t in tools):
        p.add('ai', tag, 'app.json: "ai.tools" has an empty name')
    extent = ai.get('extent')
    if ai.get('use') not in (None, 'none') and isinstance(extent, str) and len(extent.split()) < 3:
        p.add('ai', tag, 'app.json: "ai.extent" needs a few plain words on what the AI did')


# ---------------------------------------------------------------- source

def _strip_comments(src):
    out = re.sub(r'/\*.*?\*/', lambda m: '\n' * m.group(0).count('\n'), src, flags=re.S)
    return re.sub(r'//[^\n]*', '', out)


def manifest_fields(sources):
    """The LAP_APP(...) manifest fields found in the C files: {'id': ..., 'version': ...} or a reason."""
    found = []
    for name, text in sources.items():
        if not name.lower().endswith('.c'):
            continue
        code = _strip_comments(text)
        for m in re.finditer(r'\bLAP_APP\s*\(', code):
            body = code[m.end():m.end() + 4000]
            fields = {}
            for key in ('id', 'version', 'name'):
                f = re.search(r'\.%s\s*=\s*"([^"\\]*)"' % key, body)
                fields[key] = f.group(1) if f else None
            found.append((name, fields))
    return found


def check_source(app_dir, files, meta, p, tag):
    app_dir = Path(app_dir)
    sources = {}
    for f in files:
        if f.lower().endswith(('.c', '.h')):
            try:
                sources[f] = (app_dir / f).read_text(encoding='utf-8')
            except (OSError, UnicodeDecodeError):
                return None  # bundle check already said so
    local = {f.rpartition('/')[2] for f in sources}
    for f, text in sources.items():
        code = _strip_comments(text)
        for n, line in enumerate(code.splitlines(), 1):
            s = line.strip()
            if re.match(r'#\s*(embed|include_next|import)\b', s):
                p.add('source', tag, '%s line %d: #%s is not allowed' % (f, n, s.lstrip('#').split()[0]))
                continue
            m = re.match(r'#\s*include\s*(.*)$', s)
            if m:
                arg = m.group(1).strip()
                q = re.match(r'^"([^"]+)"', arg) or re.match(r'^<([^>]+)>', arg)
                if not q:
                    p.add('source', tag, '%s line %d: #include must name a file in quotes or <>' % (f, n))
                    continue
                inc = q.group(1)
                if '/' in inc or '\\' in inc or inc.startswith('.'):
                    p.add('source', tag, '%s line %d: #include "%s": include files by name only, no folders'
                          % (f, n, inc))
                elif arg.startswith('"') and inc not in local | SDK_HEADERS | STD_HEADERS:
                    p.add('source', tag, '%s line %d: #include "%s": no such file in the app or the SDK'
                          % (f, n, inc))
                elif arg.startswith('<') and inc not in SDK_HEADERS | STD_HEADERS:
                    p.add('source', tag, '%s line %d: #include <%s> is not available to apps' % (f, n, inc))
            if '.incbin' in s:
                p.add('source', tag, '%s line %d: .incbin is not allowed' % (f, n))
    found = manifest_fields(sources)
    if not found:
        p.add('source', tag, 'no LAP_APP(...) manifest found in the .c files')
        return None
    if len(found) > 1:
        p.add('source', tag, 'LAP_APP(...) appears %d times; an app has exactly one' % len(found))
        return None
    where, fields = found[0]
    if not fields['id']:
        p.add('source', tag, '%s: LAP_APP(...) has no .id = "..."' % where)
    elif isinstance(meta, dict) and isinstance(meta.get('id'), str) and fields['id'] != meta['id']:
        p.add('source', tag, 'app.json "id" is "%s" but LAP_APP in %s says .id = "%s"; make them the same'
              % (meta.get('id'), where, fields['id']))
    if fields['id'] and fields['id'] != app_dir.name:
        p.add('source', tag, 'the folder is apps/%s but the app id is "%s"; they must match'
              % (app_dir.name, fields['id']))
    if not fields['version']:
        p.add('source', tag, '%s: LAP_APP(...) has no .version = "..."' % where)
    elif not VERSION_RE.match(fields['version']) or '..' in fields['version']:
        p.add('source', tag, '%s: .version "%s" may use only letters, digits, . + _ - (up to 15)'
              % (where, fields['version']))
    return fields


# ---------------------------------------------------------------- ownership

def resolve_submitter(author_login, author_id, author_type='User', body='', bot_login=None):
    """Who submitted the PR: the author, or for the store bot the account named in its marker.
    Returns (submitter dict or None, problem message or None)."""
    is_bot = bool(bot_login) and author_login == bot_login and author_type == 'Bot'
    if not is_bot:
        if author_id is None:
            return None, 'the PR author is unknown'
        return {'github_id': int(author_id), 'login': author_login}, None
    marks = MARKER_RE.findall(body or '')
    if len(marks) != 1:
        return None, 'the store bot PR must carry exactly one submitter marker (found %d)' % len(marks)
    return {'github_id': int(marks[0][0]), 'login': marks[0][1]}, None


def _owner_entry_problem(app_id, entry):
    if not isinstance(entry, dict):
        return 'owners.json "%s" must be an object' % app_id
    if not isinstance(entry.get('github_id'), int) or isinstance(entry.get('github_id'), bool):
        return 'owners.json "%s" needs a numeric "github_id"' % app_id
    if not isinstance(entry.get('login'), str) or not re.fullmatch(LOGIN_RE, entry['login']):
        return 'owners.json "%s" needs a GitHub "login"' % app_id
    if not isinstance(entry.get('since'), str) or not DATE_RE.match(entry['since']):
        return 'owners.json "%s" needs "since" as YYYY-MM-DD' % app_id
    if set(entry) - {'github_id', 'login', 'since'}:
        return 'owners.json "%s" has unknown fields' % app_id
    return None


def check_ownership(changed_ids, removed_ids, base_owners, head_owners, submitter, p):
    """owners.json may only gain the submitter's own entries, and every changed app id must be free
    or already the submitter's."""
    if not isinstance(head_owners, dict):
        p.add('ownership', '', 'owners.json must be an object { "id": {...} }')
        return
    if submitter is None:
        return
    me, login = submitter['github_id'], submitter['login']
    for app_id, entry in head_owners.items():
        if base_owners.get(app_id) == entry:
            continue
        msg = _owner_entry_problem(app_id, entry)
        if msg:
            p.add('ownership', app_id if app_id in changed_ids else '', msg)
    for app_id, old in base_owners.items():
        new = head_owners.get(app_id)
        if new is None:
            p.add('ownership', app_id, 'owners.json: the entry for "%s" was removed; entries are never removed '
                                       'in a PR' % app_id)
        elif new != old and not (isinstance(old, dict) and old.get('github_id') == me
                                 and isinstance(new, dict) and new.get('github_id') == me):
            p.add('ownership', app_id, 'owners.json: the entry for "%s" belongs to someone else and was changed'
                  % app_id)
    for app_id in set(head_owners) - set(base_owners):
        entry = head_owners[app_id]
        if app_id not in changed_ids:
            p.add('ownership', '', 'owners.json: "%s" was added but this PR does not add apps/%s/'
                  % (app_id, app_id))
        elif isinstance(entry, dict) and entry.get('github_id') != me:
            p.add('ownership', app_id, 'owners.json: the new entry for "%s" must have your github_id (%d)'
                  % (app_id, me))
        elif isinstance(entry, dict) and entry.get('login') != login:
            p.add('ownership', app_id, 'owners.json: the new entry for "%s" must have your login (%s)'
                  % (app_id, login))
    for app_id in sorted(set(changed_ids) | set(removed_ids)):
        old = base_owners.get(app_id)
        if isinstance(old, dict) and old.get('github_id') != me:
            p.add('ownership', app_id, 'the app id "%s" belongs to %s; only that account can change it. '
                                       'Pick a different id for a new app.' % (app_id, old.get('login', '?')))
        elif old is None and app_id in changed_ids and app_id not in head_owners:
            p.add('ownership', app_id, 'new app: add "%s": {"github_id": %d, "login": "%s", "since": '
                                       '"YYYY-MM-DD"} to owners.json' % (app_id, me, login))


def check_changes(paths, p):
    """A submission PR touches only apps/<id>/... and owners.json. Returns (changed ids)."""
    ids = set()
    for path in paths:
        parts = path.split('/')
        if path == 'owners.json':
            continue
        if len(parts) >= 3 and parts[0] == 'apps' and parts[1]:
            ids.add(parts[1])
        else:
            p.add('changes', '', '%s is outside apps/ and owners.json; a maintainer has to review that by hand'
                  % path)
    return ids


# ---------------------------------------------------------------- driver

def validate_app(app_dir, p, schema=None):
    """Everything that can be checked from the folder alone. Returns manifest fields or None."""
    app_dir = Path(app_dir)
    tag = app_dir.name
    if not ID_RE.match(tag):
        p.add('bundle', tag, 'the folder name "%s" is not a valid app id (a-z, 0-9, _ and -)' % tag)
    files = check_bundle(app_dir, p, tag)
    if files is None:
        return None
    meta = load_app_json(app_dir, p, tag)
    if meta is not None:
        check_app_json(meta, p, tag, schema)
        check_ai(meta, p, tag)
        lic_path = app_dir / 'LICENSE'
        text = None
        if lic_path.is_file() and not lic_path.is_symlink():
            text = lic_path.read_text(encoding='utf-8', errors='replace')
        check_licenses(meta, text, p, tag)
    return check_source(app_dir, files, meta, p, tag)


def validate(app_dirs, changed=None, base_owners=None, head_owners=None, submitter=None, schema=None):
    """Run every check. app_dirs are the changed app folders that still exist; changed is the list of
    changed paths (PR mode) or None. Returns (Problems, {app id: manifest fields})."""
    p = Problems()
    schema = schema or load_json(SCHEMA)
    ids = {Path(d).name for d in app_dirs}
    removed = set()
    if changed is not None:
        ids_changed = check_changes(changed, p)
        removed = ids_changed - ids
    fields = {}
    for d in app_dirs:
        fields[Path(d).name] = validate_app(d, p, schema)
    if head_owners is not None:
        check_ownership(ids, removed, base_owners or {}, head_owners, submitter, p)
    return p, fields


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('apps', nargs='*', help='app folders (apps/<id>)')
    ap.add_argument('--repo', default=str(REPO), help='catalog checkout to check (default: this one)')
    ap.add_argument('--changed-from', help='check the app folders changed since this git revision')
    ap.add_argument('--base-owners', help='owners.json from main (for the ownership check)')
    ap.add_argument('--author', help='PR author login')
    ap.add_argument('--author-id', type=int, help='PR author numeric GitHub id')
    ap.add_argument('--author-type', default='User', help='PR author type (User or Bot)')
    ap.add_argument('--body-file', help='file holding the PR body (for the store bot marker)')
    ap.add_argument('--bot-login', help='login of the store bot, e.g. lakeshark-store[bot]')
    a = ap.parse_args(argv)
    repo = Path(a.repo).resolve()
    changed = None
    dirs = [Path(d).resolve() for d in a.apps]
    if a.changed_from:
        changed = changed_paths(repo, a.changed_from)
        ids = sorted({c.split('/')[1] for c in changed if c.startswith('apps/') and c.count('/') >= 2})
        dirs += [repo / 'apps' / i for i in ids if (repo / 'apps' / i).is_dir()]
    if not dirs and changed is None:
        ap.error('name at least one app folder, or use --changed-from')
    submitter = None
    pre = Problems()
    if a.author:
        body = Path(a.body_file).read_text(encoding='utf-8', errors='replace') if a.body_file else ''
        submitter, why = resolve_submitter(a.author, a.author_id, a.author_type, body, a.bot_login)
        if why:
            pre.add('ownership', '', why)
    head_owners = base_owners = None
    if (repo / 'owners.json').is_file():
        head_owners = load_json(repo / 'owners.json')
        base_owners = load_json(a.base_owners) if a.base_owners else head_owners
    p, _ = validate(sorted(set(dirs)), changed, base_owners, head_owners, submitter)
    p.items = pre.items + p.items
    for d in sorted(set(dirs)):
        if not p.for_app(d.name):
            print('PASS apps/%s' % d.name)
    for line in p.lines():
        print('FAIL ' + line)
    return 1 if p else 0


if __name__ == '__main__':
    sys.exit(main())
