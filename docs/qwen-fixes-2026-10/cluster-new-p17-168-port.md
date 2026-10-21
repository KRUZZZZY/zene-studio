FIXED

# Cluster new-p17-168-port — an empty Scala scale file loads as a constant-pitch tuning

## Decision and evidence

The finding is **REAL**, and it is still real on this branch (the newest line, base
`dbb15dded`): the fix was re-applied by hand, not merged, cherry-picked or rebased.

Before the change, `readScaleLines()` in `src/core/SessionTuningScala.cpp:110-143` read
the `.scl` body with `limit` initialised to `0` and `i` to `-2`. The loop consumes the
name line (`i == -2`), then the degree-count line (`i == -1`), then the interval lines.
For a file that carries neither header line the loop body never runs, so `limit` stays
`0` and `lines` stays empty. The count check at `src/core/SessionTuningScala.cpp:136`
(`lines->size() != limit`) then compared `0 != 0`, which is false, and the file was
**accepted**: `SessionTuning::loadScaleFile()` (`src/core/SessionTuningScala.cpp:321`)
went on to build `intervals = {1/1}` — the initial interval it always starts with
(`src/core/SessionTuningScala.cpp:349`) — and `commit()`ed a table in which every key has
the same pitch. The same held for a file that only *names* a scale and never declares a
degree count: `limit` was still `0`, so the count check passed again.

