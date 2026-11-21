# Cluster new-p9-430-port: short SID playback segments

Verdict: **FIXED** (port of accepted `bugs/qwen-new-p9-430` commit `1cd00fe96` onto `050/m0-train-5`).
Ratchet gates: **both exit 1, on regressions in files this change does not touch** (see Gates below).

Log folder: `L=/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-430-port.r1/logs`

## Status before the port

Not already fixed on this line:

- `plugins/Sid/SidInstrument.cpp:238-280` (before the port) still ran `sid_fillbuffer()` with all 25 register writes and their fixed delays, then `sid->clock(tdelta)` with a possibly negative `tdelta`.
- `plugins/Sid/SidPlaybackScheduler.h` did not exist; no regression test covered short segments.

## What was ported

| File | Change |
| --- | --- |
| `plugins/Sid/SidPlaybackScheduler.h` (new, 158 lines) | Cycle-budgeted register-write scheduler (`sidplayback::fillBuffer`, `RegisterWriteOrder`, `RegisterWriteProgress`). Byte-identical to `1cd00fe96:plugins/Sid/SidPlaybackScheduler.h`, copied with `git show` because it is new and has no conflict. |
| `plugins/Sid/SidInstrument.cpp` | Per-note state `SidNoteState { reSID::SID sid; RegisterWriteProgress }` (`:54-59`) created in `playNoteImpl` (`:256`) and deleted in `deleteNotePluginData` (`:392`). `sid_fillbuffer` (`:240-245`) delegates to the scheduler and carries the cursor between periods. The `sidorder`, `SIDWRITEDELAY`, `SIDWAVEDELAY` constants move into the header. |
| `tests/src/plugins/PluginPortsMigrationTest.cpp` | Adds the include (`:22`), slot (`:712`), `SidClockProbe` (`:1904`) and `sidShortSegmentsKeepRegisterWritesOrdered()` (`:1936-1959`). Ported from the accepted commit. |
| `tests/fork-sources.txt` | Registers `plugins/Sid/SidPlaybackScheduler.h` (new product file). |
| `tests/upstream-modifications.txt` | Records the `plugins/Sid/SidInstrument.cpp` change with reason. |

**Deliberate deviation from the accepted commit:** the commit's `sid_fillbuffer` call site re-introduces `printf("!!!Not enough samples\n")` inside `playNoteImpl`, which runs on the audio render thread. Cluster new-p9-429 removed that printf (see `tests/upstream-modifications.txt:29`), so the port keeps the current call site with no return-value check and no printf. The scheduler header itself has no I/O.

Realtime check: `SidNoteState` is allocated only where `_n->m_pluginData` is null, as the old `new reSID::SID()` was. The scheduler is header-only and does not allocate.

## Regression: RED before / GREEN after

The probe in the test is a bounded fake clock that emits one sample after 22 cycles and treats an unconsumed write as pending. A scheduler that overruns its 26-cycle budget overwrites that pending write, and the test catches it.

Pre-fix code has no seam: `sid_fillbuffer` is `static` in `SidInstrument.cpp`. So the RED step has two parts.

