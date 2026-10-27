Verdict: FIXED

new-p15-164: FIXED - autosave identity sidecars are read without a size limit

## Evidence (the defect is real on this branch)

- `src/core/RevisionTimeline.cpp:212-219` (before the fix): `applyAutosaveIdentity` opened the `.info` sidecar and called `sidecar.readAll()` with no bound, on the UI thread, every time the timeline listed an autosave.
- `src/core/ProjectRecovery.cpp:185-190` (before the fix): `readRecoveryInfo`, the sibling reader of the same `<recover>.info` file used by the startup prompt, did the same unbounded `readAll()`.
- No other reader of the sidecar exists: `rg recoveryInfoPath` finds only `RevisionTimeline.cpp:214` and `ProjectRecovery.cpp:185/215/231` (the writer and the remover).

## Change

- `include/ProjectRecovery.h`: `ProjectRecovery::MaxRecoveryIdentityBytes = 128 KiB` and `readRecoveryIdentityText(sidecarPath, text)`.
- `src/core/ProjectRecovery.cpp`: `readRecoveryIdentityText` reads at most `MaxRecoveryIdentityBytes + 1` bytes. Over the bound, or unreadable, it returns false, which means "no sidecar". `readRecoveryInfo` now uses it.
- `src/core/RevisionTimeline.cpp`: `applyAutosaveIdentity` uses the same helper. Oversized data is treated as absent, so the autosave is reported the same way an upstream autosave with no sidecar is.
- Regression tests (in the families the brief names, plus the sibling reader):
  - `tests/src/core/RevisionTimelineTest.cpp::anOversizedIdentitySidecarIsUnavailableNotRead`
  - `tests/src/core/ProjectRecoveryTest.cpp::anOversizedIdentitySidecarIsReadAsAbsent`

## Exact commands and exit codes

Logs are in `LOG FOLDER` = `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-164.r1/logs`. Build targets run from `build/`; ctest runs from `build/tests`.

| Step | Command | EXIT | Result |
| --- | --- | --- | --- |
| Build RED, attempt 1 | `cmake --build . --target RevisionTimelineTest ProjectRecoveryTest -j4 > logs/build-1-red.log 2>&1` | 2 | Not a code failure: ccache could not create temp files under a directory the sandbox denies (`build-1-red.log` lines 18-19). |
| Build RED, attempt 2 | `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp cmake --build . --target RevisionTimelineTest ProjectRecoveryTest -j4 > logs/build-2-red.log 2>&1` | 0 | Built on the unfixed code. |
| **RED** | `ctest -R '^(RevisionTimelineTest\|ProjectRecoveryTest)$' --output-on-failure > logs/test-1-red.log 2>&1` | **8** | 0/2 families passed. The only failures are the new assertions: `ProjectRecoveryTest.cpp(387)` `'!info.hasIdentity' returned FALSE` and `RevisionTimelineTest.cpp(1201)` `'!autosave.isEmpty()' returned FALSE`. |
| Fix patch saved | `git diff -- include/ProjectRecovery.h src/core/ProjectRecovery.cpp src/core/RevisionTimeline.cpp > logs/fix.patch` | 0 | |
| Fix reverted | `git apply -R logs/fix.patch` | 0 | Only the two test files remained modified. |
| Reverted build | `CCACHE_TEMPDIR=... cmake --build . --target zene RevisionTimelineTest ProjectRecoveryTest -j4 > logs/build-4-reverted.log 2>&1` | 0 | |
| **4b RED** | `ctest -R '^(RevisionTimelineTest\|ProjectRecoveryTest)$' --output-on-failure > logs/test-3-reverted-red.log 2>&1` | **8** | Same two assertion failures (log lines 33, 75). The test catches the bug. |
| Fix reapplied | `git apply logs/fix.patch` | 0 | |
| Reapplied build | `CCACHE_TEMPDIR=... cmake --build . --target zene RevisionTimelineTest ProjectRecoveryTest -j4 > logs/build-5-reapplied.log 2>&1` | 0 | |
| **GREEN** | `ctest -R '^(RevisionTimelineTest\|ProjectRecoveryTest)$' --output-on-failure > logs/test-4-reapplied-green.log 2>&1` | **0** | 2/2 families passed. |
| GREEN, first run | `cmake --build . --target zene RevisionTimelineTest ProjectRecoveryTest -j4` (`build-3-green.log`), then `ctest` (`test-2-green.log`) | 0, 0 | Also passed. |
| Test counts | `ctest -R '^(RevisionTimelineTest\|ProjectRecoveryTest)$' -V > logs/test-5-count.log` | 0 | `ProjectRecoveryTest` Totals 29 passed, 0 failed; `RevisionTimelineTest` Totals 33 passed, 0 failed. |
| Related families | Built `ProjectRevisionRestoreStagingTest ProjectRevIdsTest RecordingRecoveryMenuTest` (`build-6-related.log`), then `ctest -R '^(ProjectRevisionRestoreStagingTest\|ProjectRevIdsTest\|RecordingRecoveryMenuTest)$' ...> logs/test-7-related-built.log` | 0, 0 | 3/3 passed. |
| Related family, unbuilt | `ctest -R '^(ProjectRevisionRestoreStagingTest\|ControlRecordingRecovery\|RecordingRecoveryMenuTest\|ProjectRevisionTest\|ProjectRevIdsTest)$' ...> logs/test-6-related.log` | 8 | Three tests were "Not Run" because their executables were not built yet. That is why I built them for test-7. `ControlRecordingRecovery` passed in this run (1 of the 4 names; the other three were Not Run). |

Note: `ProjectRevisionTest` is not a registered test in this tree (`ctest -N` lists zero matches), so it did not run.

## Gates

