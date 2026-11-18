# Cluster new-p10-336-port: RNNoise harness reports success after a short output write

Verdict: **FIXED** (port of `bugs/qwen-new-p10-336`, commit `ea27afab6`, onto `bugs/qwen-new-p10-336-port`).

The newest line did not already fix this. On the pre-fix tree the output write ignored `fwrite`'s return value, `ferror()` and the `fclose()` result, so a failed write still exited 0.

## Evidence

- Pre-fix `plugins/RnnoiseDenoiser/testdata/rnn_harness.c:105-108` (HEAD `4eb8d6e8a`): `fopen` result checked, then `fwrite(out, sizeof(float), (size_t)n, fo);` and `fclose(fo);` with no checks. Exit code is 0 whatever the write did.
- Fixed `plugins/RnnoiseDenoiser/testdata/rnn_harness.c:69-83`: new `write_output()` checks the `fwrite` count (`:74`), `ferror()` (`:74`) and the `fclose()` result (`:75`), prints `output write failed` and returns 2 on failure.
- Fixed `plugins/RnnoiseDenoiser/testdata/rnn_harness.c:123`: `main` calls `write_output` and returns 2 on failure. `main`'s CCN stays at 14, under the grandfathered baseline of 15 (`tests/complexity-baseline.tsv`).
- The `fclose` check matters: with a small buffered write to `/dev/full`, `fwrite` reports success and the error only appears when the buffer is flushed at close.
- Regression test `tests/src/plugins/RnnoiseHarnessOutputTest.cpp:55-74` runs the real CLI with `/dev/full` as output and asserts a non-zero exit code. `:76-92` asserts a normal output file of one float is written with exit 0. It is `QSKIP`ped only when `/dev/full` does not exist.
- Build wiring `tests/CMakeLists.txt:1800-1837`: builds `RnnoiseHarnessCli` from `testdata/rnn_harness.c` and links `rnnoise_vendored` (defined by `plugins/RnnoiseDenoiser/CMakeLists.txt:27`). If that target does not exist, a private library is built from the same vendored sources.
- Registration: `tests/fork-sources.txt` (new test, after `NeuralAmpProfileArgumentsTest.cpp`), `tests/all-sources.txt` (after `ReverbScAllocationTest.cpp`), and `tests/upstream-modifications.txt` (last line, reason for the `tests/CMakeLists.txt` change). `rnn_harness.c` is already a fork-owned file in `tests/fork-sources.txt:1204`, so it needs no upstream-modifications entry.

