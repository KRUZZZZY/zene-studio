FIXED

# cluster ci-aa184f959f204bcd - linux-x86_64 build: `-Werror=unused-result` on the mount-namespace helper

## Verdict and evidence

REAL, product-side test-infrastructure defect (a fork-NEW test helper), reproduced locally.

The hosted `linux-x86_64` job stopped with

```
tests/src/core/MountNamespaceTestSupport.h:110:22: error: ignoring return value of
'ssize_t write(int, const void*, size_t)' declared with attribute 'warn_unused_result'
[-Werror=unused-result]
tests/src/core/MountNamespaceTestSupport.h:78:22: error: ... same at (void)::write(writeEnd, ...)
```

Code at the base commit 40b4322860:

- `tests/src/core/MountNamespaceTestSupport.h:78` - `(void)::write(writeEnd, &done, 1);`
- `tests/src/core/MountNamespaceTestSupport.h:110` - `(void)::write(toChild[1], &go, 1);`

Why it fails there and not here:

- `/usr/include/unistd.h:378` declares `write` with `__wur`; `/usr/include/sys/cdefs.h:578-581`
  defines `__wur` as `__attribute__((warn_unused_result))` only when `__USE_FORTIFY_LEVEL > 0`.
- Ubuntu 24.04's gcc turns on `_FORTIFY_SOURCE=3` by default at `-O2`; the job builds
  `RelWithDebInfo` (`-O2`) and passes `-DUSE_WERROR=ON` (`.github/workflows/build.yml:79`,
  `cmake/modules/ErrorFlags.cmake:42-46`), so `-Wall`'s `-Wunused-result` is an error.
- A `(void)` cast is not a use; only consuming the result silences `warn_unused_result`.
- This box's build tree was configured with `-Wno-error=unused-result` in `CMAKE_CXX_FLAGS`
  (`build/CMakeCache.txt:82`), which is exactly why the same header compiles locally and the
  defect only appears on the hosted job.

Reproduced locally against the REAL header with the TU's own recorded flags plus hosted
hardening (`-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3 -Werror=unused-result`): the two errors are
byte-for-byte the hosted ones (see `build/qwen-logs/red-check-1.log` and `red-direct-3.log`).

No other first-party C/C++ file casts away a `__wur` result: a scan of `tests/`, `include/`,
`src/`, `plugins/` (vendored trees excluded) for `(void)::read/write/pread/pwrite/readlink/
realpath/system/remove/dup/dup2(...)` and for bare statement-position calls returns nothing.
The two sites in this header are the whole class.

## Fix (at the cause)

`tests/src/core/MountNamespaceTestSupport.h` now USES both write results:

- `mapOwnUidIntoNamespace` returns `written && reported`, where
  `reported = ::write(writeEnd, &done, 1) == 1`.
- `enterOwnMountNamespace` computes `signalled = ::write(toChild[1], &go, 1) == 1` and returns
  `unshared && signalled && answered && done == 1`.

Semantics are unchanged, only honest: if the handshake byte is not written, the peer reads
EOF/0 instead of 1 and the function already answered "no namespace"; the result is now
checked rather than assumed. No caller, limit, save format, command, undo path or audio-thread
path is touched (this is a test helper compiled only into `RecordToolsTest`; the only includer
is `tests/src/core/RecordToolsTest.cpp:52`, verified with grep over `tests include src plugins`).

## Regression test

`tests/unused-result-fortify-check.py` (registered as ctest `UnusedResultFortifyCheck` in
`tests/CMakeLists.txt:9011-9031`).

- Reads `build/compile_commands.json`, takes the recorded command of
  `tests/src/core/RecordToolsTest.cpp`, drops that build's `-Wno-error=unused-result`,
  `-Werror`, `-o`/`-c` and any `-D_FORTIFY_SOURCE`, then compiles a probe that CALLS every
  function of the real header with `-D_FORTIFY_SOURCE=3 -Werror=unused-result`.
- Two fixtures keep it honest: a discarded `write` result must be rejected (else the script
  FAILS - a green run would prove nothing), and a used one must compile (else the flags are
  too strict).
- Deterministic: no timing, sleeps, network or fork/exec of the product; it is a single
  compiler invocation over fixed source with injected flags. On a non-Linux host, where the
  header's `#ifdef __linux__` block does not exist, it prints SKIP and exits 0.
- The probe's first draft merely named the helpers; the compiler does not gimplify an inline
  function nobody calls, so it wrongly passed with the fix reverted. The probe now calls them
  from `main`; the RED run below is against that corrected probe.

## Exact commands and exit codes

