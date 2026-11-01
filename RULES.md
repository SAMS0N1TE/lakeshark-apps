# Rules

1. Say whether AI was used and how much (none, assisted, substantial, generated), which tools, and what they did.
   AI-written apps are allowed when disclosed.
2. No AI cleanroom designs. Do not use AI (or anyone) to read another project's code, docs or binaries and
   produce a "new" version to get around its license. Re-implementing from a public standard or your own
   understanding is fine; list what you used under `references`.
3. List every dependency, snippet, data set, asset and research source under `third_party` or `references`,
   with its license. That license must allow this use. Keep the credit in the source.
4. Ethical sources only: nothing leaked, stolen, confidential, scraped against terms, or bought from people
   who should not be selling it.
5. Radio: transmit only through the firmware's TX gate, on bands the user is licensed for. No jammers, no
   replay of other people's signals, no tracking people without consent.
6. No hidden network use, no data collection, no ads, no crypto mining, no obfuscated code.
7. One GitHub account per author. The account that first publishes an app id owns it; only it can update it.
8. False declarations get the app pulled and the account blocked.

## AI use levels

| `ai.use` | Meaning |
|---|---|
| `none` | no AI tools |
| `assisted` | autocomplete, small suggestions, help reading docs |
| `substantial` | AI wrote large parts; you designed it and reviewed every line |
| `generated` | AI wrote most or all of it |

Anything other than `none` needs `ai.tools` (names) and `ai.extent` (what the AI did, in plain words).

## Licenses

The app's own license, one of:
MIT, BSD-2-Clause, BSD-3-Clause, ISC, Apache-2.0, Zlib, Unlicense, CC0-1.0, 0BSD, MPL-2.0,
GPL-2.0-or-later, GPL-3.0-only, GPL-3.0-or-later, LGPL-2.1-or-later, LGPL-3.0-or-later.

`LICENSE` holds the full text of that license.

Third-party items may also use CC-BY-4.0 or CC-BY-SA-4.0, for assets and data only, never code.
Unknown or proprietary licenses are rejected. Items with `"use": "reference"` (papers, specs, docs you
read) are listed for credit; their license is not checked.

If you include outside code under a copyleft license, the app license must follow it:

| Outside code | App license must be |
|---|---|
| GPL-2.0-or-later | GPL-2.0-or-later, GPL-3.0-only, GPL-3.0-or-later |
| GPL-3.0-or-later | GPL-3.0-only, GPL-3.0-or-later |
| GPL-3.0-only | GPL-3.0-only |
| LGPL-2.1-or-later | LGPL-2.1-or-later, LGPL-3.0-or-later, or any GPL above |
| LGPL-3.0-or-later | LGPL-3.0-or-later, GPL-3.0-only, GPL-3.0-or-later |

Permissive code (MIT, BSD, ISC, Apache-2.0, Zlib, 0BSD, Unlicense, CC0-1.0) and MPL-2.0 code fit any app
license. Keep their notices.

## Declarations

Every submission sets all four to `true`:

- `own_work_or_licensed`: I wrote this or have the right to publish every part of it.
- `no_ai_cleanroom`: no AI was used to re-create someone else's code or design.
- `licenses_respected`: every dependency's license allows this use and is credited.
- `ethical_sources`: no leaked, stolen, scraped-against-terms or proprietary material.
