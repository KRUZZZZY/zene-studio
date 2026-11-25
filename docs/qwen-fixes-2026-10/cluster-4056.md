FIXED

# Cluster 4056 — Mastering undo silently ignores file-operation failures (BUG 8)

## Decision: REAL, fixed here

The finding cites `src/core/ControlMasteringSupport.cpp:199-211`; on this branch's HEAD the code is
`src/core/ControlMasteringSupport.cpp:230-256`. Both file operations in the recorded undo step threw their
result away:

- `git show HEAD:src/core/ControlMasteringSupport.cpp:244` — `for (const QString& path : createdCopy) { QFile::remove(path); }`, a
  static call whose bool is dropped. A directory without the owner write bit (or a file that is gone/in use) makes the
  removal fail and nothing says so.
- `:251-252` — `if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) { continue; }` and then
  `file.write(it.value());`, the write's `qint64` dropped. A read-only file, a short write or a failed open leaves the
  replaced .wav holding the run's bytes while undo reports success.

`undoThroughJournal` (`src/core/ControlCommandsControl.cpp:349-360`) concludes "undone" from
`journal->undo()` returning, so the caller is told the run was taken back over files that are still on disk. The
channel that exists for exactly this (`void` journal closures) is `recordActionRestoreFailure`
(`src/core/ControlReversibility.cpp:188`), read back by `actionRestoreFailureOf`
(`src/core/ControlCommandsControl.cpp:324-336`) as a typed `Refused`. `revisions.restore`'s own undo already uses it
(`src/core/ControlCommandsRevisions.cpp:158-195`); the mastering step was the outlier.

Reproduced before touching product code: `MasteringUndoFailureTest` on the unmodified tree reported
`FAIL! undoReportsACreatedFileItCouldNotRemove() '!reason.isEmpty()' returned FALSE`,
`FAIL! undoReportsAFileItCouldNotWriteBack()` and `FAIL! controlUndoReportsTheMasteringFailure() '!undone.ok' returned
FALSE` — `Totals: 3 passed, 3 failed` (`test-1-red.log`).

## The fix (cause, one file of product code)

`src/core/ControlMasteringSupport.cpp` only, plus the declaration's comment:

- `applyMasteringInverse(created, before)` (`:235-291`, file-static in an anonymous namespace) applies the whole
  recorded inverse and returns the refusals `'; '`-joined, empty when everything worked. Each operation still runs
  (`created` first, then the write-backs), so one refused file does not abandon the rest of the inverse. A name the
  run created that is already gone is NOT a failure (`QFile::exists` guard, `:263`) — the inverse's goal, an absent
  file, already holds. Removal uses the member `QFile::remove()` so `errorString()` is readable (`:265-270`); a failed
  open (`:280-284`) and a short write (`:287-294`) are both reported with the byte counts.
- The closure (`:305-317`) is now three lines of work and reports ONCE:
  `const QString why = applyMasteringInverse(createdCopy, beforeCopy); if (!why.isEmpty()) { recordActionRestoreFailure(...) }`
  — CCN 2, and the helper stays under 10 (7: two loops and four conditionals, no nesting).
- `include/ControlMasteringSupport.h:225-232` states the same contract on the declaration, as the header/definition
  rule requires.
- No new API, no change to `ControlReversibility.*`, `ControlCommandsControl.cpp`, `ControlCommandsMasteringRun.cpp`,
  `completeMasteringRun`, the rollback path or the stale-completion path, and no control command: the
  `ControlCommandsSnapshot` canary does not move. Nothing here runs on the audio thread; the closure is an undo, which
  is a UI/control-thread operation.

Sibling and caller check:

- The only caller is `completeMasteringRun` (`src/core/ControlMasteringSupport.cpp:595`, HEAD `:532`), which still
  records the step unchanged — untouched by this fix.
- The capture half (`captureMasteringWavDirectory`, `:145-186`) already refuses with a typed error when a file cannot be
  read, so the two halves of the transaction now report failures the same way.
- Other undo steps that touch files already report through the same channel (`ControlCommandsRevisions.cpp:158-195`,
  `ControlCommandsProjectFiles.cpp:388-422`); none of them regressed, and the family run below covers them.
- The other path into `recordMasteringUndo` does not exist: `multi_grep` finds one definition and one call site.

## Test: `tests/src/core/MasteringUndoFailureTest.cpp` (new file)

Registered in `tests/CMakeLists.txt` (list entry + `QT_QPA_PLATFORM=offscreen` property), `tests/fork-sources.txt` and
`tests/all-sources.txt`. Four slots on real files under a `QTemporaryDir`, each laying down one run's effect
(`mix.wav` overwritten with new bytes, `alt.wav` created) and recording its inverse from
`captureMasteringWavDirectory` — the product's own capture:

