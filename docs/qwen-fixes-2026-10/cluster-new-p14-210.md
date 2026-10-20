FIXED — cluster new-p14-210: the allocation counter misses over-aligned allocations

The `NotePlaybackTest` allocation counter replaced only the plain `operator new` and `new[]`. A `new` for an over-aligned type went to the library's `align_val_t` overload, so it was never counted. Checking the fix found a second defect in the same counter: its `thread_local` flag and count had internal linkage, and the optimiser folded the flag store and the count reads away. With the original code, `monstroNoteStartDoesNotAllocate` could not fail, whatever `playNote` allocated. The fix covers both.

## Evidence

Original (HEAD `2da2ed356`), `tests/src/core/NotePlaybackTest.cpp`: counters at `:77-78`; the only replacements are the plain ones at `:278-293`, with no `align_val_t` overload.

Fixed file, same path:

- `:80-81` the counters become `volatile`, with a comment on why.
- `:84` `OverAligned`, a test type with `alignas(64)`.
- `:291-310` `allocateAligned` / `freeAligned`: `malloc` with padding, and the `malloc` pointer stored just below the aligned address. Only `malloc`/`free` are used, so there is no platform split.
- `:325-333` the aligned `operator new` and `new[]`; `:339-342` the four aligned `operator delete` forms.
- `:425-440` `overAlignedAllocationsAreCounted`: `new OverAligned{}` plus `new OverAligned[2]{}` must count 2.
- `:442-453` `plainAllocationsAreCounted`: a positive control, `new int{0}` must count 1. A zero from `monstroNoteStartDoesNotAllocate` only means something if this counts.
- `:401-422` `monstroNoteStartDoesNotAllocate` is unchanged.

Diff: `git diff --stat` = 1 file, 82 insertions, 3 deletions (the test file).

Why `volatile`: the disassembly of an intermediate build showed the new test's code calling the aligned `operator new` but without the `countAllocations = true` store. The volatile-only revert below is the proof: with the aligned hooks in place and only `volatile` removed, both new tests read 0.

## Commands and exit codes

All test runs were `ctest -R "^NotePlaybackTest$"` from `build/tests`. Build logs and test logs are in the LOG FOLDER `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-210.r1/logs`.

| Step | Command | EXIT | Log |
| --- | --- | --- | --- |
| Build, first try (env failure, not code: ccache temp dir and inherited jobserver) | `cmake --build build --target zene NotePlaybackTest -j4` | 2 | `build-1.log` |
| Build with `CCACHE_TEMPDIR=/tmp/...` and `MAKEFLAGS` unset | `env -u MAKEFLAGS -u MFLAGS CCACHE_TEMPDIR=/tmp/zene-ccache-p14 cmake --build build --target NotePlaybackTest -j4` | 0 | `build-2.log` |
| Build the plugin (without it `monstro` loads as `dummyinstrument`) | `… cmake --build build --target monstro -j4` | 0 | `build-3-monstro-plugin.log` |
| **RED, initial test, no fix** | `ctest -R "^NotePlaybackTest$" --output-on-failure` | 8 | `test-2-red.log`: 23 passed, 1 failed; `overAlignedAllocationsAreCounted` Actual 0, Expected 2 |
| Intermediate: hooks only, counters not volatile | ctest | 8 | `test-3-green.log`: still 0. Fix incomplete |
| Intermediate build failed on `-Werror` | build | 2 | `build-5-green-escape.log`; the `ctest` run after it (`test-4-green-escape.log`) used a **stale binary** and is discarded |
| **RED, fix reverted** (`git apply -R fix-v2.patch`) | build `build-16-final-RED.log` then `ctest -R "^NotePlaybackTest$" -V` | build 0, **test 8** | `test-16-final-RED.log`: 23 passed, 2 failed; `overAligned…` Actual 0, Expected 2; `plainAllocations…` Actual 0, Expected 1 |
| Volatile-only revert (hooks kept) | build `build-18-volatile-only-v2.log`, ctest | build 0, **test 8** | `test-18-volatile-only-v2-RED.log`: both new tests fail, 0 vs 2 and 0 vs 1 |
| **GREEN, fix applied** (`git apply fix-v2.patch`) | build `build-17-final-GREEN.log`, ctest -V | build 0, **test 0** | `test-17-final-GREEN.log`: 25 passed, 0 failed |
| Restored file after the volatile-only check | build `build-19-restored-green.log`, ctest -V | build 0, **test 0** | `test-19-restored-green.log`: 25 passed, 0 failed |

