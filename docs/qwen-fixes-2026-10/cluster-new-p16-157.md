Verdict: FIXED

new-p16-157 (BUG 14, Opulenz converts invalid frequencies to unsigned F-numbers): REAL, but narrower than the finding says.

Finding vs code:

- The finding's trigger is wrong. `tuneEqual` is only called with the constant 440 Hz and centre 69: [OpulenZ.cpp:173](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-157/plugins/OpulenZ/OpulenZ.cpp:173) and [OpulenZ.cpp:344](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-157/plugins/OpulenZ/OpulenZ.cpp:344). The only varying input is `pitchbend`, an int, so the scaled value cannot be NaN and is never negative.
- The real path is the pitch-bend sensitivity RPN. [OpulenZ.cpp:365](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-157/plugins/OpulenZ/OpulenZ.cpp:365) sets `pitchBendRange = event.controllerValue() * 100`. `controllerValue()` returns `uint8_t` from the raw parameter with no 7-bit mask ([MidiEvent.h:163](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-157/include/MidiEvent.h:163)-166), in contrast to `key()` and `velocity()`, which mask with `& 0x7F`. A data value of 127 gives at most 12700 cents, which keeps every scaled value below about 4.1e8, inside `unsigned`. Values from about 168 upward (out of MIDI's 7-bit range) push the block-0 value past 2^32, which is undefined behaviour in `static_cast<unsigned>`.
- I did not trace every producer of controller events to prove whether a value above 127 can arrive from a device or file. The `MidiEvent` accessor allows it, and that is what makes the undefined behaviour reachable.
- Observed result: on this toolchain (GCC, -O2) the original returns 0 for the out-of-range samples I tried (3.1e10, 3e8, NaN). The undefined behaviour has no visible symptom here, which is why the regression is structural (see below).

Fix (at the conversion): [OpulenZ.cpp:533-541](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-157/plugins/OpulenZ/OpulenZ.cpp:533). `Hz2fnum` now compares the float `scaled` against `[0, 1023)` and only then converts to `unsigned`. Out-of-range blocks are skipped, and the existing `return 0` fall-through applies, the function's existing sentinel for "no representable F-number". The original's `fnum<1023` check after the cast is equivalent to `scaled < 1023` for non-negative inputs, so every defined input gives the same result. I did not clamp `pitchBendRange` in the RPN handler: that would change behaviour for out-of-spec data beyond the bug.

Equivalence check (scratch program in `$TMPDIR`, deleted afterwards): original and new `Hz2fnum` agree on all 128 notes × every pitch bend from -25500 to +25500 cents plus a 1e-4 geometric sweep of 0.01 Hz to 1e7 Hz: 6,735,337 comparisons, 0 mismatches. Out-of-range samples (3.1e10, 3e8, NaN) return 0 from both.

Regression: `PluginPortsMigrationTest::opulenzHz2fnumChecksRangeBeforeUnsignedCast` (ctest `OpulenZHz2fnumRange`). It is a source-structure test in the same style as the other OpulenZ tests in that file. The result of the undefined conversion is not deterministic across compilers, so a runtime assertion would not be a reliable RED. The test asserts that `Hz2fnum`'s body has a `>= 0.0f` and `< 1023.0f` guard before its only `static_cast<unsigned>`.

Registration: `tests/upstream-modifications.txt` gets a line for `plugins/OpulenZ/OpulenZ.cpp` (cluster new-p16-157). `tests/CMakeLists.txt` gets the `OpulenZHz2fnumRange` entry.

## Commands and results

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-157.r1/logs/`. Each command was run from the worktree root unless noted.

| Step | Command | Result |
| --- | --- | --- |
| Build attempt 1 (failed, environment) | `cmake --build build --target zene PluginPortsMigrationTest -j4 > .../build-1-red.log 2>&1; echo EXIT=$?` | EXIT=2. ccache could not write to `/home/kruzzzzy/.local/share/zene-bunny-fix/dsh/home/ccache-tmp-*`, outside the write-jail. No code error. |
| Build for RED (ccache disabled, no code change to the product file) | `CCACHE_DISABLE=1 cmake --build build --target zene PluginPortsMigrationTest -j4 > .../build-2-red.log 2>&1; echo EXIT=$?` | EXIT=0 |
| RED | `cd build/tests && ctest -R '^OpulenZHz2fnumRange$' --output-on-failure > .../test-1-red.log 2>&1; echo EXIT=$?` | EXIT=8. 0% passed, 1 test failed. `FAIL! ... beforeCast.contains(">= 0.0f") && beforeCast.contains("< 1023.0f") returned FALSE` |
| Fix applied; build zene and test target | `CCACHE_DISABLE=1 cmake --build build --target zene PluginPortsMigrationTest -j4 > .../build-3-green.log 2>&1; echo EXIT=$?` | EXIT=0 |
| Plugin build | `CCACHE_DISABLE=1 cmake --build build --target opulenz -j4 > .../build-4-plugin.log 2>&1; echo EXIT=$?` | EXIT=0 |
| GREEN | `cd build/tests && ctest -R '^OpulenZHz2fnumRange$' --output-on-failure > .../test-2-green.log 2>&1; echo EXIT=$?` | EXIT=0. 100% passed, 1/1 |
| 4b: save fix | `git diff -- plugins/OpulenZ/OpulenZ.cpp > .../fix.patch; echo DIFF_EXIT=$?` | DIFF_EXIT=0 |
| 4b: revert product fix | `git apply -R .../fix.patch; echo REVERSE_EXIT=$?` | REVERSE_EXIT=0. `git diff -- plugins/OpulenZ/OpulenZ.cpp` then empty |
| 4b: rebuild with fix reverted | `CCACHE_DISABLE=1 cmake --build build --target PluginPortsMigrationTest -j4 > .../build-5-revert.log 2>&1; echo BUILD_EXIT=$?` | BUILD_EXIT=0 |
| 4b: RED with fix reverted | `cd build/tests && ctest -R '^OpulenZHz2fnumRange$' --output-on-failure > .../test-3-revert-red.log 2>&1; echo TEST_EXIT=$?` | TEST_EXIT=8. Same `FAIL!` as above |
| 4b: re-apply fix | `git apply .../fix.patch; echo APPLY_EXIT=$?` | APPLY_EXIT=0 |
| 4b: rebuild with fix | `CCACHE_DISABLE=1 cmake --build build --target PluginPortsMigrationTest opulenz -j4 > .../build-6-reapply.log 2>&1; echo BUILD_EXIT=$?` | BUILD_EXIT=0 |
| 4b: GREEN with fix | `cd build/tests && ctest -R '^OpulenZHz2fnumRange$' --output-on-failure > .../test-4-reapply-green.log 2>&1; echo TEST_EXIT=$?` | TEST_EXIT=0. 100% passed |
| Family | `cd build/tests && ctest -R 'OpulenZ' --output-on-failure > .../test-5-family.log 2>&1; echo EXIT=$?` | EXIT=0. 4/4 passed: OpulenZ, OpulenZSpanSize, OpulenZZeroFrameCount, OpulenZHz2fnumRange |

Affected-family test count: 4 executed, 4 passed.

## Gates

`feature_check.py` could not run in this session. It writes its logs to `<worktree>-feature-check/` and a baseline cache under `~/.local/share/zene-bunny-fix/qwen/`, both outside the write-jail. It fails at the first `mkdir` with `PermissionError`. I ran the same commands it runs, from the worktree, logging to the run folder:

| Gate | Command | Exit | Note |
| --- | --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 | |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 | |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 | |
| check-namespace | `tests/scripted/check-namespace` | 0 | |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 | |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 | |
| complexity | `bash tests/complexity-gate.sh --check` | 1 | Ratchet, red on the train. 6 REGRESSION lines, none in the four changed files. The baseline comparison that `feature_check.py` does was not computed. |
| file-length | `bash tests/file-length-gate.sh --check` | 1 | Ratchet, red on the train. 23 REGRESSION lines, none in the four changed files. The same caveat applies. |

Changed files: `plugins/OpulenZ/OpulenZ.cpp`, `tests/CMakeLists.txt`, `tests/src/plugins/PluginPortsMigrationTest.cpp`, `tests/upstream-modifications.txt`, and this report. None of the ratchet items is in those files.

## Self-check

- **Test fails with the product change reverted, passes with it:** yes. Reverted: `test-1-red.log` EXIT=8 (the RED run, before any fix) and `test-3-revert-red.log` TEST_EXIT=8 after `git apply -R`. Re-applied: `test-2-green.log` EXIT=0 and `test-4-reapply-green.log` TEST_EXIT=0. Family `test-5-family.log` EXIT=0, 4/4.
- **Deterministic, no timing, sleeps or races:** yes. The test reads `plugins/OpulenZ/OpulenZ.cpp` through `functionBody()` (the existing helper in `PluginPortsMigrationTest.cpp`) and checks the source order of a guard and a cast. It uses no clock, thread or injected value.
- **Fix at the cause, every caller and sibling checked:** `Hz2fnum` (declared `OpulenZ.h:143`) has one definition (`OpulenZ.cpp:533`) and one caller, `tuneEqual` (`OpulenZ.cpp:528`), which is called from the constructor (`:173`) and the pitch-bend handler (`:344`). The file has one `static_cast<unsigned>`, and it is the one fixed. The cause of the large input is the unmasked `controllerValue()` in the RPN handler (`:365`). I did not change that, so out-of-spec RPN data still produces a large bend. The conversion is now defined for every input, and such a value returns the existing sentinel `0`. Clamping the RPN value would change behaviour for out-of-spec data beyond this bug, so it is left for a separate decision.
- **No collateral damage:** the change affects no saved data. For every defined input the result is identical (scratch comparison, 6,735,337 cases, 0 mismatches). The only limit used is the existing `1023` check, moved in front of the cast. It is not a new limit.
- **Verdict is FIXED, not ALREADY-FIXED or FALSE-POSITIVE:** this item does not apply. The reachable case is narrower than the finding's trigger (see the top of this report).
- **Gates item (`feature_check.py`) FAILS in `submit_check.py`:** environment limit, not a code failure. `feature_check.py` writes to `<worktree>-feature-check/` and a baseline cache under `~/.local/share/zene-bunny-fix/qwen/`, both outside the write-jail. It stops at `PermissionError`. The same six simple gates were run directly and exit 0. The two ratchet gates exit 1 on the train, with no item in the changed files. The baseline comparison was not computed, so this report cannot claim the gate table is PASS.
