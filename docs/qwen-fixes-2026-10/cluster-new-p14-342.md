FIXED

# Cluster new-p14-342: revision-only changes invalidate saved undo history

Finding (Luna patrol, `src/core/UndoHistory.cpp:237`, historical): the history fingerprint removes `writer` from `<head>` but hashes the `rev` and `writer` stamps on every other element, so a re-save that changes only those stamps discards valid history on reopen.

## Evidence

- The fingerprint hashes every attribute of every section: `src/core/DocumentIndex.cpp:229-244` (`sectionDigest` → `writeCanonical`), reached from `UndoHistory.cpp` `digestOfSections`.
- Only `<head writer>` was excluded before the fix: `src/core/UndoHistory.cpp:155` (pre-fix, see `git show HEAD:src/core/UndoHistory.cpp`).
- Revision stamps sit on track and clip elements: `src/core/Track.cpp:758` and `src/core/Clip.cpp:166` call `ProjectIds::writeRevision`, which sets `rev` and `writer` (`src/core/ProjectIds.cpp:356-363`).
- The content comparison the finding cites already strips them on every element: `src/core/ProjectIds.cpp:141-144` (`contentSignature`). So this is an inconsistency inside the codebase, not a new policy.

Verdict basis: REAL. The bug is reproducible (below). The realistic trigger is a re-save by a build whose stamps differ from the file's. A revision-aware build keeps the stamps on an unchanged save (`ProjectIds.cpp:336-364`: `rev` only moves on a content change), so the trigger is a build that rewrites them. I reproduced the failure with a stamp-only re-save (`rev=99`, `writer=another-process`) and did not reproduce it with a real second build.

## Fix

- `include/UndoHistory.h`: `fingerprint()` is now SCHEME v3 (excludes `rev`/`writer` on every element). New `fingerprintV2()` keeps the old value for histories already on disk. New member `m_loadFingerprintV2`.
- `src/core/UndoHistory.cpp`:
  - `stripRevisionStamps()` at `:91` (recursive, same rule as `ProjectIds::contentSignature`).
  - `digestOfSections(..., bool stripStamps)` at `:149`; the clone-and-strip runs only when stripping is requested, so the v1 and v2 digests are byte-for-byte what they were.
  - v3 `fingerprint()` at `:299-305`; `fingerprintV2()` at `:308`; `legacyFingerprint()` at `:314` (v1, unchanged output).
  - `beginLoad()` computes the v3 and v2 values at `:419-424`.
  - `fingerprintMatches()` at `:433-434`: a stored `sha256:v3:` matches v3, a stored `sha256:v2:` matches v2, a stored v1 value keeps its legacy rule.
- Why a new scheme and not an edit to v2: a v2 value written by an earlier build is stored in the project file. Changing the v2 digest would make every such history fail to match and be discarded on load, which breaks "saved data loads as before". Stored v2 and v1 values still match their own digests.

## Tests (`tests/src/core/UndoHistoryPersistenceTest.cpp`, all additions, no deletions)

- `aRevisionStampRewrittenWithoutAContentChangeKeepsTheHistory` (`:408`): save → open → two renames → save → re-stamp every element that carries `rev` (`restamp` at `:362`) → open → expect `loaded_steps == 2`, no discard.
- `aCurrentFormatHistoryUsingTheSchemeV2FingerprintStillLoads` (`:640`): same project with a stored `fingerprintV2()` value → must load.
- Helpers: `parseInto`, `writeDocument`, `saveRevisedAfterLoad` (`:394`).

## Commands and exit codes

