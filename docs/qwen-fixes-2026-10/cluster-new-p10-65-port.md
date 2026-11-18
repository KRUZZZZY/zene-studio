# Cluster new-p10-65-port: merged tempo map add reply

**Verdict: FIXED** (ported by hand from `bugs/qwen-new-p10-65` commit `2391734bf`; not merged, not cherry-picked).

Not ALREADY-FIXED: on `bugs/qwen-new-p10-65-port` (base `ded355440`) the regression fails before the change (see RED below).

## Finding and change

`tempo_map_add` built its reply from the submitted event, so a merged event lost its untouched half. `TempoMap::addEvent` merges per property (`src/core/TempoMap.cpp:140-158`), so the stored event at the tick can hold both halves while the input holds one.

- `src/core/ControlCommandsTransportMap.cpp:270-278` - new helper `resultingEventAt()`: returns the stored event at `tick`, or the submitted event as fallback.
- `src/core/ControlCommandsTransportMap.cpp:358-361` - the handler reads the event back from `song->tempoMap().map()` after the accepted edit, then calls `addReply()`. `replaced`, the inverse logic and the refusal path are unchanged.

The helper is a free function, so the handler's complexity does not grow.

## Regression test

`tests/src/core/ControlTempoMapCommandsTest.cpp:265-287` - `addReplyReportsTheMergedEvent()`: adds a 7/8 metre at tick 384, adds BPM 137 at the same tick, checks the map holds one event with the signature kept, and checks the BPM reply reports `has_tempo:true`, `has_time_signature:true`, `numerator:7`, `denominator:8`.

No new files were added, so no registration in `tests/fork-sources.txt` or `tests/upstream-modifications.txt` is needed. The test file already exists in the tree.

No control commands were added, so `ControlCommandsSnapshot` is unaffected.

## Evidence

Note: the default `ccache` temp dir (`/run/user/1000/ccache-tmp`) returned "Permission denied" in this sandbox. Every build below that succeeded used `CCACHE_TEMPDIR=/tmp/ccache-tmp-new-p10-65`. The first attempt is logged for transparency (EXIT=2). No project config was changed.

