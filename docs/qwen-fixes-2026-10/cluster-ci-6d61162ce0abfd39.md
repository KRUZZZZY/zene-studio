FIXED

# Cluster ci-6d61162ce0abfd39 - CI regression: quality-gates / static gates (3, 4, 6, 7, 8, 9, 11, 12)

Branch `bugs/qwen-ci-6d61162ce0abfd39`, base `af010bc3cc055d477045a54e2d866f59a95017d7` (`050/m0-train-26`),
HEAD `a1a98b67b2f9c961fd6e79c078815b4aedaa8695` (`050/m0-train-27`, the pinned commit the CI log names).

## Verdict: REAL - a reproduced source/test complexity failure (Gate 4, fork scope), not infrastructure

The hosted job's log is the `static gates (3, 4, 6, 7, 8, 9, 11, 12)` job, and its first failing step is
**Gate 4 - per-method complexity (CC <= 10 ratchet)**, `bash tests/complexity-gate.sh --check`
(`.github/workflows/quality-gates.yml:67-68`). The other numbers in the job name are the job's gate list,
not a list of failures.

Reproduced on this box at the exact pinned SHA, with the CI's own tool version:

- CI log: `~/.local/share/zene-bunny-fix/qwen/train-ci/a1a98b67b2f9c961fd6e79c078815b4aedaa8695/run-42805458711-job-129793314360.log`
  (8 `REGRESSION:` lines, `FAIL: complexity ratchet regressed`).
- The workflow installs lizard unpinned (`pip install lizard`, `.github/workflows/quality-gates.yml:63-64`),
  so CI measures with the current PyPI release. Local default is lizard 1.24.0; CI's is 1.25.1. With
  **1.25.1** the local gate reproduces the CI byte-for-byte - 8 regressions, `EXIT=1`
  (`gate-complexity-lizard1251-2.log`). With 1.24.0 the same command gives 7 (it counts `wav_facts` at 11,
  exactly its baseline value, where 1.25.1 counts 12) - `gate-complexity-1.log`.

Cause: the functions below are **new since the baseline was last refreshed** (2026-10-09, `40104e2369`)
and are not in `tests/complexity-baseline.tsv`, and two grandfathered functions rose. The gate's own
policy (`tests/complexity-gate.sh:1-20`) is that a new function over CCN 10, or a rise over the recorded
value, fails; the only compliant repair is to bring the functions under the target, **not** to re-anchor
the baseline (the train note says the same: "without raising baselines").

Seven of the eight items are also red at the base commit `af010bc3cc`
(`~/.local/share/zene-bunny-fix/qwen/gate-baselines/af010bc3cc055d477045a54e2d866f59a95017d7-complexity.log`),
i.e. the ratchet was already stale on the train; `wav_facts` is the one item CI saw that 1.24.0 did not.
That makes the failure inherited, not introduced by train-27 - but it is still the failure the job reports
and still a true violation of the gate's contract, so it is REAL and fixed here.

## The fix - behaviour-preserving extractions, one per offending function

No behaviour changes, no new control commands, no limit added, no baseline edit. Every helper below is a
pure extraction of code that was already in the function; the callers are unchanged.

| Function | File:line (after) | CCN before -> after | Extracted into |
| --- | --- | --- | --- |
| `lmms::wasm::WasmSandbox::loadModuleBytes` | `src/wasm/WasmSandbox.cpp:412` | 16 -> 6 | `Impl::resetModule` (`:148`), `Impl::readExports` (`:298`), `Impl::clampTuning` (`:325`) |
| `dotCentreAlong` | `tests/src/gui/XyPadPairTest.cpp:126` | 16 -> 1 | `isPadBackground` (`:85`), `coverageOf` (`:94`), `dotCentreIn` (`:107`) |
| `lmms::interchange::readdetail::parseNotes` | `src/core/DawProjectReadTracks.cpp:156` | 13 -> 7 | `refuseNotesTimeUnit` (`:108`), `parseNoteElement` (`:128`) |
| `lmms::setStrip` | `src/core/ControlCommandsMixerStrip.cpp:284` | 12 -> 5 | `validStripSettings` (`:244`, wraps the existing `finiteStripSettings`) |
| `lmms::adjust` | `src/core/ControlCommandsMixerTools.cpp:243` | 12 -> 9 | `adjustWouldChange` (`:231`) |
| `lmms::sessionrecord::landPairState` | `src/core/ControlCommandsSessionRecordLand.cpp:357` | 12 -> 8 | `pairSlotBlocker` (`:123`) |
| `wav_facts` | `tests/stem_commands_lib.py:143` | 12 -> 8 (lizard 1.25.1) | `wav_chunks` (`:126`) |
| `lmms::extract` | `src/core/VideoAudioExtract.cpp:217` | 11 -> 9 | `decodeFailureText` (`:209`) |

