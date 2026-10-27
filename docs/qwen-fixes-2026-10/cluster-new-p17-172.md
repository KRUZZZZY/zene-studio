FALSE-POSITIVE: reSID's `SID::clock` takes the cycle count by reference and decrements it, so the scheduler's `consumed = requested - delta` is correct and the register sequence completes.

# Cluster new-p17-172: SID register scheduling past the first register

Log folder: `L=/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-172.r1/logs`

## The claim

Luna's finding says `reSID::SID::clock` takes its cycle count by value, so `delta` never changes, `consumed` stays 0, and the scheduler never gets past the first register delay.

## Evidence that the claim is wrong

- `plugins/Sid/resid/resid/sid.h:57`: `int clock(cycle_count& delta_t, short* buf, int n, int interleave = 1);`. The count is a reference. `plugins/Sid/resid/resid/siddefs.h.in:70` defines `cycle_count` as `int`.
- `plugins/Sid/resid/resid/sid.cc:836-848`: the buffered overload dispatches all four sampling modes with `cycle_count& delta_t`.
- `plugins/Sid/resid/resid/sid.cc:869` (`clock_fast`), and the same `(delta_t -= delta_t_sample) == 0` in `clock_interpolate` (`:911`), `clock_resample` (`:980`) and `clock_resample_fastmem` (`:1047`): `delta_t` is decremented in place by the cycles actually run, and left holding the cycles not yet run when `n` samples are full.
- `plugins/Sid/SidPlaybackScheduler.h:54-63`: `delta = requested`, `clock(delta, ...)`, `consumed = requested - delta`. That is the contract the reference API supports.
- The real caller is `plugins/Sid/SidInstrument.cpp:241-246`, which passes `&state->sid` (a `reSID::SID`, `SidInstrument.cpp:258`) into `sidplayback::fillBuffer`.

So `consumed` is not zero, `cycleBudget` and `cyclesRemaining` do decrease, and the scheduler moves on. The existing `SidClockProbe` in `tests/src/plugins/PluginPortsMigrationTest.cpp` also takes `int& cycles`, which matches the real API.

## What I checked by running code

The regression I added drives the real `reSID::SID` through `fillBuffer`. Its only assertions are that every register is written in `RegisterWriteOrder`, that each call spends no more than its budget, and that a zero budget does nothing. It passes on the unchanged scheduler.

To show the test catches the reported defect, I applied the finding's by-value semantics as a mutation (the chip gets a copy, so `delta` never moves). The test failed, so it does detect that defect. I then restored the header, and it passed again.

## Changes committed

| File | Change |
| --- | --- |
| `tests/src/plugins/PluginPortsMigrationTest.cpp` | Adds `#include <sid.h>`, the `ReSidWriteRecorder` adapter (`:3830-3849`), and the slot `sidSchedulerAdvancesThroughEveryRegisterWithReSid` (declared at `:1064`, defined at `:3851-3899`). |
| `tests/CMakeLists.txt` | `target_link_libraries(PluginPortsMigrationTest PRIVATE ... resid)` so the test can use the real chip. Nothing else in the build changes. |
| `tests/upstream-modifications.txt` | Two lines registering the inherited files above with a reason. |
| `docs/qwen-fixes-2026-10/cluster-new-p17-172.md` | This report. |

`plugins/Sid/SidPlaybackScheduler.h` is unchanged (`git diff --quiet` after the mutation check exits 0 against HEAD).

## Test matrix

| Path | Covered by |
| --- | --- |
| Normal: short 26-cycle segments, one sample each, full sweep of 25 registers including waveform registers 4/11/18 | `sidSchedulerAdvancesThroughEveryRegisterWithReSid`, sweep loop |
| Every call spends at most its budget (no over-clocking) | `QVERIFY(chip.clockedCycles - cyclesBefore <= CycleBudget)` |
| Exhaustion / non-progress: bounded at 1000 calls, so a stuck scheduler fails instead of hanging | `MaxCalls` in the sweep loop |
| Zero budget: no write, no sample, position unchanged | `idle` block |
| Old probe path (int& semantics with a stub chip) | `sidShortSegmentsKeepRegisterWritesOrdered`, unchanged |
| Realtime/allocation | Test code only. Nothing in `plugins/` or `src/` changes, so no audio-thread path changes. |
| Undo/commands | None touched. No control command added. |
| Old projects / saved data | No serialization touched. |

