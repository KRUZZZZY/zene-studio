# VERSIONING: how Zene Studio numbers releases

> Owner instruction 2026-09-11: *"let's make the rule for updates — find the common convention and have
> the numbering be updated with stuff."* Convention adopted: **Semantic Versioning 2.0.0**, adapted for
> a pre-1.0 product. This file is the rule; `docs/RELEASE-NOTES-v<version>.md` is the per-release record.
> Lands in the product repo as `docs/VERSIONING.md`.

## The number

`MAJOR.MINOR.PATCH[-STAGE.N][+build]` — e.g. `0.2.0-alpha`, `0.3.0-beta.2`, `1.0.0`.

| component | moves when | does **not** move for |
|---|---|---|
| **MINOR** (`0.x`) | **stuff is added**: a new user-facing capability, a new paradigm wave, a new command group, or any change that alters the project file format or the control-socket protocol | fixes, docs, CI, tests, refactors with no behaviour change |
| **PATCH** | **only** for fixes: no new capability, no format change, no protocol change | anything additive |
| **MAJOR** | **stays 0 until v1.0.** `1.0.0` is reserved for the release that makes the project-format stability promise (the promise `docs/KNOWN-LIMITATIONS.md` says arrives with v1.0) and clears the complete-DAW bar | pre-1.0 breaking changes — those are a MINOR bump, and the release notes must say so |
| **STAGE** | `-alpha.N` while a release bar is still being assembled; `-beta.N` when it is feature-complete for the bar; `-rc.N` when only fixes are pending; **no stage** on a stable release | last stage of a line |

Pre-1.0 the leading `0.` is fixed and the two original `0.1.x` development versions are **not**
renumbered — the convention starts with the next release. Every bump after that follows the table.

## The three versions are separate

| version | where it lives | bumps when |
|---|---|---|
| **product** | tag `vMAJOR.MINOR.PATCH[-stage.N]`; reported by `-v`/About | per the table above |
| **protocol** | integer `proto` in the control-socket handshake (`AGENT-TOOLING.md` A12) | any breaking change to a command's args, result or error set — independent of the product number |
| **project format** | integer in the project XML | only when a previously saved file stops loading correctly |

The reported product string must come from the tag:
`VersionInfo.cmake` uses `git describe --tags --match v[0-9].[0-9].[0-9]*`; `CMakeLists.txt` is the
fallback for tagless builds. **The two must always agree** — the disagreement between them is exactly
what made the `0.1.0-alpha` version string ambiguous at release time.

## Release mechanics (non-negotiable)

1. `CMakeLists.txt` `VERSION_MAJOR/MINOR/RELEASE/STAGE` is bumped in the release commit, so a tagless CI
   build reports the same string as the tagged one.
2. A published tag is never re-pointed (the `v*` ruleset forbids it, no bypass). A bad release is fixed
   by cutting the **next** number — the release is deleted, the tag stays.
3. `docs/RELEASE-NOTES-v<version>.md` is written per release; `docs/KNOWN-LIMITATIONS.md` is
   **rewritten, not patched**, because it is the one page that cannot be vague.
4. The release body is assembled from those docs (`scripts/publish-alpha-release.sh`), with a
   `scripts/publish-release.sh` generalisation once the beta line starts.

## Worked examples

| release | why that number |
|---|---|
| `0.1.0-alpha` | first public alpha (history — the six-item install gate) |
| **`0.2.0`** | **adds the in-app agent control surface: new capability + a new protocol → MINOR** |
| **`0.2.1`** | **fixes only: the `v0.2.0-alpha` tag's build came back red on 7 of 7 jobs, and because a published tag is never re-pointed the fix ships as the next number — nothing new, nothing added** |
| `0.3.0` | Session View (wave W1) lands → MINOR |
| `0.4.0` | warp (W2) → MINOR |
| `0.9.0` | the project-format stability promise is written and enforced → last MINOR before 1.0 |
| `1.0.0` | complete-DAW bar cleared, format promise in force → MAJOR |

Nothing that ships without a user-visible change bumps anything: docs, CI, tests and refactors ride
along in the next release that does.
