FIXED

# Cluster new-p17-168-port — an empty Scala scale file loads as a constant-pitch tuning

## Decision and evidence

The finding is **REAL**, and it had **two** paths into the same defect: the command
surface (`mts.load_scale`) and the Microtuner dialog's own "Load" button. The core path
was fixed in `51e24f93e`; this round fixes the dialog path, which the round-3 review
named as the blocker.

### Path 1 — the command surface (fixed in `51e24f93e`, unchanged since)

Before the change, `readScaleLines()` in `src/core/SessionTuningScala.cpp` read the
`.scl` body with `limit` initialised to `0` and `i` to `-2`. A file that carries
neither header line left `limit` at `0` and `lines` empty, so the declared-vs-carried
check compared `0 != 0`, accepted the file, and `SessionTuning::loadScaleFile()` built
`intervals = {1/1}` — the interval it always starts with — and committed a table in
which every key has the same pitch. The guard at
`src/core/SessionTuningScala.cpp:131-141` refuses a stream that never reached the
degree-count line (`i < 0`).

### Path 2 — the Microtuner dialog (fixed in `327a46899`, this round)

The dialog read the file with **its own loop** and never checked that the file had a
name and a degree count at all:

- `src/gui/MicrotunerConfig.cpp:495-522` (before this round) — the slot opened the
  file, **cleared the form**, read the first line as the name and the second as the
  degree count, and then called `applyScale()` whatever the file held.
- `src/gui/MicrotunerConfig.cpp:323-328` (before this round) — `validateScaleForm()`
  accepts a blank name and an empty interval list, so nothing refused the file.
- `src/gui/MicrotunerConfig.cpp:400-436` (before this round) — `applyScale()` starts
  the interval vector with `1/1`, so an empty interval list produced a scale of
  **exactly one interval**, which was handed to `Song::setScale()` and fed to
  `SessionTuning::feedScale()`.
- `src/core/SessionTuning.cpp:138` — a one-interval scale makes every key answer the
  base frequency: the constant-pitch table the finding describes.

So an empty `.scl` (and a `.scl` that names a scale and never declares how many
degrees it has) installed a one-interval scale through the dialog, exactly as the
review reported.

**The fix.** The dialog reads its file with the core's rules instead of its own loop:

- `src/core/SessionTuningScala.cpp:332-357` — the new
  `SessionTuning::parseScaleFile(path, name, intervals, error)`: it opens the file and
  reads it with `readScaleLines()`, the same function `loadScaleFile()` uses, so the
  dialog and the command accept and refuse exactly the same files.
- `include/SessionTuning.h:137-148` — the declaration.
- `src/gui/MicrotunerConfig.cpp:515-538` — `MicrotunerConfig::loadScaleFile()` calls
  it and **returns before it touches the form**: a file that describes no scale is
  refused with a typed reason, so `Song::setScale()` is never called and
  `SessionTuning::feedScale()` never sees the scale.
- `src/gui/MicrotunerConfig.cpp:495-506` — the slot keeps the file picker and shows
  the refusal to the user; the picker stays out of the parse path.
- `include/MicrotunerConfig.h:60-67` — the declaration.