## Commands and EXIT codes

| Step | Command (run in worktree, `build/` tree) | Log | EXIT |
| --- | --- | --- | --- |
| Build, first try | `cmake --build build --target PluginPortsMigrationTest -j4` | `build-1.log` | 2 |
| | Failed in ccache (`failed to create temporary file ... Permission denied` under the sandboxed `dsh/home/ccache-tmp-r6`). Environment, not code. | | |
| Build with ccache temp in /tmp | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p17172 cmake --build build --target PluginPortsMigrationTest -j4` | `build-2.log` | 0 |
| Run, before decimator built | `cd build/tests && ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | `test-green-1.log` | 8 |
| | One failure, unrelated to the SID test: `nonRealtimeLadspaEffectsAreInvalid` says "SWH Decimator test fixture was not loaded" (`PluginPortsMigrationTest.cpp:1382`). `decimator_1202.so` was not built in this tree. | | |
| Build the missing fixture | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p17172 cmake --build build --target decimator_1202 -j4` | `build-3.log` | 0 |
| GREEN (unchanged code) | `cd build/tests && ctest -R '^PluginPortsMigrationTest$' -V` | `test-green-2.log` | 0 |
| | 84 passed, 0 failed, 2 skipped. New slot: `PASS` (line 197 of the log). | | |
| Mutation: by-value clock (the finding's bug) | Edit `plugins/Sid/SidPlaybackScheduler.h`: `int copy = delta;` and `sid->clock(copy, ...)`. Patch saved with `git diff -- plugins/Sid/SidPlaybackScheduler.h > mutation-by-value.patch`. | `mutation-by-value.patch` | |
| RED: rebuild with mutation | `CCACHE_TEMPDIR=... cmake --build build --target PluginPortsMigrationTest sid -j4` | `build-mut-1.log` | 0 |
| RED: run | `cd build/tests && ctest -R '^PluginPortsMigrationTest$' -V` | `test-red-1.log` | 8 |
| | 81 passed, 3 failed. `sidSchedulerAdvancesThroughEveryRegisterWithReSid`: `chip.writes.size() == NumSidRegisters` returned FALSE (the scheduler stopped before writing every register), `PluginPortsMigrationTest.cpp:3879`. The existing `sidShortSegmentsKeepRegisterWritesOrdered` also fails (`:3806`), and so does `migratedPluginsPreserveBehaviour` (`:2259`, max\|delta\|=0.231). | | |
| Restore header | `cp` the saved original back over `plugins/Sid/SidPlaybackScheduler.h`, then `git diff --quiet -- plugins/Sid/SidPlaybackScheduler.h` | | 0 |
| Rebuild after restore | `CCACHE_TEMPDIR=... cmake --build build --target PluginPortsMigrationTest sid -j4` | `build-green-2.log` | 0 |
| GREEN after restore | `cd build/tests && ctest -R '^PluginPortsMigrationTest$' -V` | `test-green-3.log` | 0 |
| | 84 passed, 0 failed, 2 skipped. | | |

The RED run is the mutation of the finding's by-value bug, not a failing test written before a fix. There was no bug to fix, so no product change was needed.

## Gates

`python3 qwen/feature_check.py <worktree>` could not run: it writes its logs to `<worktree>-feature-check`, one level above the worktree, which is outside the write-jail. It exited 1 with `PermissionError` (`gate-feature-1.log`, EXIT=1). I ran the same gate commands it lists, in the worktree, logging to the log folder.

| Gate | Command | Log | EXIT |
| --- | --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | `gate-fork-sources.log` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | `gate-all-sources-reproduce.log` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | `gate-no-upstream-regression.log` | 0 |
| check-namespace | `tests/scripted/check-namespace` | `gate-check-namespace.log` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | `gate-no-tautology.log` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | `gate-doc-figures.log` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | `gate-complexity-1.log` | 1 |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | `gate-filelength-1.log` | 1 |

The two ratchet gates are red on the train, as stated in the brief. None of their `REGRESSION` lines names a file this change touches (`PluginPortsMigrationTest.cpp`, `tests/CMakeLists.txt`). The listed items are the existing train items, such as `dotCentreAlong` in `XyPadPairTest.cpp` (CCN 16) and `WasmSandbox.cpp`. The new function is not listed, so it is under CCN 10.

## Self-check

Filled in after the completion check below.