- `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` run directly: EXIT=1 with `PermissionError` (Errno 13). The script creates `<worktree-parent>/<name>-feature-check` before it runs any gate, and that directory is outside my write area. No table was produced. I did not edit the shared script.
- The same unmodified script run through `python3 -I -c` with only its log-directory line redirected to `logs/gates-1`: EXIT=0, `RESULT: PASS`. The table is in `logs/gates-1-table.log`:
  - PASS fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures (exit 0 each)
  - PASS complexity, file-length: no new item in my files

## Self-check

Procedure items 1-5 are answered above. The completion check's items:

1. **Each new test fails with the product change reverted, then passes restored.** Yes. The RED and reverted runs both exit 8 with the two assertions above; the reapplied run exits 0 (table above).
2. **Every acceptance criterion is proven by a test I ran.** The criterion is "oversized sidecars are read as unavailable, not in full". `RevisionTimelineTest::anOversizedIdentitySidecarIsUnavailableNotRead` (exit 0 with the fix, 8 without) proves the timeline side. `ProjectRecoveryTest::anOversizedIdentitySidecarIsReadAsAbsent` (same exit codes) proves the startup reader.
3. **One undo step, honest reversibility.** No command was added or changed and nothing is undoable. This is read-only code on the timeline and recovery listing.
4. **Nothing added runs on the audio thread.** Yes: `applyAutosaveIdentity` and `readRecoveryInfo` run on the UI or startup thread only. The new helper does a single bounded `QFile::read`. It is not on any audio path.
5. **Old projects still load and render as before; saved data round-trips.** Yes. The sidecar format and the writer are unchanged. A sidecar this build writes is four short lines plus the project path. The longest path is bounded by the platform: PATH_MAX is 4096 bytes on Linux, and Windows allows 32767 UTF-16 units, about 98 KB as UTF-8. Both are under 128 KiB, so the bound never truncates a sidecar this build writes. The existing `ProjectRecoveryTest` and `RevisionTimelineTest` cases still pass.
6. **Qt 5.15 / MSVC / macOS portability.** Only `QFile::open`, `QFile::read(qint64)`, `QIODevice::Text` and `QString::fromUtf8` are used. All exist in Qt 5.15 and Qt 6.
7. **Sibling commands.** Not applicable: no command was touched.
8. **Judgement call for review.** An oversized sidecar is now treated as absent. In `RevisionTimeline` that means its autosave is reported in this project's timeline, even if the oversized sidecar named another project. The same rule already applies to malformed sidecars: `parseRecoveryIdentity` fails, so the autosave is reported. In `ProjectRecovery::readRecoveryInfo`, an oversized sidecar that named another project used to be refused at the startup prompt. It is now offered as an identity-less recovery. This follows the finding's rule ("treat oversized or malformed identity data as unavailable") and the existing malformed-data handling. A reviewer who prefers fail-closed for oversized sidecars can change the helper's caller behaviour without changing the bound.
9. **Test determinism.** The padding is a fixed 1 MiB comment line. The tests use temp directories, fixed timestamps and no clock, sleeps or races. The 1 MiB padding is a literal rather than the constant, so the RED build compiles against the unfixed header. It is 8x the bound, so it still tests "over the bound".

## Files committed

- `include/ProjectRecovery.h`
- `src/core/ProjectRecovery.cpp`
- `src/core/RevisionTimeline.cpp`
- `tests/src/core/RevisionTimelineTest.cpp`
- `tests/src/core/ProjectRecoveryTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p15-164.md`

All six files are already registered in `tests/fork-sources.txt` (`src/core/ProjectRecovery.cpp:1859`, `src/core/RevisionTimeline.cpp:1881`, `include/ProjectRecovery.h:1048`), so no registration change was needed.

## Completion check (`submit_check.py`, `logs/submit-1.log`, EXIT=1)

Mechanical rows: PASS for commits on the branch, working tree clean, report present, tests added or changed (`ProjectRecoveryTest.cpp`, `RevisionTimelineTest.cpp`).

FAIL: `gates (feature_check.py)`. That row runs the unmodified `feature_check.py`, which cannot create `<worktree-parent>/zene-qwen-new-p15-164-feature-check` (`PermissionError`, Errno 13) because that directory is outside my write area. The same gates pass with the log folder redirected (`logs/gates-1-table.log`, EXIT=0, `RESULT: PASS`, see Gates above). This needs the owner or loop to run the gate with a writable log directory, or to create that directory. I did not change the shared script.

The four "?" items, quoted and answered:

- *The regression test FAILS with your product change reverted and PASSES with it (exit codes).* Yes. Reverted: `ctest` EXIT=8, both new assertions fail (`logs/test-3-reverted-red.log`). Reapplied: EXIT=0, 2/2 families pass (`logs/test-4-reapplied-green.log`).
- *The test is deterministic: no wall-clock timing, sleeps or races.* Yes. Fixed temp directories, fixed timestamps, and a fixed 1 MiB comment padding. No clock, sleep, thread or race is involved.
- *The fix is at the cause and complete: every caller and sibling was checked.* Yes. `applyAutosaveIdentity` has one caller, `findRevision` at `src/core/RevisionTimeline.cpp:279` (after the fix; it was line 275 before). The sibling reader is `readRecoveryInfo` at `src/core/ProjectRecovery.cpp:198`, now on the same helper. `rg recoveryInfoPath src include` shows no third reader. The writer (`ProjectRecovery.cpp:227`) and remover (`:243`) are unchanged.
- *No collateral damage.* Yes. The bound is 128 KiB and a sidecar this build writes is far under it (see Self-check item 5). The judgement call in item 8 is the one behaviour change outside the test cases, and it is flagged for review.

Verdict is FIXED, so the ALREADY-FIXED or FALSE-POSITIVE evidence ask does not apply.
