# VERSION-0.2.1-ALPHA: the version bump, and the two jobs that closed the release

**What this is.** The record of the last two mechanical jobs before Zene Studio's release is re-run and
published as **0.2.1-alpha**: the version bump that makes `tests/release-version-gate.sh` pass, and the four
`check-namespace` errors that were the one remaining cause of the tag's `checks.yml` failure. Evidence logs:
`tests/integration-logs-ver021/`. Branch: `chore/version-0.2.1-alpha`, cut from `post-alpha/integration`
tip `ba24a9578`.

**Why the number moved.** `v0.2.0-alpha` was created and pushed — it is in the product repository at
`b099fd6c` — and its build run came back **7 of 7 jobs red** across four platforms
(`tests/integration-logs-ci-fix/README.md` is the record of the five causes). The fixes landed at
`ba24a9578`. The `v*` ruleset forbids moving or deleting a tag for everyone with no bypass
(`docs/VERSIONING.md`, rule 2), so a corrected tag is not available and the release ships as the **next
number**. `0.2.1` is a **PATCH** by that same document's table — nothing was added, no project-format
change, no control-protocol change. `docs/VERSIONING.md`'s worked-example row for `0.2.1` previously read
"fixes a control-socket framing bug"; it now carries this release's real reason.

## 1. The gate, point by point

`tests/release-version-gate.sh` is the oracle. It reads four things:

| # | point | what it reads | before | at this branch |
|---|---|---|---|---|
| 1 | tree | `CMakeLists.txt` `VERSION_MAJOR`/`MINOR`/`RELEASE`/`STAGE` composed into `<version>` | `VERSION_RELEASE "0"` → `0.2.0-alpha` | `VERSION_RELEASE "1"` → **`0.2.1-alpha`** |
| 2 | docs | `docs/RELEASE-NOTES-v<version>.md` exists **and its H1 names `<version>`** | `docs/RELEASE-NOTES-v0.2.0-alpha.md`, H1 `Zene Studio 0.2.0-alpha` | renamed to `docs/RELEASE-NOTES-v0.2.1-alpha.md`, H1 `Zene Studio 0.2.1-alpha` |
| 3 | README | the Download section **names** `<version>` and **links** `releases/tag/v<version>` | `0.2.0-alpha` / `releases/tag/v0.2.0-alpha` | `0.2.1-alpha` / `releases/tag/v0.2.1-alpha` |
| 4 | tag | the release ref (`GITHUB_REF=refs/tags/v…`) must be `v<version>`; a `v*` tag at HEAD must be `v<version>`; a tag named `v<version>` that exists must be an ancestor of HEAD | `[skip]` — no tag exists | **still `[skip]`** — `refs/tags/v0.2.1-alpha` does not exist until the owner cuts it |

Nothing in the tree can satisfy point 4 before the tag exists: the gate's own message is
`[skip] tag: no v0.2.1-alpha tag exists yet (the owner creates it at freeze, Block C/F)`. A `[skip]` is not
a pass — what is verified here is that the gate is *reachable* for it (the tree declares exactly the version
the tag would have to be named, and any other `v*` tag at HEAD would now be a `[FAIL]`, which the harness
below exercises as R5).

The gate's own red/green harness, `tests/test-release-version-gate.sh`, derives the expected version from the
tree rather than hard-coding it, so it needed no edit and reports **8 of 8 controls behaving as declared** —
the six injected defects (release ref, tree/doc disagreement, reverted README, missing notes, wrong tag at
HEAD, tag not an ancestor) still exit 1 each.

## 2. What was renamed and updated

