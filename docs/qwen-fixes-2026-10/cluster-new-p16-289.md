FALSE-POSITIVE

# Cluster new-p16-289: zero-frame buffers can perform null pointer arithmetic

## Verdict

FALSE-POSITIVE. The finding says `addGroup()` performs invalid pointer arithmetic for zero frames. In C++20 the
only arithmetic there is adding the integer `0` to a possibly-null pointer, and the standard defines that as a null
result, not undefined behaviour. The arithmetic is never followed by a dereference. No product code was changed.

## Evidence

- `src/core/AudioBuffer.cpp:207-215` (the loop the finding describes): `float* ptr = m_sourceBuffer.data();` then, per
  channel, `m_accessBuffer[channel] = ptr; ptr += m_frames;`. With `m_frames == 0` the increment adds `0`.
- `src/core/AudioBuffer.cpp:199-200`: `m_sourceBuffer.resize(sourceBufferSize)` with a size of 0, so `data()` may be null.
- C++20 `[expr.add]` (paragraph 4, first case): if the pointer operand is a null pointer value and the integer operand
  is 0, the result is a null pointer value. This is the carve-out that separates C++ from C. I confirmed the rule from
  secondary sources only: a 2023 ISO C++ Liaison thread quoting Jens Maurer on the `[expr.add]` carve-out
  (https://lists.isocpp.org/liaison/2023/10/1324.php), and GCC bug 60760 noting libstdc++ depends on it
  (https://gcc.gnu.org/bugzilla/show_bug.cgi?id=60760). I did not fetch the current draft text.
- Nothing dereferences the channel pointers when frames are zero. Every accessor returns a view whose length is
  `m_frames` (`include/AudioBuffer.h:195-249`), so a zero-frame view has no elements to read.
- Sanitizer probe of the exact operation (`logs/zero_null_probe.cpp`): a `std::pmr::vector<float>` resized to 0, then
  `ptr += 0` on its null `data()`:
  - `clang++ 23.1.4 -std=c++20 -fsanitize=undefined,pointer-overflow -fno-sanitize-recover=all` → build EXIT=0, run EXIT=0
  - `g++ 16.2.1 -std=c++20 -fsanitize=undefined -fno-sanitize-recover=all` → build EXIT=0, run EXIT=0
  - Both print `data=(nil) out0=(nil)`. No sanitizer report.
- Through the real class: `AudioBuffer{0}` (default channels) builds one stereo group with `frames() == 0`
  (`tests/src/core/AudioBufferTest.cpp:248-257`, the new test below). It passes on the unchanged product code.

Not checked: a separate question is whether callers pass a zero-frame channel's null `data()` to `memcpy`/`memset` with
length 0. That is a different pattern from the one in this finding and I did not audit it.

## Change made

- `tests/src/core/AudioBufferTest.cpp:248-257`: new slot `Constructor_ZeroFramesDefaultChannels` pins the zero-frame
  default-channel construction contract (1 group, 2 channels, 0 frames, no signal). It is a guard, not a regression
  proof: it passes on the unchanged code, which is the expected result for a behaviour that already holds.
- `tests/upstream-modifications.txt` (last line): registration of that test-file change with its reason.
- No product file changed. A "skip the arithmetic when frames is 0" branch would be a no-op, so I did not add it.

## Commands and exit codes

| # | Command (run from the worktree unless noted) | EXIT | Log |
|---|---|---|---|
| 1 | `cmake --build build --target AudioBufferTest -j4` | 2 | `build-1.log` (ccache could not write its temp files under `~/.local/share/zene-bunny-fix/dsh`, outside the write-jail; not a code error) |
| 2 | `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp cmake --build build --target AudioBufferTest -j4` | 0 | `build-2.log` |
| 3 | `cd build/tests && ctest -R '^AudioBufferTest$' --output-on-failure` | 0 | `test-2.log` (`1/1 Test #7: AudioBufferTest ... Passed`) |
| 4 | `cd build/tests && ./AudioBufferTest Constructor_ZeroFramesDefaultChannels -v2` | 0 | `test-3-zeroframes-green.log` (`3 passed, 0 failed`) |
| 5 | `cd build/tests && ./AudioBufferTest` | 0 | `test-4-full-binary.log` (`Totals: 44 passed, 0 failed, 0 skipped`) |
| 6 | sanitizer probe: `clang++ -std=c++20 -fsanitize=undefined,pointer-overflow ...` then run | 0 / 0 | `probe-clang-build.log`, `probe-clang-run.log` |
| 7 | sanitizer probe: `g++ -std=c++20 -fsanitize=undefined ...` then run | 0 / 0 | `probe-gcc-build.log`, `probe-gcc-run.log` |

RED/GREEN (step 4b) does not apply: there is no product change to revert. The new test is green on both sides by
construction, so it does not count as RED evidence.

Affected test family: `AudioBufferTest` (1 ctest entry, 44 QTest functions executed). `SidechainInputsTest` and
`RnnoiseDenoiserEffectTest` were not rebuilt or run: no product code changed.

### Gates (`feature_check.py`)

`python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` → EXIT=1, but it did not reach the gates.
Line 71 of that script calls `mkdir` on `<worktree>/../zene-qwen-new-p16-289-feature-check`, which is outside the
write-jail, and fails with `PermissionError` (`gates-1.log`). I did not change the script.

I ran the same commands it runs, logs in the LOG folder:

| Gate | Command | EXIT | Log |
|---|---|---|---|
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 | `gate-1.log` |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 | `gate-2.log` |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 | `gate-3.log` |
| check-namespace | `tests/scripted/check-namespace` | 0 | `gate-4.log` |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 | `gate-5.log` |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 | `gate-6.log` |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 | `gate-7.log` (6 REGRESSION lines) |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 | `gate-8.log` (23 REGRESSION lines) |

The two ratchet gates are red on the train by design. By the script's own rule, a ratchet row fails only for a
REGRESSION line that names a file this branch changed. The only changed product-or-test file is
`tests/src/core/AudioBufferTest.cpp`, and no REGRESSION line in `gate-7.log` or `gate-8.log` names it (`grep`
returned nothing). So both rows should read PASS under `feature_check.py`. I derived this from the script's logic and
did not run the script to completion. The base-listing cache for `37eaf07557` is absent, and the script can only
rebuild it by creating a worktree outside the write-jail.

## Self-check

1. **Regression fails with the change reverted, then passes.** Not applicable: there is no product change to revert.
   The new test passes on the unchanged code (commands 4 and 5), so it is a contract guard, not regression proof.
2. **Deterministic.** Yes. The test uses only construction and accessor calls: no timing, sleeps, threads or I/O.
3. **Fix at the cause and complete.** No fix. Every zero-frame path in `addGroup` (`AudioBuffer.cpp:199-223`) was read:
   the access-table loop, the group re-binding loop and the silence-flag update. None dereferences a channel pointer
   when frames are zero.
4. **No collateral damage.** No product code changed, so no existing project, save format or caller behaviour changes.
5. **Verdict evidence.** `AudioBuffer.cpp:206-215`, `include/AudioBuffer.h:195-249`, `[expr.add]` (secondary sources), and
   the sanitizer probe (commands 6-7).
6. **Audio thread.** Nothing added to any audio path. The test runs only in the test binary.
7. **Old projects load and render as before.** Unchanged: no product code.
8. **Portability.** The test uses only the `QCOMPARE`/`QVERIFY` idioms already in the file.
9. **Sibling commands.** No command files changed, so no sibling check applies.

## Completion check

`python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p16-289` → EXIT=1 (`submit-check-1.log`):
commits on the branch PASS, working tree clean PASS, report verdict PASS, tests added or changed PASS (claim-only
closure), **gates FAIL**. The gates row is the same `feature_check.py` `PermissionError` described above (sibling
log directory outside the write-jail), not a product or test failure. The fix-loop run, or a session with write
access beside the worktree, should re-run that row. The gate commands themselves exited 0 for the six simple gates
and exited 1 for the two ratchet gates, which the report explains.