Environment: `ccache` could not write its temp file under `~/.local/share/zene-bunny-fix/dsh/...` (permission denied), so every build ran with `CCACHE_DISABLE=1`. All logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-342.r1/logs`.

| Step | Command | Exit | Log |
|---|---|---|---|
| RED, product unmodified (earlier run, same test logic) | `CCACHE_DISABLE=1 cmake --build build --target zene UndoHistoryPersistenceTest -j4` | 0 | build-6-red.log |
| RED, product unmodified | `./UndoHistoryPersistenceTest aRevisionStampRewrittenWithoutAContentChangeKeepsTheHistory` | 1 | test-6-red-single.log: `loaded_steps:0`, "the project was changed after its undo history was saved" |
| 4b revert | `git diff -- include/UndoHistory.h src/core/UndoHistory.cpp > logs/fix.patch`; `git apply -R logs/fix.patch` | 0; 0 | fix.patch |
| 4b, final test file, product reverted | build (v2 test removed for this build only; file restored byte-identical afterwards, `cmp` ok) | 0 | build-12-4b-final-reverted.log |
| 4b, final test file, product reverted | `./UndoHistoryPersistenceTest aRevisionStampRewrittenWithoutAContentChangeKeepsTheHistory` | 1 | test-12-4b-final-reverted.log: `FAIL!` with the discard reason |
| 4b, product restored | `git apply logs/fix.patch`; rebuild | 0; 0 | build-13-4b-final-restored.log |
| GREEN, new test | `./UndoHistoryPersistenceTest aRevisionStampRewrittenWithoutAContentChangeKeepsTheHistory` | 0 | test-13-final-rev-green.log (3 passed) |
| GREEN, v2 guard | `./UndoHistoryPersistenceTest aCurrentFormatHistoryUsingTheSchemeV2FingerprintStillLoads` | 0 | test-11-v2.log (3 passed) |
| Family | `ctest -R '^UndoHistoryPersistenceTest$' --output-on-failure` (from `build/tests`) | 8 | test-13-final-family.log: 12 passed, 1 failed (see below) |
| Family | `ctest -R '^UndoHistoryOptionsTest$'` | 0 | test-8-options.log: 100% |
| Family | `ctest -R '^UndoHistoryPanelTest$'` | 0 | test-8-panel.log: 100% |

Note on the stale-binary run: one reverted build failed to compile (below), and the run after it used the previous binary and exited 0. That result is not evidence and is not used above; the rebuilt RED is `test-12`.

Family failure, unrelated to this change: `aSavedHistoryUndoesToEveryPreEditState` fails at its own precondition (`UndoHistoryPersistenceTest.cpp:210`): "the baseline needs a real instrument (tripleoscillator)". In this targeted build the `tripleoscillator` plugin is not found. It failed the same way in `test-2-red.log`, which ran before any product edit.

Gates, run one by one in the worktree (the `feature_check.py` wrapper itself cannot run here: it writes its logs to `<projects>/zene-qwen-new-p14-342-feature-check`, outside the write-jail; `PermissionError`, log `gates-1-feature-check.log`):

| Gate | Exit | Note |
|---|---|---|
| fork-sources | 0 | |
| all-sources-reproduce | 0 | |
| no-upstream-regression | 0 | |
| check-namespace | 0 | |
| no-tautology | 0 | |
| doc-figures | 0 | |
| complexity | 1 | No REGRESSION line names `UndoHistory*`. Listed items are in other files (WasmSandbox, XyPadPairTest, DawProjectReadTracks, ControlCommandsMixerStrip, ControlCommandsMixerTools, ControlCommandsSessionRecordLand). Red on the train. |
| file-length | 1 | No REGRESSION line names my files. Listed: Vst3Host.cpp, TakeLane.cpp, WasmSandbox.cpp and others. `UndoHistoryPersistenceTest.cpp` was over 500 code lines after my first draft (509); I moved the repeated setup into helpers, and it is no longer listed. |

Registration: no new files. The three changed files are already fork-new (`tests/fork-sources.txt:1186`, `:1951`, `:2642`), so `tests/upstream-modifications.txt` needs no entry.

## Self-check

- **RED/GREEN with the product change reverted and restored:** RED = `test-12-4b-final-reverted.log`, exit 1, with the product reverted (`git apply -R logs/fix.patch`, exit 0; build exit 0). GREEN = `test-13-final-rev-green.log`, exit 0, after `git apply logs/fix.patch` (exit 0) and rebuild (exit 0). The v2 guard cannot be RED on the old product because it calls the new `fingerprintV2` seam; that is a compile-time dependency, not a behavioural failure, so the behavioural RED is the first test.
- **Deterministic:** no sleeps, timers or threads. The test drives the project through the control surface (`project.save`, `project.open`, `track.rename`) and changes attributes on an in-memory DOM with fixed values (`rev="99"`, `writer="another-process"`). The assertions check only the discard flag, the step count and the save/open results, so they do not depend on attribute order or timing.
- **At the cause and complete:** the only callers of `fingerprint` are `seal()` (save, `UndoHistory.cpp:401`) and `beginLoad()` (load, `:422`); both now produce v3 and compare against the same v3 value. `digestOfSections` has three callers (v1, v2, v3), with the strip flag set only for v3. `contentSignature` (`ProjectIds.cpp:133-162`) strips `rev`/`writer` on every element, and v3 matches it. The `<head writer>` exclusion (`UndoHistory.cpp:155`, v2 path) is kept, and v3 covers the same attribute through the recursive strip. `metadata="1"` and `journallingObject` handling is not changed: `contentSignature` skips `journallingObject` and the fingerprint does not. That is a separate asymmetry; I did not change it because the finding is about revision stamps only.
- **No collateral damage:** stored v2 values still match their own digest (`aCurrentFormatHistoryUsingTheSchemeV2FingerprintStillLoads`, new, GREEN); stored v1 values still match the legacy rule (`aCurrentFormatHistoryUsingTheOldFingerprintStillLoads`, existing, passes); the other persistence tests pass (12 passed in the family, the one failure is the pre-existing plugin precondition). Nothing new on the audio thread: the change is in save/load code only. No limit or check was added. Settings, control commands and the ControlCommandsSnapshot are untouched.
- **Verdict basis:** REAL, not ALREADY-FIXED and not FALSE-POSITIVE: the fingerprint code at `DocumentIndex.cpp:229-244` hashes all attributes, and `rev`/`writer` are written by `ProjectIds.cpp:356-363`. The failure is reproduced with the old code (`test-6`, `test-12`).