The fix is the guard at `src/core/SessionTuningScala.cpp:131-141`: a stream that never
reached the degree-count line (`i < 0`, i.e. `i == -2` for an empty file and `i == -1`
for a name-only file) is refused with a typed reason. The existing checks are untouched
and still cover their own cases — a non-positive or non-numeric degree count is refused
at `src/core/SessionTuningScala.cpp:122-126` ("no degree count in the .scl file"), and a
file that carries fewer interval lines than it declared is refused at
`src/core/SessionTuningScala.cpp:147` ("declares N degrees but carries M interval
lines"). The refusal happens inside `readScaleLines()`, so `loadScaleFile()` returns
before any interval vector, scale or table is built
(`src/core/SessionTuningScala.cpp:350-355`), and nothing is committed.

### Callers and siblings checked

- `readScaleLines()` has exactly one caller: `SessionTuning::loadScaleFile()`
  (`src/core/SessionTuningScala.cpp:350`).
- `SessionTuning::loadScaleFile()` has exactly one caller: the `mts.load_scale` command
  handler (`src/core/ControlCommandsMts.cpp:192`), which turns a parse refusal into the
  typed `ControlErrorKind::Refused` (`src/core/ControlCommandsMts.cpp:194`).
- The GUI path is the same command: `SessionTuningMenu` registers "Load Scala Scale..."
  with `mts.load_scale` (`src/gui/SessionTuningMenu.cpp:116`), so the menu and the
  socket/control path refuse the same file the same way.
- The neighbouring `.kbm` reader (`keymapFields()` / `keymapOf()`,
  `src/core/SessionTuningScala.cpp:150-317`) is a separate path and is unchanged.
- The Microtuner **dialog** (`MicrotunerConfig::loadScaleFromFile()`,
  `src/gui/MicrotunerConfig.cpp:495`) parses `.scl` files itself, with its own
  `i`/`limit` loop, for the per-track Microtuner — it does not call
  `SessionTuning::loadScaleFile()` and never reaches the session table. It is a
  separate product path with its own validation (`validateScaleForm()`), it is not the
  path this finding names, and it is deliberately left untouched here.

### Collateral damage

The guard rejects only a `.scl` stream that never produced a degree count. It adds no
numeric cap, touches no project deserialisation, and changes nothing for a valid
`.scl`: the name line, the degree count and the interval lines are read exactly as
before. The refusal happens before `m_scale`, `m_keymap` or the active table are
touched, which the regression proves by comparing the table snapshot before and after.

## Verification record

All CTest commands ran from `build/tests`. The affected family `ctest -R SessionTuning`
contains **3 CTest tests**: `SessionTuningTest`, `SessionTuningPublicationTest` and
`SessionTuningMenuTest`.

| Stage | Exact command | EXIT | Evidence |
| --- | --- | ---: | --- |
| Baseline build | `cmake --build build --target zene SessionTuningTest SessionTuningPublicationTest SessionTuningMenuTest > LOGS/build-1.log 2>&1` | 0 | Built `zene` and the three family executables on the untouched tree. |
| First red attempt (stale binary) | `ctest -R '^SessionTuningTest$' --output-on-failure > LOGS/test-red-1.log 2>&1` | 0 | NOT the proof: the executable had been compiled before the regression was added, so it did not contain the new case. Recorded here so the log is not mistaken for a red run. |
| Build with the regression | `cmake --build build --target SessionTuningTest > LOGS/build-2.log 2>&1` | 0 | The new case is in the executable now. |
| RED on the original parser | `ctest -R '^SessionTuningTest$' --output-on-failure > LOGS/test-red-2.log 2>&1` | **8** | `FAIL! ... aScaleFileWithoutNameOrDegreeCountIsRefused() '!loaded.ok' returned FALSE. (incomplete .scl '.../empty.scl' was accepted...)`; QtTest reported 24 passed, 1 failed — the 24 pre-existing cases still passed. |
| Fixed build | `cmake --build build --target zene SessionTuningTest SessionTuningPublicationTest SessionTuningMenuTest > LOGS/build-3.log 2>&1` | 0 | Rebuilt `zene` and the three family executables with the guard. |
| GREEN, whole family | `ctest -R SessionTuning --output-on-failure > LOGS/test-green-1.log 2>&1` | **0** | 3/3 passed (`SessionTuningTest`, `SessionTuningPublicationTest`, `SessionTuningMenuTest`). |
| Save product patch | `git diff -- src/core/SessionTuningScala.cpp > LOGS/fix.patch` | 0 | Only the product change, for the reverse-patch proof. |
| Reverse product patch | `git apply -R LOGS/fix.patch` | 0 | The guard is removed; the regression stays. |
| Proof build without the fix | `cmake --build build --target zene SessionTuningTest > LOGS/build-4.log 2>&1` | 0 | Rebuilt both targets with the product patch reversed. |
| Proof RED without the fix | `ctest -R '^SessionTuningTest$' --output-on-failure > LOGS/test-mutant-1.log 2>&1` | **8** | The same assertion failed again (`24 passed, 1 failed`): the empty `.scl` was accepted. The test catches the bug. |
| Restore product patch | `git apply LOGS/fix.patch` | 0 | The guard is back. |
| Proof build with the fix | `cmake --build build --target zene SessionTuningTest > LOGS/build-5.log 2>&1` | 0 | Rebuilt both targets with the guard restored. |
| Proof GREEN with the fix | `ctest -R SessionTuning --output-on-failure > LOGS/test-green-2.log 2>&1` | **0** | 3/3 passed. |
| First gate run | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > LOGS/gate-1.log 2>&1` | 1 | `fork-sources` and `all-sources-reproduce` FAILED: `tests/all-sources.txt` no longer reproduced from its own recipe. **Pre-existing train breakage, not caused by this fix** — the train-10 snapshot `5ef150729` had added the vendored `plugins/SfzPlayer/sfizz` tree (excluded from this scope on purpose since Q050-484, see `tests/upstream-modifications.txt` line 2805) to the entry list. Re-derived the list from the manifest's own "Regenerate with" recipe, as that manifest's header prescribes; no first-party source left the scope. |
| Final gate run | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > LOGS/gate-2.log 2>&1` | **0** | All eight gates PASS. |

`LOGS` = `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-168-port.r1/logs`.

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

- **The regression test FAILS with your product change reverted and PASSES with it (exit codes).** Yes. `ctest -R '^SessionTuningTest$'` returned 8 on the original parser (`test-red-2.log`) and 8 again with only the product patch reversed (`test-mutant-1.log`); `ctest -R SessionTuning` returned 0 with the fix restored (`test-green-1.log`, `test-green-2.log`).
- **The test is deterministic: no wall-clock timing, sleeps or races (name the seam or injected values it uses).** Yes. It writes a zero-byte `QFile` and a name-only `.scl` into a `QTemporaryDir`, drives the registered `mts.load_scale` command and compares the `mts.get_state` table snapshot before and after. No timing, no sleep, no race-based synchronisation, no injected clock.
- **The fix is at the cause and complete: every caller and sibling of what you changed was checked and stays consistent.** `readScaleLines()` has one caller (`loadScaleFile()`, `src/core/SessionTuningScala.cpp:350`), which has one caller (`mts.load_scale`, `src/core/ControlCommandsMts.cpp:192`); the GUI menu (`src/gui/SessionTuningMenu.cpp:116`) drives that same command. The `.kbm` reader and the Microtuner dialog's own parser are separate paths and are unchanged. The two neighbouring `.scl` checks (non-positive degree count, declared-vs-carried interval count) are untouched and still fire on their own cases.
- **No collateral damage: old projects and saved data still load as before, and any limit or check you added does not hit other callers that legitimately exceed it.** The guard adds no numeric limit and only refuses a `.scl` that never declared a degree count — a file that describes no scale. Valid `.scl` files, project deserialisation and every other session-tuning path are unaffected; the refusal lands before the scale or the table is built, which the regression proves with the before/after table snapshot.
- **If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended contract that shows it.** Not applicable — the verdict is FIXED, and the red run on the untouched parser demonstrates the finding was real on this branch.

## Files committed

- `src/core/SessionTuningScala.cpp` — the guard.
- `tests/src/core/SessionTuningTest.cpp` — the regression `aScaleFileWithoutNameOrDegreeCountIsRefused()` (line 950).
- `tests/upstream-modifications.txt` — the ledger entries for the two inherited files changed here.
- `tests/all-sources.txt` — the whole-tree scope manifest re-derived from its own recipe (pre-existing train breakage, see the gate record above).
- `docs/qwen-fixes-2026-10/cluster-new-p17-168-port.md` — this report.