| file | change |
|---|---|
| `CMakeLists.txt` | `VERSION_RELEASE "0"` → `"1"` |
| `docs/RELEASE-NOTES-v0.2.0-alpha.md` → `docs/RELEASE-NOTES-v0.2.1-alpha.md` | renamed (`git mv`, so the history follows); H1 updated; a version note added at the top; the freeze-status item 2 ("the string that ships") corrected; the freeze-status item 1 and 3 command/path strings updated; the asset-name pattern updated |
| `README.md` | Download section: `0.2.1-alpha` and `releases/tag/v0.2.1-alpha`; one clause naming the superseded tag so a reader is not sent looking for a 0.2.0 download |
| `docs/KNOWN-LIMITATIONS.md` | H1; a version note; "no version-suffixed 0.2.1 limitations file"; "the 0.2.1 release notes carry no per-platform first-run steps" (these two are statements **about this release**, so they move) |
| `.github/ISSUE_TEMPLATE/alpha-feedback.yml` | `name`, `description`, the Build field's explanation and its placeholder; the untagged-build example now reads `0.2.0-alpha.12+b099fd6c` from the superseded tag |
| `docs/RELEASING.md` | new `## 0.2.1-alpha — what changes in this runbook` section; the publish sequence's tag, asset names, `--version` expectation and the `cat` of the notes file all moved to 0.2.1-alpha |
| `docs/VERSIONING.md` | the `0.2.1` worked-example row now gives this release's actual reason; the release-mechanics rule 1 named the CMake variable `PATCH` where the tree says `VERSION_RELEASE` (accuracy fix) |
| `docs/STATUS.md` | its pointer to the release notes now resolves (the old relative link would have 404'd after the rename) |
| `docs/CI-TAG-FAILURES.md` | a closing section: the four `check-namespace` errors this page left open are fixed, and how |
| `scripts/release-verify.sh` (**program workspace**, not this repo) | its own header and its `SUMMARY.md` H1 named `0.2.0-alpha`; both now say `0.2.1-alpha`. It does **not** hard-code the notes path — the script that did is `scripts/publish-alpha-release.sh`, whose `--notes` default was `docs/RELEASE-NOTES-v0.1.0-alpha.md`; that default is now derived from the tag (`docs/RELEASE-NOTES-$TAG.md`), so it cannot go stale on the next bump either |

## 3. What was deliberately kept, and why

The brief is explicit that the version bump must not rewrite history, and the reason is that the documents
that name `0.1.0-alpha` and `v0.2.0-alpha` are, in every case below, *accurate as written*:

- **Everything that names `0.1.0-alpha`** — the shipped release: `docs/RELEASE-NOTES-v0.1.0-alpha.md`,
  `docs/KNOWN-LIMITATIONS-v0.1.0-alpha.md`, the notes' `## Coming from 0.1.0-alpha` section and its
  "0.1.0-alpha rehearsal build" sentence, the limitations page's preserved "Getting it running" history, and
  `docs/RELEASING.md`'s worked example of the tag that actually shipped.
- **Every reference to `v0.2.0-alpha` as the superseded tag** — the notes' version note, `docs/RELEASING.md`
  §0.2.1-alpha and the publish sequence ("`v0.2.0-alpha` is not a candidate for this release"), the
  limitations page's version note, `docs/VERSIONING.md`, `docs/CI-TAG-FAILURES.md`,
  `tests/integration-logs-ci-fix/` (the failure record itself) and `tests/evidence/release-prep-0.2.0/`.
  These are the reason the re-cut exists; deleting or re-labelling them would erase the trail from a red tag
  to a green number.
- **Artefact paths that carry `0.2.0` in their names** — `drafts/RELEASE-NOTES-v0.2.0-alpha-DRAFT.md`,
  `drafts/KNOWN-LIMITATIONS-v0.2.0-alpha-DRAFT.md` and `docs/RELEASE-PREP-0.2.0.md`. These are the names of
  real files and real records; renaming the *references* would make them false. The notes' provenance block
  and the limitations page each say so in one line, so a reader is not left guessing why the names differ
  from the release's.
- **The past-pass records** — `docs/AUDIT-FIX-PASS.md`, `docs/FINAL-DOC-EDITS.md`,
  `docs/INDEPENDENT-NOTES-READ.md`, `docs/RELEASE-DOCS-FINAL-PASS.md`, `docs/INTEGRATION-MERGES-*`,
  `docs/COVERAGE-GATE-GREEN.md`, `docs/TELEMETRY-KILL-SWITCH.md`, `docs/UNDO-RELEASE-CONFIG.md`. They quote
  commands *as they were run* against the file as it was named; updating them would turn a transcript into a
  reconstruction.
- **`tests/advertised-features.tsv`** — its header says `# CHANGED FOR 0.2.0-alpha`, which is when the
  *contract* changed (the capability set this release ships). A PATCH bump changes no capability, so the
  manifest and the release-honesty gate that reads it stay exactly as they are; this was checked rather than
  assumed, because the gate binds the manifest to the binary's build options and both are unchanged.
- **Test fixtures that hard-code `version="0.2.0-alpha"`** — `tests/control_socket_harness.py`,
  `tests/agent-surface-gate.py`, `tests/control-socket-integration.py`, `tools/mcp-zene-control/tests/` and
  the `tools/mcp-zene-control/README.md` transcripts. These are *arbitrary* version strings standing in for
  "whatever the binary reports"; they are not claims about the shipping release, and moving them would churn
  fixtures without making anything truer.
- **`tests/evidence/release-prep-0.2.0/` and `tests/integration-logs-*/`** — measurements taken at the
  commits they name, including the `release-version-gate.log` that reads `declared version : 0.2.0-alpha`.
  That is what the gate said on that tree.

## 4. The four `check-namespace` errors, fixed rather than waived

`python3 tests/scripted/check-namespace` is run directly by `.github/workflows/checks.yml`, and on the
`v0.2.0-alpha` tag it was the single failing step of the `scripted-checks` job. Four errors, two kinds:

**a. `src/core/ControlDeviceHosted.cpp:139` and `:245` — "Missing comment // LMMS_HAVE_LV2".** The checker
requires its convention comment on an end-of-block `#endif` when the block is longer than a page or nests
another `#if`. Both blocks (`#ifdef LMMS_HAVE_LV2`, 43 and 51 lines) now carry `#endif // LMMS_HAVE_LV2`. The
file's other three `LMMS_HAVE_LV2` conditionals are short enough that the checker wants no comment, and were
left alone. **No ledger entry was owed:** the file is fork-NEW — added by the `post-alpha/lv2-catalogue`
lane (`c366d1bbd`) on top of upstream `4e677cb6c` — and is already declared in `tests/fork-sources.txt`;
`tests/upstream-modifications.txt` is the ledger for *upstream-inherited* files this fork diverges, and this
is not one. (Checked with `git log --diff-filter=A`, not from memory.)

**b. `include/ScriptLuaQtTypes.h` and `tools/ncpu-shim.c` — "File has no namespace lmms".** Both are false
positives, and both now sit on the checker's own designed allowance, `known_no_namespace_lmms` — the list the
script already keeps for exactly this purpose (`src/core/main.cpp`, `include/debug.h`,
`plugins/CarlaBase/DummyCarla.cpp`, `plugins/ZynAddSubFx/RemoteZynAddSubFx.cpp`) — each with the reason the
script's own style puts above its entries:

- the header declares **only** `luabridge::Stack<>` specialisations for Qt value types, and C++ requires a
  specialisation to live in the namespace of the template it specialises. Its namespace is `luabridge` by
  necessity; there is no class of ours in the file to put under `lmms`, so wrapping it would have been wrong,
  not merely noisy;
- the shim is **C**, and a C translation unit cannot carry a C++ namespace at all. It interposes
  `sysconf`/`get_nprocs`/`sched_getaffinity` so that `docs/RENDER-DETERMINISM.md`'s measurement can change
  the worker-pool size for one binary, and is not product code.

**The allowance list grew; the check did not weaken.** That is a claim, so it was tested as one — a gate never
seen to fail is not a gate:

| run | command | result |
|---|---|---|
| clean tree | `python3 tests/scripted/check-namespace` | `0 errors.`, exit 0 |
| negative control, argument path | `python3 tests/scripted/check-namespace build/namespace-negative-control/namespaceless-probe.cpp` — a scratch file with no namespace, **not on the list** | `1 errors.`, exit 1, naming the probe |
| negative control, **real enumeration path** | the same scratch file made visible to the checker's own `git ls-files` scope with `git add -f -N`, then run with no arguments | `1 errors.`, exit 1, naming the probe |
| back to clean | scratch file's index entry reset and the file removed, run again | `0 errors.`, exit 0 |

The third row is the one that matters: it drives `git ls-files`, the path CI uses, so a **new** file added
without a namespace is still reported — and the tree went from four errors to zero without the check losing
that ability. The transcript is `tests/integration-logs-ver021/02-check-namespace-negative-control.log`.
