FIXED — hosted linux-arm64 build failure in `tests/src/core/MountNamespaceTestSupport.h`.

## Finding and decision

REAL. The hosted `linux-arm64` build stopped at `MountNamespaceTestSupport.h:78`
and `:110`:

```
error: ignoring return value of 'ssize_t write(int, const void*, size_t)'
       declared with attribute 'warn_unused_result' [-Werror=unused-result]
```

Evidence: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/train-ci/40b4322860e9c2133f412a4b921b63b057dd710c/run-43569206372-job-132369662657.log`
(lines for `RecordToolsTest.cpp.o`, exit 2). Classification: a TEST-SUPPORT
source defect, not a product runtime bug and not infrastructure. The header is
compiled into `RecordToolsTest`, so it breaks the product build.

Cause: the helper discarded the results of two handshake writes with
`(void)::write(...)`. Under `_FORTIFY_SOURCE`, glibc declares `write()` with
`warn_unused_result`, and GCC does not suppress that diagnostic for a void cast
of the fortified call. The same header compiles locally only because this
build's cache carries the workaround `-Wno-error=unused-result`; the check below
sets the hosted flags explicitly, so it does not depend on that cache.

## Fix

Both writes are now checked at the cause (file:line after the fix):

- `tests/src/core/MountNamespaceTestSupport.h:78` — `const bool notified =
  ::write(writeEnd, &done, 1) == 1;` and `return written && notified;` (the
  handshake only succeeds when the uid map was written AND the notification was
  delivered).
- `tests/src/core/MountNamespaceTestSupport.h:110` — `const bool released =
  ::write(toChild[1], &go, 1) == 1;` and line 117 returns
  `unshared && released && answered && done == 1` (a failed release byte is an
  incomplete handshake, never a silent success).

The `read`, `close`, `waitpid`, `mount` and `stat` calls in the same helper were
checked: their results were already handled or none is declared
`warn_unused_result`. `git grep` over all tracked C/C++ found no other discarded
`write()` result. `RecordToolsTest` is the only consumer of the header.

## Regression test

New mode `unused-result` in `tests/train-39-portability-check.py`, registered as
`Train39-unused-result` in `tests/CMakeLists.txt:9056` (GNU/Linux only, because
the diagnostic requires GCC's fortify behaviour; the local Clang control accepts
`(void)::write`, so Clang is not a platform where this can fail).

The probe is deterministic and compile-only. It uses the fixed configuration
`-std=c++20 -O2 -D_FORTIFY_SOURCE=3 -Werror=unused-result -c` and:

1. a discarded-write control that MUST be rejected (proves the probe can fail);
2. a checked-write control that MUST compile (proves the flags do not reject
   `write()` itself);
3. a translation unit that includes the real header and calls both helpers,
   compiled with the real Qt Core include directories — this must compile.

Test matrix: normal path = real header compiles and `RecordToolsTest` passes;
failure path = discarded write rejected by the control; reconfiguration = a
failed handshake write now returns `false` instead of an unchecked call;
audio-thread = not applicable (test-only helper, used before the test starts any
threads); supported platforms = the Linux body is unchanged semantically and the
non-Linux stub is untouched.

## Commands run (exit codes copied from the logs)

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/ci-0b372147c313d00a.r1/logs`

RED before the fix, `build/tests`:

```sh
ctest -R Train39-unused-result --output-on-failure > LOGS/test-red-1.log 2>&1
# EXIT=8: 1/1 failed; the probe reproduces header errors at :78 and :110
```

Rebuild after the fix:

```sh
cmake --build build --target RecordToolsTest -j12 > LOGS/build-2.log 2>&1
# EXIT=0
ctest -R 'Train39-unused-result|RecordToolsTest' --output-on-failure > LOGS/test-green-1.log 2>&1
# EXIT=0: 2/2 passed (RecordToolsTest, Train39-unused-result)
ctest -R '^Train39' --output-on-failure > LOGS/test-train39-family.log 2>&1
# EXIT=0: 6/6 passed (vst2, disk, rnnoise, sfz, unused-result, qt5)
```

Step 4b, product change reverted:

```sh
git diff -- tests/src/core/MountNamespaceTestSupport.h > LOGS/fix.patch   # 1497 bytes
git apply -R LOGS/fix.patch                                              # EXIT=0
cmake --build build --target RecordToolsTest -j12 > LOGS/build-3-reversed.log 2>&1  # EXIT=0
ctest -R Train39-unused-result --output-on-failure > LOGS/test-red-2.log 2>&1       # EXIT=8
# FAIL at MountNamespaceTestSupport.h:78 and :110
git apply LOGS/fix.patch                                                # EXIT=0
cmake --build build --target RecordToolsTest -j12 > LOGS/build-4-reapplied.log 2>&1  # EXIT=0
ctest -R Train39-unused-result --output-on-failure > LOGS/test-green-2.log 2>&1      # EXIT=0
```

Fallback path without CMake-supplied Qt includes (pkg-config):
`python3 tests/train-39-portability-check.py unused-result --cxx /usr/bin/c++ > LOGS/test-fallback.log 2>&1` — EXIT=0.

Gates before commit (`feature-check-2.log`):

```sh
python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > LOGS/feature-check-2.log 2>&1
# EXIT=0; PASS fork-sources, all-sources-reproduce, no-upstream-regression,
# check-namespace, no-tautology, doc-figures, complexity, file-length
```

## Files committed

- `tests/src/core/MountNamespaceTestSupport.h` (fix)
- `tests/train-39-portability-check.py` (regression probe)
- `tests/CMakeLists.txt` (register `Train39-unused-result`)
- `tests/upstream-modifications.txt` (ledger entry for the inherited CMake test list)
- `docs/qwen-fixes-2026-10/cluster-ci-0b372147c313d00a.md` (this report)

No product behaviour, saved data, project format, command surface, undo or
audio-thread path changes. The header is test-only and both new branches only
turn an unchecked failure into an honest `false` handshake result.

## Self-check

- **Regression FAILS reverted / PASSES restored:** reverted product hunk — rebuild EXIT=0, `ctest -R Train39-unused-result` EXIT=8 with errors at :78/:110 (`test-red-2.log`); restored — rebuild EXIT=0, same ctest EXIT=0 (`test-green-2.log`).
- **Deterministic, no timing/sleeps/races:** the probe is compile-only with fixed flags `-O2 -D_FORTIFY_SOURCE=3 -Werror=unused-result`; its discarded-write and checked-write controls are both compiled on every run.
- **Cause and completeness:** both write results in the helper are checked (:78, :110/:117); `git grep` over tracked C/C++ found no other discarded `write()`; the only consumer, `RecordToolsTest`, was rebuilt and passed; the whole Train39 family is 6/6.
- **No collateral damage:** the header is Linux-only test support, the non-Linux stub is unchanged, no project/save/load path is touched, no limit or check was added, and `RecordToolsTest` still passes. The CMake test registration runs only under GNU/Linux, where the hosted diagnostic exists.
- **Verdict evidence:** REAL is shown by the untrusted hosted log plus a local reproduction at the identical two lines; the post-fix compile of the real header and the 2/2 and 6/6 ctest runs are recorded above.
- **ALREADY-FIXED / FALSE-POSITIVE evidence item:** not applicable — the verdict is FIXED and the product header hunk is in this commit.