Affected-family count: `ctest -R "^NotePlaybackTest$" -V` executed 1 test binary with 25 QTest functions (25 `PASS   :` lines in `test-19-restored-green.log`).

The 4b patch is `fix-v2.patch` in the LOG FOLDER. It is the diff from `fixless-v2-NotePlaybackTest.cpp` to `green-v2-NotePlaybackTest.cpp`, with the `a/`/`b/` paths of the repo file. It covers the include, the `volatile` counters, the allocator, and the overloads. The test additions stay in both files, so the patch reverses only the fix.

## Gates

`feature_check.py` could not run here. It writes its logs to `<worktree>/../<name>-feature-check`, which is outside the write-jail (`gate-feature-check-1.log`: `PermissionError`, EXIT 1). I did not change the script. Instead I ran each gate command it runs, from the worktree, with logs in the LOG FOLDER (final run = `gate-*-3.log`):

| Gate | Command | EXIT |
| --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 |

The two ratchets are red on the train on purpose. I checked them the way `feature_check.py` does: each REGRESSION line's key (its own `key()`/`item_file()` helpers) against the cached base listing `gate-baselines/ded355440871a86cc907cdc2d6465f2d3d597a6b.json`, limited to my changed file. Complexity has 6 REGRESSION lines and file-length has 15. None is a new item in a changed file. The file-length item for `tests/src/core/NotePlaybackTest.cpp` is already in the base listing, so it is not new. No new function is over CCN 10: the new functions are the two small helpers and the two test slots, none of them complex.

## Self-check

1. **RED fails and GREEN passes, with exit codes.** Yes. Fix reverted: test EXIT 8, both new tests fail (0 vs 2 and 0 vs 1). Fix applied: test EXIT 0, 25 passed. Volatile-only revert: test EXIT 8. Full table above.
2. **Deterministic, no timing or races.** Yes. No clock, sleep, or thread is involved. The test runs on one thread, and the counters are `thread_local`. Each `new` expression makes exactly one replaceable call, so the expected counts are exact (2 and 1). The counts were checked at `-O2` on this build (RelWithDebInfo).
3. **Fix at the cause, and siblings checked.** The cause is the counter's allocator in this test: the missing aligned overloads (`:325-342`) and the internal-linkage counters (`:80-81`). Siblings:
   - `tests/src/core/AllocationProbe.cpp:51-128` already has every aligned overload, and its counters live in `namespace lmms::test` (`:40-41`), so the optimiser issue does not arise there. Not changed.
   - `tests/src/core/RoutingGraphTest.cpp:52` replaces only the plain `operator new`, the same gap in another binary. It is not part of this cluster, so I left it alone and report it for a separate cluster.
   - Over-aligned allocations inside this binary: the `renderMonstroVoiceSetConcurrent` helper in this file now goes through `allocateAligned` and is freed by the matching aligned delete. `monstroConcurrentVoicesKeepIndependentModulation` passes (`test-17-final-GREEN.log`).
4. **No collateral damage.** Yes. The change is test-only, in the `NotePlaybackTest` binary. No product code, saved project data or load path is touched. Counting `monstroNoteStartDoesNotAllocate` is now a real check: it reads 0 with the counter working. The positive controls show the counter sees plain allocations.
5. **Verdict FIXED**, not ALREADY-FIXED or FALSE-POSITIVE, so no separate proof is needed for those.

Other checks: the audio path, undo and commands are unchanged. The fix adds no logging, locking or allocation to any product path, and no control command, so `ControlCommandsSnapshot` does not move. The test file is already listed in `tests/fork-sources.txt:2377`; no new file and no upstream-modifications entry are needed.

## Not verified, or worked around

- `feature_check.py` could not write its logs (see Gates). Each of its gate commands was run directly and the ratchet comparison was reproduced. The wrapper itself did not produce a PASS table.
- To load the real `monstro` instrument, the `monstro` plugin target had to be built into the worktree's `build/` (`build/plugins/libmonstro.so`). That is a build artifact, not a source change.
- Build commands needed `CCACHE_TEMPDIR` pointed at `/tmp` and `MAKEFLAGS` unset, because the inherited jobserver fifo and the `/run/user` ccache directory are not writable from this session.
- Only the `NotePlaybackTest` family was run. The full `ctest` was not run.
