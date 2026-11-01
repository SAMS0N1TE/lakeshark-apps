# LakeShark community apps

Apps made by LakeShark users, for the App Store on terminalbay.com and on the board.
Each folder in `apps/` is one app: C source, `app.json`, `LICENSE`, and optional pictures.
Every app here is built and signed by this repo's CI, then listed in the store.

## Submit an app

You need a GitHub account (30 days or older). You do not need to use GitHub itself.

- **LakeShark Studio**: open PUBLISH, sign in, fill the form, Submit.
- **lsdk**: `lsdk login`, then `lsdk publish` in your app folder.
- **Website**: terminalbay.com, App Store, Submit.
- **By hand**: open a pull request here that adds `apps/<id>/` and your `owners.json` entry.

Step by step: [SUBMITTING.md](SUBMITTING.md). The rules: [RULES.md](RULES.md).

## What happens next

1. Checks run on the submission: folder layout, `app.json`, licenses, AI disclosure, ownership, a build with the SDK.
2. The results show up as one comment on the pull request, and on your status page.
3. A maintainer reviews it. Merged means published.
4. Updates go the same way. Only the account that first published an app id can update it.

## Check locally

```
python tools/validate.py apps/<id>
python tools/build.py --sdk path/to/LakeShark/app-sdk apps/<id>
python tools/sim_check.py --sdk path/to/LakeShark/app-sdk apps/<id>
```

`build.py` needs the ESP-IDF 5.5 RISC-V compiler (`riscv32-esp-elf-gcc`). `sim_check.py` needs a full
LakeShark checkout and skips itself when it cannot run. Tests: `python -m pytest tests`.

## Repo layout

| Path | What |
|---|---|
| `apps/<id>/` | one app per folder |
| `owners.json` | which GitHub account owns each app id |
| `sdk.lock` | the LakeShark SDK commit every app is built with |
| `schema/app.schema.json` | `app.json` format |
| `tools/` | validate, build, simulate, catalog |
| `.github/workflows/` | PR checks, the check comment, publishing |

## License

Each app has its own license, in its folder.
