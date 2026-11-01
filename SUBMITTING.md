# Submitting an app

Read [RULES.md](RULES.md) first.

## Your app folder

```
app.json            store details (below), required
*.c *.h             source, next to app.json or in src/; at least one .c
LICENSE             full text of the license named in app.json
media/thumb.png     optional, PNG, up to 300 KB
media/screen-1.png  optional, up to 4 (screen-1 to screen-4), PNG or GIF, 300 KB each
media/clip.gif      optional, up to 1.5 MB
README.md NOTES.md  optional
```

- Up to 64 files and 2 MB in total.
- Allowed types: .c .h .json .md .txt .png .gif, and `LICENSE`.
- No binaries, no .lap files, no links, no hidden files, no other folders.
- The folder name, `app.json` `id` and the `.id` in `LAP_APP(...)` are the same.
- `#include` only your own files, the SDK headers and standard C headers, by name.

## app.json

```json
{
  "schema": 2,
  "id": "dice",
  "category": "games",
  "description": "One to three plain sentences.",
  "license": "MIT",
  "source_url": "",
  "ai": {"use": "none", "tools": [], "extent": ""},
  "third_party": [
    {"name": "tinyfont", "license": "CC-BY-4.0", "url": "https://example.com/tinyfont", "use": "asset"}
  ],
  "references": ["Name of the spec or paper you used"],
  "declarations": {
    "own_work_or_licensed": true,
    "no_ai_cleanroom": true,
    "licenses_respected": true,
    "ethical_sources": true
  }
}
```

- `category`: games, tools, radio, maps or utilities.
- `third_party[].use`: code, data, asset or reference.
- `status`: add `"status": "wip"` for an EARLY ACCESS badge.
- Name, version and author come from `LAP_APP(...)` in your source.
- Full format: [schema/app.schema.json](schema/app.schema.json).

## 1. From LakeShark Studio

1. Open your app, then PUBLISH from the menu.
2. Sign in with GitHub: open the link shown and type the code.
3. Fill in AI use, license, third-party items, references, and tick the four boxes.
4. Submit. The status list shows the checks and the review.

## 2. With lsdk

```
lsdk login
cd my-app
lsdk publish
lsdk submissions
```

`lsdk publish` checks `app.json`, builds once, uploads, and prints a status link. It asks for missing
fields when run in a terminal.

## 3. From the website

1. terminalbay.com, App Store, Submit.
2. Sign in with GitHub.
3. Upload a zip of your app folder and fill the form.
4. Keep the status link.

## 4. Pull request by hand

1. Fork this repo and add `apps/<id>/`.
2. For a new app id, add your entry to `owners.json`:
   `"<id>": {"github_id": 123, "login": "you", "since": "YYYY-MM-DD"}`.
   Your numeric id is at `https://api.github.com/users/<login>`.
3. Run `python tools/validate.py apps/<id>`.
4. Open the pull request and fill in the template. Change nothing outside `apps/<id>/` and `owners.json`.

## After you submit

- The check comment lists each rule as pass or fail, with what to fix.
- Fix and submit again; the same pull request is updated.
- A maintainer merges it, and the app shows up in the store.