Notes on completeness (callers / siblings / the wider type):

- `parseNotes` keeps its one caller (`src/core/DawProjectReadTracks.cpp:216`) and the loop's `continue`
  semantics; `parseNoteElement` returns early exactly where the old code continued.
- `setStrip` keeps the same refusal text and the same short-circuit order: `validStripSettings` evaluates
  `finiteStripSettings` first, so a NaN setting is refused before the range terms, as before. Its sibling
  `getStrip`/`clearClips` are untouched.
- `adjust` keeps the "nothing to move" early return and the same clamp bounds; `adjustWouldChange` returns
  on the first channel that moves, as the old loop did.
- `landPairState` keeps the two distinct reasons (`column_has_no_song_track`, `session_grid_cell_removed`)
  and the empty-span check; the existing sibling predicate `pairHasSlot` (`:116`) is unchanged and still
  used by its own callers.
- `extract` keeps the same three failure texts and their order; `decodeAll`, `WavSink` and
  `extractVideoAudio` are untouched.
- `loadModuleBytes` keeps the order: size refusal, `resetModule` (module delete -> `hasProcess = false` ->
  `resetStore` when an instance existed), compile, fuel prime, instantiate, `readExports`, `clampTuning`.
  `readExports` writes the same members and returns the same refusal text.
- `wav_facts` keeps the same return tuple; the chunk walk is unchanged and the `fmt`/`data_bytes` check
  moved outside the `with` block (the file is closed one statement earlier, no observable difference).
- `dotCentreAlong` keeps the same rendering and the same coverage maths; `isPadBackground` is the old
  `isBackground` lambda body, `coverageOf` is the old `coverage` lambda with its low-contrast guard folded
  into the sum (`|span| < 1.0` adds 0.0 instead of continuing), and `dotCentreIn` is the old pixel loop with
  the two `continue`s folded into one guard. The helper block was kept compact so the file stays under the
  500-code-line ratchet (498; it was 499 before and 507 after the first version of the refactor).

## Regression test and RED/GREEN proof

The regression test is the repo's own Gate 4, `bash tests/complexity-gate.sh --check`, run with CI's
lizard 1.25.1 (`PATH="$TMPDIR/lizard1251/bin:$PATH"`). It is deterministic: a static source scan plus a
TSV comparison, no clock, no sleeps, no I/O beyond reading the sources. `fix.patch` is the full product
diff (all eight files; the gate measures test sources too, so the two test files are part of the fix, not
a separate test).

| Step | Command | EXIT |
| --- | --- | --- |
| Repro at the pinned SHA | `PATH="$TMPDIR/lizard1251/bin:$PATH" bash tests/complexity-gate.sh --check` (`gate-complexity-lizard1251-2.log`) | 1 (8 regressions) |
| **RED** (fix reversed) | `git apply -R fix.patch` then the same gate (`gate-red-14.log`) | **1** (8 regressions, byte-identical to the CI list) |
| **GREEN** (fix restored) | `git apply fix.patch` then the same gate (`gate-green-15.log`) | **0** (`PASS (check mode: no regressions; baseline not written)`) |
| GREEN, default lizard 1.24.0 | `bash tests/complexity-gate.sh --check` (`gate-complexity-1240-13.log`) | 0 |

The gate needs no build (it reads sources), so the reverse/restore cycle is the whole proof; the tree was
rebuilt and the family re-run afterwards (below).

## Commands and exit codes (logs in `~/.local/share/zene-bunny-fix/qwen/runs/ci-6d61162ce0abfd39.r1/logs/`)