| Case | What it drives | Verdict against the fix |
|---|---|---|
| `undoReportsACreatedFileItCouldNotRemove` | directory chmod'd to owner `r-x` (no write), so `remove()` is refused; `projectJournal()->undo()`, then `takeActionRestoreFailure()` | **FAILS before, PASSES after**; the reason names `alt.wav` and "could not remove", the file is still there, and the write-back half still ran |
| `undoReportsAFileItCouldNotWriteBack` | the replaced file chmod'd `r--`; undo; `takeActionRestoreFailure()` | **FAILS before, PASSES after**; names `mix.wav` and "could not write", and the created file was still removed |
| `controlUndoReportsTheMasteringFailure` | same refusal, but through `ControlRegistry::instance()->invoke("control.undo")` | **FAILS before** (`ok` true), PASSES after: `Refused`, message contains the path |
| `undoOfARunThatCouldBeTakenBackRestoresAndRemovesByteExactly` | normal path | passes both ways (behaviour preserved): created file gone, replaced file byte-exact, channel empty |

Determinism: the refusal is a permission bit, read back with `QFile::permissions()` before the assertion; no sleeps, no
wall clock, no threads. `QSKIP` when `geteuid() == 0` (root ignores the bits) and on non-POSIX platforms. The run
reported `Totals: 6 passed, 0 failed, 0 skipped` — no skip, so the refusal was real (`qtest-1-green.log`).

Not provoked by a test: the SHORT-write branch (`file.write()` returning less than the buffer). Reaching it needs a
file-size rlimit or a full disk, and a process-wide `RLIMIT_FSIZE` can kill the test on `SIGXFSZ`; the failed-open
branch exercises the same `if`'s reporting path. Disclosed rather than claimed.

## Evidence (exact commands, EXIT codes; `LOGS = /home/kruzzzzy/.config/kads/zene/state/runs/4056.r7/logs`)

| Step | Command | EXIT | Log |
|---|---|---|---|
| build-tree regeneration (needed for a new test target) | `cmake --build build --target cmake_check_build_system` | 0 | `configure-2.log` |
| RED build attempt 1 | `cmake --build build --target MasteringUndoFailureTest -j4` | **2** | `build-1-red.log` — my test's own compile errors (`captureMasteringWavDirectory` not in scope: the helpers live in `lmms::control`); fixed by `using namespace lmms::control` |
| RED build | `cmake --build build --target MasteringUndoFailureTest -j4` | 0 | `build-2-red.log` |
| RED test (unfixed product code) | `cd build/tests && ctest -R MasteringUndoFailureTest --output-on-failure` | **8** | `test-1-red.log` — `Totals: 3 passed, 3 failed` |
| GREEN build | `cmake --build build --target MasteringUndoFailureTest -j4` | 0 | `build-3-green.log` |
| GREEN test | `cd build/tests && ctest -R MasteringUndoFailureTest --output-on-failure` | **0** | `test-2-green.log` |
| GREEN cases | `cd build/tests && ./MasteringUndoFailureTest` | 0 | `qtest-1-green.log` — `Totals: 6 passed, 0 failed, 0 skipped` |
| zene rebuilt (header touched) | `cmake --build build --target zene -j4` | 0 | `build-4-zene.log` |
| family targets | `cmake --build build --target RevisionRestoreCreatedFileUndoTest ProjectRestoreMissingTargetTest ReversibilityUndoTest -j4` | 0 | `build-5-family.log` |
| family tests | `cd build/tests && ctest -R "MasteringUndoFailureTest\|RevisionRestoreCreatedFileUndoTest\|ProjectRestoreMissingTargetTest\|ReversibilityUndoTest" --output-on-failure` | **0** | `test-4-family.log` — 4/4 passed |

The first family run (`test-3-family.log`, EXIT 8) is not a regression: those three binaries had never been built in
this build tree (`Not Run`), which is why they were then built.

### 4b — the test catches the bug (product files reverted, not the test)

| Step | Command | EXIT | Log |
|---|---|---|---|
| capture the product patch | `git diff -- src/core/ControlMasteringSupport.cpp include/ControlMasteringSupport.h > $LOGS/fix.patch` | 0 | `fix.patch` (123 lines) |
| revert | `git apply -R $LOGS/fix.patch` | **0** (`GIT_APPLY_R_EXIT=0`) | `proof-revert.log` |
| rebuild | `cmake --build build --target MasteringUndoFailureTest -j4` | 0 | `proof-revert.log` |
| run | `cd build/tests && ctest -R MasteringUndoFailureTest --output-on-failure` | **8** | `test-5-red-without-fix.log` — `Totals: 3 passed, 3 failed` |
| re-apply | `git apply $LOGS/fix.patch` | **0** (`GIT_APPLY_EXIT=0`) | `proof-reapply.log` |
| rebuild | `cmake --build build --target MasteringUndoFailureTest -j4` | 0 | `proof-reapply.log` |
| run | `cd build/tests && ctest -R MasteringUndoFailureTest --output-on-failure` | **0** | `test-6-green-with-fix.log` |