The rules the file must now satisfy (all of them the core's, none of them new): a
comment line starts with `!`, a blank line is skipped, the first line names the scale,
the second declares how many degrees follow, the count must be positive, and the file
must carry exactly that many interval lines.

### Callers and siblings checked

- `SessionTuning::parseScaleFile()` has exactly one caller: the dialog's
  `loadScaleFile()` (`src/gui/MicrotunerConfig.cpp:520`).
- `MicrotunerConfig::loadScaleFile()` has exactly one caller in the product: the
  "Load" button's slot `loadScaleFromFile()` (`src/gui/MicrotunerConfig.cpp:500`),
  which the Microtuner config widget connects to the button
  (`src/gui/MicrotunerConfig.cpp:101`).
- `SessionTuning::loadScaleFile()` (the command's path) is **unchanged** — it still
  reads the file with `readScaleLines()` and its error messages are byte-identical to
  before this round; the only new function is the shared reader beside it.
- The neighbouring `.kbm` reader (`keymapFields()` / `keymapOf()`,
  `src/core/SessionTuningScala.cpp:156-328`) is a separate path and is unchanged.
- The Microtuner **keymap** file path (`loadKeymapFromFile()`,
  `src/gui/MicrotunerConfig.cpp`) keeps its own parser: a `.kbm` is a different format
  with a different header, and the finding names the `.scl` path only.
- `applyScale()` is unchanged: the fix refuses the file *before* it, so the manual
  "Apply scale changes" button behaves exactly as before.

### Collateral damage

- **Valid `.scl` files load exactly as before**: the same name, the same interval
  lines and the same applied scale. The regression proves it — `aValidScaleFileStillLoads`
  checks the description ("JI three"), the interval count (4 = the implicit 1/1 plus
  the file's three degrees) and the scale the session table was fed.
- **The command path is untouched** (`loadScaleFile()` keeps its own messages and its
  own path-length cap); the shared reader is additive.
- **No project, saved data or undo path is touched.** The change is a parse-time
  refusal on the GUI thread; no audio-thread code, no mutex, no logging and no new
  control command is involved.
- **One user-visible message changes**: a file the dialog cannot read now reports the
  core's reason ("cannot read the scale file '…'") instead of "Unable to open selected
  file.". Both are refusals of the same case; the new one names the file.
- The dialog's form is no longer cleared by a refused file — it keeps the scale it had
  (before this round the refusal did not exist, so the form was cleared and a
  one-interval scale was applied).

## Verification record

All CTest commands ran from `build/tests`. The affected family
`ctest -R 'SessionTuning|Microtuner'` contains **4 CTest tests**: `SessionTuningTest`,
`SessionTuningPublicationTest`, `SessionTuningMenuTest` and the new
`MicrotunerScaleFileTest`.

`LOGS` = `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-168-port.r3/logs`
(the round-1 and round-3 runs keep their own folders next to it).

| Stage | Exact command | EXIT | Evidence |
| --- | --- | ---: | --- |
| Baseline build | `cmake --build build --target zene SessionTuningTest SessionTuningPublicationTest SessionTuningMenuTest > LOGS/build-1.log 2>&1` | 0 | The tree at `51e24f93e` builds. |
| Round-3 family check | `ctest -R SessionTuning --output-on-failure > LOGS/test-green-1.log 2>&1` | 0 | 3/3 passed. |
| Register the new test | `cmake --build build --target zene > LOGS/build-4.log 2>&1` | 0 | The build tree re-ran cmake (the new test target), `zene` rebuilt. |
| Build the new test | `cmake --build build --target MicrotunerScaleFileTest > LOGS/build-5.log 2>&1` | 0 | The regression builds. |
| **RED before the fix** (the seam commit `f82c656c4`, the old dialog behaviour) | `QT_QPA_PLATFORM=offscreen ./MicrotunerScaleFileTest > LOGS/test-red-5.log 2>&1` | **2** | `FAIL! … '!loaded' returned FALSE. (the empty .scl was accepted: it built a one-interval scale whose every key has the same pitch)` and the same for the file without a degree count; `3 passed, 2 failed`. The valid-file case passed. |
| Fixed build | `cmake --build build --target zene MicrotunerScaleFileTest SessionTuningTest SessionTuningPublicationTest SessionTuningMenuTest > LOGS/build-12.log 2>&1` | 0 | The fix and the four family executables build. |
| GREEN, whole family | `ctest -R 'SessionTuning|Microtuner' --output-on-failure > LOGS/test-green-4.log 2>&1` | **0** | 4/4 passed. |
| Save the product patch | `git diff HEAD~1 HEAD -- src/gui/MicrotunerConfig.cpp src/core/SessionTuningScala.cpp include/SessionTuning.h > LOGS/fix.patch` | 0 | The fix commit's product diff only (the seam commit is the behaviour-preserving extraction the regression drives). |
| Reverse the product patch | `git apply -R LOGS/fix.patch` | 0 | The refusal is gone; the dialog falls back to its own loop. |
| Proof build without the fix | `cmake --build build --target zene MicrotunerScaleFileTest > LOGS/build-16.log 2>&1` | 0 | Rebuilt with the fix reversed. |
| **Proof RED without the fix** | `ctest -R '^MicrotunerScaleFileTest$' --output-on-failure > LOGS/test-red-6.log 2>&1` | **8** | The same two assertions failed again (`3 passed, 2 failed`): the empty `.scl` and the `.scl` without a degree count were accepted. The test catches the bug. |
| Restore the product patch | `git apply LOGS/fix.patch` | 0 | The fix is back. |
| Proof build with the fix | `cmake --build build --target zene MicrotunerScaleFileTest SessionTuningTest SessionTuningPublicationTest SessionTuningMenuTest > LOGS/build-17.log 2>&1` | 0 | Rebuilt with the fix restored. |
| **Proof GREEN with the fix** | `ctest -R 'SessionTuning|Microtuner' --output-on-failure > LOGS/test-green-7.log 2>&1` | **0** | 4/4 passed. |
| Sibling command test | `ctest -R 'ControlSuccessPathsTest' --output-on-failure > LOGS/test-green-6.log 2>&1` | 0 | The other `mts.load_scale` user still passes. |
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > LOGS/gate-3.log 2>&1` | **0** | `RESULT: PASS`, all eight gates green. |
| Completion check | `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p17-168-port > LOGS/submit-check-2.log 2>&1` | **0** | `RESULT: PASS - submit`. |

### Why the regression drives a callable entry point

The dialog's Load button opens a **modal** file picker, and the offscreen platform
does not service the answering timer inside it: the existing `AafImportMenuTest`
(which answers a file dialog the same way) hangs in this environment the same way
(`LOGS/aaf-run.log`, killed by a 60 s timeout in the file-dialog case). The dialog's
`.scl` path was therefore extracted into `MicrotunerConfig::loadScaleFile(fileName,
error)` — the picker stays in the slot — so the regression can drive the real path
without a modal dialog. That extraction (`f82c656c4`) changes no behaviour; the fix
(`327a46899`) is the refusal, and the reverse-patch proof above reverses **only** that
commit's product diff.

| Gate | Result |
| --- | --- |
| fork-sources | PASS |
| all-sources-reproduce | PASS |
| no-upstream-regression | PASS |
| check-namespace | PASS |
| no-tautology | PASS |
| doc-figures | PASS |
| complexity (new items only) | PASS — no new item |
| file-length (new files only) | PASS — no new item |

## Self-check

- **The regression test FAILS with your product change reverted and PASSES with it (exit codes).** Yes. With the fix reversed, `ctest -R '^MicrotunerScaleFileTest$'` returned **8** (`test-red-6.log`): the empty `.scl` and the `.scl` without a degree count were accepted again (`3 passed, 2 failed`); the same two assertions had failed before the fix at the seam commit (`test-red-5.log`, exit 2). With the fix in place, `ctest -R 'SessionTuning|Microtuner'` returned **0** (`test-green-4.log`, `test-green-7.log`, 4/4).
- **The test is deterministic: no wall-clock timing, sleeps or races (name the seam or injected values it uses).** Yes. It writes the `.scl` files into a `QTemporaryDir` (a zero-byte file, a name-only file and a three-degree file), calls the dialog's own `loadScaleFile()` and compares the Song's scale slot and the scale the session table holds before and after. No timing, no sleep, no race-based synchronisation, no injected clock, and no modal dialog at all — which is also why it cannot hang.
- **The fix is at the cause and complete: every caller and sibling of what you changed was checked and stays consistent.** `SessionTuning::parseScaleFile()` has one caller (the dialog, `src/gui/MicrotunerConfig.cpp:520`); `MicrotunerConfig::loadScaleFile()` has one product caller (the Load button's slot, `src/gui/MicrotunerConfig.cpp:500`); the command's `SessionTuning::loadScaleFile()` is unchanged and still reads with the same `readScaleLines()` rules. The `.kbm` reader, the keymap file path and `applyScale()` are untouched. The dialog and the command now refuse the same files.
- **No collateral damage: old projects and saved data still load as before, and any limit or check you added does not hit other callers that legitimately exceed it.** No numeric limit was added; the check refuses only a `.scl` that describes no scale. Valid `.scl` files load exactly as before (the regression checks the description, the interval count and the fed scale), the command path and its messages are unchanged, and no project deserialisation, undo step or audio-thread path is touched.
- **If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended contract that shows it.** Not applicable — the verdict is FIXED, and the RED run with the fix reversed (`test-red-6.log`, exit 8) demonstrates the dialog path was real on this branch.
- **Realtime, undo and commands.** The refusal is a parse-time check on the GUI thread: nothing new on the audio thread allocates, frees, locks, waits or logs; a refused load changes no state, so no undo step is involved; no new control command was added (the `mts.*` group is untouched).

## Files committed

- `src/gui/MicrotunerConfig.cpp` — the dialog reads its `.scl` with the core's rules and refuses a file that describes no scale, before the form, the Song or the session table is touched.
- `include/MicrotunerConfig.h` — declares `loadScaleFile()`.
- `src/core/SessionTuningScala.cpp` — the shared `SessionTuning::parseScaleFile()` reader.
- `include/SessionTuning.h` — declares it.
- `tests/src/gui/MicrotunerScaleFileTest.cpp` — the new regression (fork-NEW): the empty `.scl` and the `.scl` without a degree count are refused with the Song's scale and the session table untouched; a valid `.scl` still loads.
- `tests/CMakeLists.txt` — registers the test and its offscreen platform.
- `tests/upstream-modifications.txt` — the ledger entries for the inherited files changed here.
- `tests/fork-sources.txt`, `tests/all-sources.txt` — the new test source's scope registration.
- `docs/qwen-fixes-2026-10/cluster-new-p17-168-port.md` — this report.