| Step | Command | EXIT |
| --- | --- | --- |
| Build (core + 10 test targets + zene) | `cmake --build build --target zene ControlSessionRecordLandTest MixerStripTest MixerToolsTest DawProjectInterchangeRoundTripTest DawProjectDeflateImportTest VideoAudioNameTest VideoTrackTest XyPadPairTest StripMeterModeTest ConsoleStripTest -j4` (`build-16.log`) | 0 |
| Affected family | `cd build/tests && ctest -R '^(ControlSessionRecordLandTest\|MixerStripTest\|MixerToolsTest\|DawProjectInterchangeRoundTripTest\|DawProjectDeflateImportTest\|VideoAudioNameTest\|VideoTrackTest\|XyPadPairTest\|StripMeterModeTest\|ConsoleStripTest\|ControlStemCommands)$' --output-on-failure` (`test-family-17.log`) | 0 (**11/11** passed) |
| WasmSandbox TU (not built here: `WANT_WASM=OFF`, no wasmtime) | `g++ -fsyntax-only -Werror src/wasm/WasmSandbox.cpp` with the pinned wasmtime 48.0.1 headers (`wasmsandbox-syntax-20.log`) | 0 |
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py .` (`feature-check-18.log`) | 0 (**PASS**, table below) |
| Other static gates | `gates-other-19.log`: Gate 8 fork/tools, Gate 11, Gate 12, Gate 7 tools | 0 / 0 / 0 / 0 / 0 |

feature_check table (`feature-check-18.log`):

```
feature-check zene-qwen-ci-6d61162ce0abfd39 (base af010bc3c, 133 changed files)
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

## Out of scope, with evidence (pre-existing, inherited from the base, not this finding)

The `static gates` job has further red steps that this finding does not name and this branch did not
cause. They are recorded here so the next patrol does not re-file them as this cluster:

- **Gate 4, tools scope**: `merge_elem@tools/mmpz-git/mmpz_git.py` CCN 59, not in
  `tests/complexity-baseline-tools.tsv`. `tools/` is unchanged base..HEAD and the baseline is unchanged,
  so it is identical at `af010bc3cc` (`gates-other-19.log`, EXIT=1).
- **Gate 7, fork scope**: 26 `REGRESSION` lines, all inherited - the same 25 unique keys are in the
  cached base listing at `af010bc3cc` (`gate-baselines/af010bc3cc...-file-length.log`), and none of those
  files is in this branch's diff. `feature_check` treats file-length as red-on-the-train-on-purpose and
  only fails on items new in the branch's files; after this fix the branch adds none
  (`gate-filelength-9.log`, `feature-check-18.log`).

## Self-check

- **The regression test FAILS with your product change reverted and PASSES with it (exit codes).**
  RED `gate-red-14.log` EXIT=1 with the exact 8 CI regressions; GREEN `gate-green-15.log` EXIT=0. The
  patch reversed is `fix.patch` (all eight changed files) and re-applied cleanly both ways.
- **The test is deterministic: no wall-clock timing, sleeps or races.**
  `tests/complexity-gate.sh --check` runs `python3 -m lizard --csv` over the manifest and compares the
  over-target functions with `tests/complexity-baseline.tsv`. No clock, no sleeps, no threads, no network.
- **The fix is at the cause and complete: every caller and sibling of what you changed was checked and
  stays consistent.**
  The cause is the eight functions' CCN; each is extracted at its own seam, callers unchanged
  (`parseNotes` at `DawProjectReadTracks.cpp:216`, `extract` at `VideoAudioExtract.cpp:246`,
  `loadModuleBytes` via `WasmSandboxHost`/`readModuleFile`, `landPairState` at
  `ControlCommandsSessionRecordLand.cpp:461`, the control handlers via `ControlRegistry`). Siblings:
  `pairHasSlot`, `finiteStripSettings`, `decodeAll`, `getStrip`, `clearClips` and the command registrations
  are untouched; no command id, schema or reversibility class changed, so the
  `ControlCommandsSnapshot` canary is unaffected.
- **No collateral damage: old projects and saved data still load as before, and any limit or check you
  added does not hit other callers that legitimately exceed it.**
  No limit, check, command or schema was added; every extraction preserves the original short-circuit
  order and texts. The affected family (11 tests, including the DawProject import/round-trip and the
  stem-command end-to-end test that exercises `wav_facts`) passes 11/11. The one file whose length moved,
  `tests/src/gui/XyPadPairTest.cpp`, was brought back under the 500-code-line ratchet (498).
- **If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended contract that
  shows it.** Not applicable - verdict is FIXED.
- **Sibling commands in the command files you changed.** `ControlCommandsMixerStrip.cpp` registers the
  `mixer.*` strip group, `ControlCommandsMixerTools.cpp` the `mixer.*` tools group, and
  `ControlCommandsSessionRecordLand.cpp` the `session.record` land group. None of their registrations,
  schemas, handlers' externally visible behaviour or reversibility classes was touched: the diff only
  extracts file-local helpers (`validStripSettings`, `adjustWouldChange`, `pairSlotBlocker`) and leaves
  every `ControlRegistry` call and `ControlResult` text byte-identical. The sibling commands are
  therefore unaffected by construction; the family tests that drive them (MixerStripTest,
  MixerToolsTest, StripMeterModeTest, ConsoleStripTest, ControlSessionRecordLandTest) pass.