| Step | Exact command (worktree root unless noted) | Log | EXIT | Result |
|---|---|---|---|---|
| Build, ccache default dir | `cmake --build build --target zene ControlTempoMapCommandsTest -j2 > LOGS/build-red-1.log 2>&1; echo EXIT=$?` | build-red-1.log | 2 | ccache permission error; no compile |
| Build, RED state | `CCACHE_TEMPDIR=/tmp/ccache-tmp-new-p10-65 cmake --build build --target zene ControlTempoMapCommandsTest -j2 > LOGS/build-red-2.log 2>&1; echo EXIT=$?` | build-red-2.log | 0 | built |
| RED run (before fix) | `cd build/tests && ctest -R ControlTempoMapCommandsTest --output-on-failure > LOGS/test-red-1.log 2>&1; echo EXIT=$?` | test-red-1.log | **8** | `FAIL! ControlTempoMapCommandsTest::addReplyReportsTheMergedEvent()`, `Actual (has_time_signature): 0`, `Expected: 1`, `ControlTempoMapCommandsTest.cpp(285)`. 0% passed, 1 failed |
| Fix applied | edits to `ControlCommandsTransportMap.cpp:270-278, 358-361` | - | - | - |
| Build, GREEN | `cmake --build build --target zene ControlTempoMapCommandsTest -j2 > LOGS/build-green-1.log 2>&1; echo EXIT=$?` | build-green-1.log | 0 | built |
| GREEN run | `ctest -R ControlTempoMapCommandsTest --output-on-failure > LOGS/test-green-1.log 2>&1; echo EXIT=$?` | test-green-1.log | 0 | 100% passed, 1 of 1 |
| Proof: save product diff | `git diff -- src/core/ControlCommandsTransportMap.cpp > LOGS/fix.patch` | fix.patch | 0 | saved |
| Proof: revert product fix | `git apply -R LOGS/fix.patch` | - | 0 | reverted |
| Proof: build without fix | `cmake --build build --target zene ControlTempoMapCommandsTest -j2 > LOGS/build-proof-noFix.log 2>&1; echo EXIT=$?` | build-proof-noFix.log | 0 | built |
| Proof: RUN without fix | `ctest -R ControlTempoMapCommandsTest --output-on-failure > LOGS/test-proof-noFix.log 2>&1; echo EXIT=$?` (from `build/tests`) | test-proof-noFix.log | **8** | fails at `ControlTempoMapCommandsTest.cpp(285)` again |
| Proof: re-apply fix | `git apply LOGS/fix.patch` | - | 0 | re-applied; `git diff --stat` showed 2 files, 37 insertions |
| Proof: build with fix | `cmake --build build --target zene ControlTempoMapCommandsTest -j2 > LOGS/build-proof-fix.log 2>&1; echo EXIT=$?` | build-proof-fix.log | 0 | built |
| Proof: RUN with fix | `ctest -R ControlTempoMapCommandsTest --output-on-failure > LOGS/test-proof-fix.log 2>&1; echo EXIT=$?` | test-proof-fix.log | 0 | 100% passed, 1 of 1 |
| Affected family build | `CCACHE_TEMPDIR=/tmp/ccache-tmp-new-p10-65 cmake --build build --target zene ControlTempoMapCommandsTest TempoMapPersistenceTest TempoMapTest TempoMapPanelTest CommandPaletteTest MidiExportTest SmfInterchangeTest SmfInterchangeRoundTripTest -j2 > LOGS/build-family-1.log 2>&1; echo EXIT=$?` | build-family-1.log | 0 | built |
| Affected family run | `ctest -R "TempoMap\|ControlTempoMap\|SmfInterchange\|MidiExport\|CommandPalette" > LOGS/test-family-1.log 2>&1; echo EXIT=$?` (from `build/tests`) | test-family-1.log | 0 | **9 of 9 tests executed and passed** |
| Complexity gate, with fix | `bash tests/complexity-gate.sh --check > LOGS/gate-cc-1.log 2>&1; echo EXIT=$?` | gate-cc-1.log | **1** | 6 `REGRESSION` lines, none in the files touched here |
| File-length gate, with fix | `bash tests/file-length-gate.sh --check > LOGS/gate-len-1.log 2>&1; echo EXIT=$?` | gate-len-1.log | **1** | 14 `REGRESSION` lines, none in the files touched here |
| Complexity gate, base state | `git diff > LOGS/full-with-fix.patch; git apply -R LOGS/full-with-fix.patch`, then `bash tests/complexity-gate.sh --check > LOGS/gate-cc-base.log 2>&1; echo EXIT=$?` | gate-cc-base.log | **1** | same 6 regressions |
| File-length gate, base state | `bash tests/file-length-gate.sh --check > LOGS/gate-len-base.log 2>&1; echo EXIT=$?`, then `git apply LOGS/full-with-fix.patch` | gate-len-base.log | **1** | same 14 regressions |

The two gates fail in the same way on the base tree as with the fix. A `diff` of the sorted `REGRESSION` lines was empty for both gates (`cc-base.txt` vs `cc-fix.txt`, `len-base.txt` vs `len-fix.txt`, in the logs folder). The failures are pre-existing on `bugs/qwen-new-p10-65-port` at `ded355440`. They come from files this fix does not touch, for example `tests/src/gui/XyPadPairTest.cpp` (CCN 16), `src/core/DawProjectReadTracks.cpp` (CCN 13), `tests/src/core/VideoTrackTest.cpp` (589 lines), `src/core/TakeLane.cpp` (520 lines). The gate output also reports "baseline entry is dead weight" improvements, which suggests the baselines are stale on this line.

I did not fix these, because they are unrelated to this bug, and the rules forbid changing gate limits or baselines. Clearing them would mean restructuring about 20 unrelated files and functions. That needs its own cluster or a baseline decision by the owner.

## Files committed

- `src/core/ControlCommandsTransportMap.cpp` - `resultingEventAt()` helper and read-back before `addReply()`.
- `tests/src/core/ControlTempoMapCommandsTest.cpp` - regression `addReplyReportsTheMergedEvent()`.
- `docs/qwen-fixes-2026-10/cluster-new-p10-65-port.md` - this report.

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-65-port.r1/logs/`.