## RED / GREEN and gates

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-336-port.r1/logs`. All commands were run from the worktree root unless noted.

| Step | Exact command | Exit | Result |
|---|---|---:|---|
| Build attempt 1 (not a code failure) | `cmake --build build --target zene RnnoiseHarnessOutputTest -j2 > .../build-1.log 2>&1; echo EXIT=$?` | 2 | `ccache: failed to create temporary file for /run/user/1000/ccache-tmp/...: Permission denied`. The write-jail blocks `/run/user`. |
| Build attempt 2 (`ccache` temp dir moved to `/tmp`) | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p10336 cmake --build build --target zene RnnoiseHarnessOutputTest -j2 > .../build-2.log 2>&1; echo EXIT=$?` | 0 | Built `zene` and the test (fix not yet applied). |
| **RED** (unfixed harness) | `cd build/tests && ctest -R RnnoiseHarnessOutputTest --output-on-failure > .../test-1.log 2>&1; echo EXIT=$?` | **8** | `FAIL! : RnnoiseHarnessOutputTest::reportsOutputWriteFailure() 'harness.exitCode() != 0' returned FALSE` (`test-1.log:7-8`). `0% tests passed, 1 tests failed out of 1`. |
| Fix applied; build | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p10336 cmake --build build --target zene RnnoiseHarnessOutputTest -j2 > .../build-3.log 2>&1; echo BUILD_EXIT=$?` | 0 | Rebuilt `zene`, `RnnoiseHarnessCli` and the test. |
| **GREEN** (fixed harness) | `cd build/tests && ctest -R RnnoiseHarnessOutputTest --output-on-failure > .../test-2.log 2>&1; echo TEST_EXIT=$?` | **0** | `100% tests passed out of 1`. |
| Proof: fix removed | `git diff -- plugins/RnnoiseDenoiser/testdata/rnn_harness.c > .../fix.patch && git apply -R .../fix.patch; echo REVERSE_EXIT=$?` | 0 | Reverse applied (38-line product patch). |
| Proof: rebuild without fix | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p10336 cmake --build build --target zene RnnoiseHarnessOutputTest -j2 > .../build-4.log 2>&1; echo BUILD_EXIT=$?` | 0 | |
| **RED proof** (fix removed) | `cd build/tests && ctest -R RnnoiseHarnessOutputTest --output-on-failure > .../test-3.log 2>&1; echo TEST_EXIT=$?` | **8** | Same assertion failed (`test-3.log:7`). |
| Proof: fix reapplied | `git apply .../fix.patch; echo APPLY_EXIT=$?` | 0 | |
| Proof: rebuild with fix | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p10336 cmake --build build --target zene RnnoiseHarnessOutputTest -j2 > .../build-5.log 2>&1; echo BUILD_EXIT=$?` | 0 | |
| **GREEN proof** (fix reapplied) | `cd build/tests && ctest -R RnnoiseHarnessOutputTest --output-on-failure > .../test-4.log 2>&1; echo TEST_EXIT=$?` | **0** | `100% tests passed out of 1`. |
| Direct binary, QTest totals | `cd build/tests && ./RnnoiseHarnessOutputTest > .../qtest-green.log 2>&1; echo EXIT=$?` | 0 | `Totals: 4 passed, 0 failed, 0 skipped` (`initTestCase`, `reportsOutputWriteFailure`, `writesOutputFileSuccessfully`, `cleanupTestCase`). |

**Affected family (`ctest -R RnnoiseHarnessOutputTest`): 1 test executed, 1 passed.** Non-zero, so not a zero-test run.

### Ratchet gates

| Gate | Exact command | Exit (this branch) | Exit (clean HEAD export, `/tmp`) |
|---|---|---:|---:|
| Complexity | `bash tests/complexity-gate.sh --check > .../gate-1.log 2>&1; echo COMPLEXITY_EXIT=$?` | **1** | 1 (`gate-head-1.log`) |
| File length | `bash tests/file-length-gate.sh --check > .../gate-2.log 2>&1; echo FILELENGTH_EXIT=$?` | **1** | 1 (`gate-head-2.log`) |

Both gates fail on the branch, and they fail the same way on clean HEAD. I did not change any gate, limit or baseline.

- **Complexity (exit 1):** six `REGRESSION` lines (`gate-1.log:97-102`), all in files this diff does not touch (`tests/src/gui/XyPadPairTest.cpp`, `src/core/DawProjectReadTracks.cpp`, `src/core/ControlCommandsMixerTools.cpp`, `src/core/ControlCommandsMixerStrip.cpp`, `src/core/ProjectRevisions.cpp`, `src/gui/ControlCommandsShortcuts.cpp`). This change does not add any function over CCN 10 and does not raise `main`'s CCN above 14 (`gate-1.log:40`).
- **File length (exit 1):** fourteen `REGRESSION` lines (`gate-2.log:24-37`), all in untouched files (`src/core/TakeLane.cpp`, `tests/src/core/*Test.cpp`, `tests/src/gui/ArrangerTimelineTest.cpp`, and others). The new `tests/src/plugins/RnnoiseHarnessOutputTest.cpp` (96 lines) is under the 500-line limit.
- The `REGRESSION` lists match clean HEAD exactly: `diff` of the sorted entries reports no difference for either gate.

I used `git archive HEAD ... | tar -x -C /tmp/p10336-head` to get the clean-HEAD comparison without switching or stashing. I removed that temporary copy afterwards.

### Notes

- `clang-tidy` reports some naming and braces warnings in `rnn_harness.c` (for example `parse_scale`, `open_stats`, `process_frame`, plus the new `write_output`). They follow the file's existing snake_case style and are not gated. I left them as they are.
- `build-1.log` failed on the `ccache` temp dir, not on code. The build was rerun with `CCACHE_TEMPDIR=/tmp/ccache-tmp-p10336`. That is the only build-environment change.

## Files committed

- `plugins/RnnoiseDenoiser/testdata/rnn_harness.c` (fix: `write_output`, checked `fwrite` / `ferror` / `fclose`)
- `tests/src/plugins/RnnoiseHarnessOutputTest.cpp` (new regression test)
- `tests/CMakeLists.txt` (RnnoiseHarnessCli and RnnoiseHarnessOutputTest block)
- `tests/fork-sources.txt`, `tests/all-sources.txt` (registration of the new test)
- `tests/upstream-modifications.txt` (reason line for `tests/CMakeLists.txt`)
- `docs/qwen-fixes-2026-10/cluster-new-p10-336-port.md` (this report)