## Gates

`python3 .../qwen/feature_check.py <worktree>` — **EXIT 1**, before it printed any row: it computes the base
listings with `git -C .../qwen/gate-baselines/tree checkout -q --detach 9913d36d…`, which the lane guard refuses
(`bunny-guard: ... not allowed in a Bunny lane`), and the cache directory it falls back to is on a read-only mount
(`mount` shows `zene-bunny-fix ... type btrfs (ro,…)`; `touch` there returns `Read-only file system`). Log:
`gate-feature-1.log`. The same rows were therefore run directly:

| Row | Command | EXIT | Log |
|---|---|---|---|
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 | `gate-fork-sources.log` |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 | `gate-all-sources-reproduce.log` (before the entry was added: `gate-allsources-1.log` 0) |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 | `gate-no-upstream-regression.log` |
| check-namespace | `tests/scripted/check-namespace` | 0 | `gate-check-namespace.log` |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 | `gate-no-tautology.log` |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 | `gate-doc-figures.log` |
| file-length | `bash tests/file-length-gate.sh --check` | 0 | `gate-file-length.log` — no new file over 500 lines |
| unregistered-tests | `bash tests/unregistered-tests-gate.sh` | 0 | `gate-unregistered-1.log` — 835 sources, 833 registered, 0 violations |
| complexity | `bash tests/complexity-gate.sh --check` | **2** | `gate-complexity.log` — `lizard is not installed — run: pip install lizard`, so it produced no item at all; not a red row but an unmeasured one |

The complexity row is the one gate I could not execute: `lizard` is absent from this machine's Python (verified with
`python3 -c "import lizard"` → `ModuleNotFoundError`) and the lane has no network to install it. By hand, the new
helper has CCN 7 (`for`, `if (!exists)`, `if (!remove)`, `for`, `if (!open)`, `if (short write)` over a base of 1) and
the new closure CCN 2; the file is 614 lines against a 500-line rule that applies only to NEW files, and no file was
added under `src/` or `include/`.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it (exit codes).** Yes, both ways:
  `git apply -R fix.patch` + rebuild → `ctest` **8** (`test-5-red-without-fix.log`, `Totals: 3 passed, 3 failed`);
  `git apply fix.patch` + rebuild → `ctest` **0** (`test-6-green-with-fix.log`). The first RED, on the untouched tree,
  gave the same 8 (`test-1-red.log`).
- **The test is deterministic.** The refusal is a permission bit: `QFile::setPermissions(dir, ReadOwner|ExeOwner)` and
  the read-back assertion that `WriteOwner` is gone, then a read-only file for the write-back case. No sleeps, no
  wall-clock, no threads, no event-loop timing; the only injected values are the two byte buffers the test writes.
  `QSKIP` under `geteuid() == 0` and off POSIX.
- **The fix is at the cause and complete: every caller and sibling checked.** One caller
  (`completeMasteringRun:595`, unchanged), one definition. The capture half already refuses typed; the two other
  file-restoring undo steps in the tree (`ControlCommandsRevisions.cpp:158-195`,
  `ControlCommandsProjectFiles.cpp:388-422`) already report through the same channel and are covered by the family run
  (4/4, EXIT 0). `revisions.*`, `project.*` and `control.undo` were not edited.
- **No collateral damage.** The removal half keeps its previous semantics exactly (an already-absent created file is
  still a no-op success, now explicit instead of an ignored `false`); the write-back half still attempts every captured
  file; a successful undo emits an empty reason, which `actionRestoreFailureOf` treats as "no failure"
  (`ControlCommandsControl.cpp:326-330`). The test's success slot asserts the old behaviour byte for byte. No limit or
  check was added, so nothing that legitimately exceeds one is rejected. Saved projects are untouched: this file has no
  persistence of its own.
- **Not applicable here (non-feature unit):** no GUI path, no command id and no reversibility class were changed, so
  no `ControlCommandsSnapshot` canary, manual or GUI harness evidence is due. No `src/core/Song*`, `*Record*`,
  `*Undo*`, `*Transport*`, `Take*` or `AudioEngine*` file was touched, so no state-sequence artifact is due.

## Other findings

- `tests/CMakeLists.txt` changes do not reach an already-configured lane build tree by `cmake --build <target>`
  alone: an unknown target fails before `cmake_check_build_system` runs, so the build system had to be refreshed with
  `cmake --build build --target cmake_check_build_system` (allowed by the guard; `cmake -S . -B build` is not). Worth
  a line in the lane notes.
- `feature_check.py` cannot run in a lane at all today: its baseline cache lives under the read-only
  `zene-bunny-fix` mount and its fallback (`git checkout` in that tree) is refused by the guard. The ratchet rows are
  therefore unverifiable from inside a lane, which is what hides the missing `lizard` too.