Logs are under `build/qwen-logs/` (gitignored, inside the worktree). The prescribed LOG FOLDER
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/ci-aa184f959f204bcd.r1/logs` is
root-owned `drwxr-xr-x` and NOT writable by uid 1000 in this sandbox (`touch` -> EACCES;
`rmdir`/recreate -> EPERM), so every log was written to `build/qwen-logs/` instead.

```
# RED proof: revert only the product file, run the registered test
git diff -- tests/src/core/MountNamespaceTestSupport.h > build/qwen-logs/fix.patch   # EXIT=0
git apply -R build/qwen-logs/fix.patch                                              # EXIT=0
cd build/tests && ctest -R UnusedResultFortifyCheck --output-on-failure \
  > ../qwen-logs/ctest-red-2.log 2>&1; echo EXIT=$?                                  # EXIT=8
#   0% tests passed, 1 tests failed out of 1
#   MountNamespaceTestSupport.h:110:22: error: ... [-Werror=unused-result]
#   MountNamespaceTestSupport.h:78:22:  error: ... [-Werror=unused-result]

# GREEN: restore the fix, rebuild the affected target, run the same test
git apply build/qwen-logs/fix.patch                                                 # EXIT=0
cmake --build build --target RecordToolsTest -j20 \
  > build/qwen-logs/build-4-final.log 2>&1; echo EXIT=$?                            # EXIT=0
cd build/tests && ctest -R UnusedResultFortifyCheck --output-on-failure \
  > ../qwen-logs/ctest-green-2.log 2>&1; echo EXIT=$?                               # EXIT=0
#   100% tests passed, 0 tests failed out of 1

# Family regression (RecordToolsTest + the three sibling hosted-portability checks)
cd build/tests && ctest -R \
 '^(RecordToolsTest|UnusedResultFortifyCheck|Qt5SignalOverloadCheck|Qt5SignalOverloadArmCheck|ArrangerZoomNoGestures)$' \
 --output-on-failure > ../qwen-logs/ctest-family-1.log 2>&1; echo EXIT=$?           # EXIT=0
#   100% tests passed, 0 tests failed out of 5

# Gates
python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> \
  > build/qwen-logs/feature-check-1.log 2>&1; echo EXIT=$?                          # EXIT=0
#   PASS fork-sources / all-sources-reproduce / no-upstream-regression / check-namespace /
#        no-tautology / doc-figures / complexity (no new item) / file-length (no new item)

# Pre-submit
python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> ci-aa184f959f204bcd
```

Measured complexity of the new script (`python3 -m lizard tests/unused-result-fortify-check.py
-C 10`): 6 functions, max CCN 8, 214 lines - no threshold exceeded, so the fork-scoped
ratchets measure it at their existing thresholds. It is registered in `tests/fork-sources.txt`
(entry plus this report) by the same rule as the Train 29 `tests/*.py` scanners.

## Files committed

- `tests/src/core/MountNamespaceTestSupport.h` - use both `write` results (the fix)
- `tests/unused-result-fortify-check.py` - the regression probe
- `tests/CMakeLists.txt` - ctest registration `UnusedResultFortifyCheck`
- `tests/fork-sources.txt` - registration of the probe and this report
- `docs/qwen-fixes-2026-10/cluster-ci-aa184f959f204bcd.md` - this report

## Self-check

- **Regression FAILS with the product change reverted, PASSES with it.**
  RED: `git apply -R build/qwen-logs/fix.patch` (EXIT=0), `ctest -R UnusedResultFortifyCheck`
  -> EXIT=8, one test failed, with the hosted `-Werror=unused-result` errors at lines 78 and
  110 (`build/qwen-logs/ctest-red-2.log`). GREEN: `git apply build/qwen-logs/fix.patch`
  (EXIT=0), `cmake --build build --target RecordToolsTest` (EXIT=0),
  `ctest -R UnusedResultFortifyCheck` -> EXIT=0, 1/1 passed (`ctest-green-2.log`).
- **Deterministic.** No wall-clock timing, sleeps, threads or races: the seam is the build's
  own recorded compile command plus injected `-D_FORTIFY_SOURCE=3 -Werror=unused-result`
  (`compile_only_arguments()` / `FORTIFY` in the script), and the checked source is fixed.
  The two fixtures pin the flags' sensitivity and fairness.
- **Fix at the cause and complete.** Both `(void)::write` sites are fixed (lines 78 and 110 at
  the base). The only includer of the header is `RecordToolsTest.cpp`; `RecordToolsTest` was
  rebuilt (EXIT=0) and passes (EXIT=0). A whole-tree scan found no other first-party
  cast-away `__wur` call. A sibling write at line 75 already compared its result and is
  unchanged.
- **No collateral damage.** The handshake's success/failure answers are unchanged for every
  reachable path (a write that fails yields the same "no namespace" answer as before, now by
  construction rather than by assumption). No save format, limit, command, undo step or
  audio-thread code is touched; `RecordToolsTest` (including
  `armedRoutesOnOneFilesystemUnderTwoMountPointsShareItsFreeSpace`, the case that uses the
  namespace) passes 1/1 in `ctest-recordtools-green.log`.
- **ALREADY-FIXED / FALSE-POSITIVE.** Not claimed; the defect is present at the reported
  commit and reproduced locally.