| Step | Command (`$L` = log folder) | EXIT | Result |
| --- | --- | ---: | --- |
| RED-A: test on the pre-fix tree (header absent) | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p9430 cmake --build build --target PluginPortsMigrationTest > $L/build-red-B.log 2>&1` | **2** | `PluginPortsMigrationTest.cpp:22:10: fatal error: plugins/Sid/SidPlaybackScheduler.h: No such file or directory` (the first attempt, `build-red-A.log`, stopped earlier on the ccache error below) |
| RED-B: behavioural red, stand-in header with the pre-fix unbounded semantics | `cp $L/SidPlaybackScheduler.naive.h plugins/Sid/SidPlaybackScheduler.h`; `cmake --build build --target zene sid PluginPortsMigrationTest` (build-red-C.log) | 0 | builds |
| | `(cd build/tests && ctest -R '^PluginPortsMigrationTest$' --output-on-failure > $L/test-red-C.log 2>&1)` | **8** | `FAIL! sidShortSegmentsKeepRegisterWritesOrdered()`: `Actual (sid.overwrittenPendingWrites): 21`, `Expected (0): 0`, `Loc: PluginPortsMigrationTest.cpp(1948)` |
| Mutation check, rerun | `cp $L/SidPlaybackScheduler.naive.h plugins/Sid/SidPlaybackScheduler.h`; `cmake --build build --target sid PluginPortsMigrationTest > $L/build-mut-fail.log 2>&1` | 0 | builds |
| | `(cd build/tests && ctest -R '^PluginPortsMigrationTest$' --output-on-failure > $L/test-mut-fail.log 2>&1)` | **8** | same assertion, line 1948 (`test-mut-fail.log:163-166`) |
| GREEN: restore the accepted header | `cp $L/SidPlaybackScheduler.fixed.h plugins/Sid/SidPlaybackScheduler.h && cmp ...` | 0 | byte-identical to `1cd00fe96`'s header |
| | `cmake --build build --target sid PluginPortsMigrationTest > $L/build-restored-pass.log 2>&1` | 0 | builds |
| | `(cd build/tests && ctest -R '^PluginPortsMigrationTest$' -V > $L/test-restored-pass.log 2>&1)` | **0** | `PASS : sidShortSegmentsKeepRegisterWritesOrdered()` (`test-restored-pass.log:176`); `Totals: 46 passed, 0 failed, 2 skipped` |

The RED-C run also showed a second failure, `nonRealtimeLadspaEffectsAreInvalid` (SWH Decimator fixture not loaded). That was an environment gap: `decimator_1202` had not been built because only the test target was built. It was resolved with `cmake --build build --target decimator_1202` (`build-decimator-2.log`, EXIT 0). It is not part of this change.

**Affected family:** `PluginPortsMigrationTest` has 1 registered CTest. The verbose run executes 48 test functions: 46 pass, 0 fail, 2 skipped (both skips are pre-existing environment skips). Zero executed tests would be a failure; this run executed 48.

**Mutation (4b) note:** the procedure's literal `git apply -R` cannot swap the scheduler, because the scheduler is a new file and `git diff` of `SidInstrument.cpp` alone does not reach the test (the test exercises the header, not `SidInstrument.cpp`). The swap was therefore done by copying the saved stand-in and the saved fixed header (`$L/fix.patch` holds the header diff; `$L/fix-sidinstrument.patch` holds the production diff). The FAIL/PASS pair above is the proof that the test depends on the scheduler's budget logic.

## Build and environment notes

- `ccache` in this sandbox cannot write to `/run/user/1000/ccache-tmp`; every build was run with `CCACHE_TEMPDIR=/tmp/ccache-tmp-p9430`. Plain builds fail with that error before any compile (`build-red-A.log`, EXIT 2). This is an environment issue, not a code issue.
- The build tree is configured; no reconfigure was done.

## Ratchet gates

| Gate | Command | EXIT | Result |
| --- | --- | ---: | --- |
| Complexity | `bash tests/complexity-gate.sh --check > $L/gate-complexity-1.log 2>&1` | **1** | 6 `REGRESSION` lines, all in files this change does not touch: `tests/src/gui/XyPadPairTest.cpp` (`dotCentreAlong`, CCN 16), `src/core/DawProjectReadTracks.cpp`, `src/core/ControlCommandsMixerTools.cpp`, `src/core/ControlCommandsMixerStrip.cpp`, `src/core/ProjectRevisions.cpp`, `src/gui/ControlCommandsShortcuts.cpp`. The new functions this change adds (`fillBuffer`, `sid_fillbuffer`, the test function) are not in the output. |
| File length | `bash tests/file-length-gate.sh --check > $L/gate-filelength-1.log 2>&1` | **1** | 14 `REGRESSION: new file over 500 lines` lines (e.g. `src/core/TakeLane.cpp (520)`, `tests/src/core/DeviceDspTest.cpp (752)`). `SidInstrument.cpp`, `SidPlaybackScheduler.h` and `PluginPortsMigrationTest.cpp` are not listed (`grep -n "Sid\|PluginPorts"` on both logs: no hits). |

Why I believe these are pre-existing and not caused by this change: each gate measures per file and per function. None of the flagged files is in this diff (`git diff --stat` covers only the four tracked files listed above, plus the new header). Their metrics cannot move because of my edit. I did not change any gate, limit or baseline. Clearing these needs their owners' fixes, outside this cluster's scope.

## Files committed

- `plugins/Sid/SidInstrument.cpp`
- `plugins/Sid/SidPlaybackScheduler.h` (new)
- `tests/src/plugins/PluginPortsMigrationTest.cpp`
- `tests/fork-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-430-port.md`

## Not verified

- No CTest drives `SidInstrument` end to end in this build (`ctest -N -R Sid` lists only `PhaseDSidechainTest` and `SidechainInputsTest`, which are unrelated). The normal-size path is argued from reading the code, not measured: for a 512-frame period the cycle budget is about 11,400 and the 237 cycles of writes complete within one call, so the output path is the same as before. Running a rendered-audio comparison would be the way to confirm that.
