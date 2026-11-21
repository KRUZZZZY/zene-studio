# Cluster new-p9-460-port: empty or moved-from system semaphores

**Verdict: FIXED** (port of accepted `bugs/qwen-new-p9-460` commit `11b7e3ed8`; the newest line did not already fix it, so ALREADY-FIXED does not apply). The two ratchet gates are **red on the branch for reasons outside this change** (see the gate section); the fix itself is verified.

## What was wrong

`SystemSemaphore` holds its platform implementation in `m_impl`. The default constructor leaves it null, and a move leaves the source's `m_impl` null. The three public methods dereferenced it without checking:

- `src/common/SystemSemaphore.cpp:203` `acquire()` — `m_impl->acquire()`
- `src/common/SystemSemaphore.cpp:204` `release()` — `m_impl->release()`
- `src/common/SystemSemaphore.cpp:205` `tryAcquire()` — `m_impl->tryAcquire(timeoutMs)`

`tryAcquire` was not in the accepted commit (it covered `acquire`/`release` only). It has the identical unchecked dereference, and the finding's title is "crash when used", so it is included here. This is a deliberate extension beyond the accepted port.

## The change

- `src/common/SystemSemaphore.cpp:203-205`: each method returns `false` when `m_impl` is null (`m_impl && m_impl->...`). The non-null path is unchanged, and no allocation, lock or logging was added.
- `tests/src/core/SystemSemaphoreTest.cpp`: three new test functions added to the existing file (the newer line's file, which already holds the `tryAcquire` timeout tests):
  - `defaultConstructedOperationsReturnFalse`
  - `movedFromConstructorOperationsReturnFalse`
  - `movedFromAssignmentOperationsReturnFalse`
  
  Also `#include <utility>` for `std::move`. The existing `tryAcquire` tests are untouched.
- `tests/upstream-modifications.txt:831`: the reason for `src/common/SystemSemaphore.cpp` now records the null guards (it previously said acquire/release were unchanged).
- No new files. `SystemSemaphoreTest.cpp` is already in `tests/CMakeLists.txt:553` and `tests/fork-sources.txt:2559`.

## Evidence in source

- `src/common/SystemSemaphore.cpp:203-205` — guarded methods (after the fix).
- `src/common/SystemSemaphore.cpp:186-196` — default and key constructors set `m_impl`; `:200-201` defaulted move operations (leave source null).
- `tests/src/core/SystemSemaphoreTest.cpp` — new cases after `tryAcquireTakesACountAtOnce`.

## Evidence table

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-460-port.r1/logs`. All ctest runs from `build/tests`. `CCACHE_TEMPDIR=/tmp/ccache-tmp-qwen460` was needed because ccache could not write to `/run/user/1000` (build-1 failed with that, EXIT=2; build-2 onward exit 0).

| Check | Exact command | EXIT / result |
| --- | --- | --- |
| RED, per function (unfixed code, the test file already in place) | `./SystemSemaphoreTest <fn>` for each new fn; log `test-2-red-per-function.log` | `defaultConstructedOperationsReturnFalse` **139** (SIGSEGV); `movedFromConstructorOperationsReturnFalse` **139**; `movedFromAssignmentOperationsReturnFalse` **139**; the two existing `tryAcquire*` **0** |
| RED, ctest (unfixed code) | `ctest -R '^SystemSemaphoreTest$' --output-on-failure > test-1-red.log 2>&1; echo EXIT=$?` | **EXIT=8**, `Received signal 11 (SIGSEGV)`, 0% passed |
| Reversed-fix proof | `git diff -- src/common/SystemSemaphore.cpp > fix.patch && git apply -R fix.patch` (EXIT=0); `cmake --build . --target zene SystemSemaphoreTest -j2 > build-4-red-reverted.log` (EXIT=0); `ctest -R ... > test-3-red-reverted.log` | ctest **EXIT=8**, SIGSEGV |
| Restored-fix | `git apply fix.patch` (EXIT=0); `cmake --build . --target zene SystemSemaphoreTest -j2 > build-5-green.log` | build **EXIT=0** |
| GREEN, ctest | `ctest -R '^SystemSemaphoreTest$' --output-on-failure > test-4-green.log 2>&1; echo EXIT=$?` | **EXIT=0**, 100% passed, 1 CTest target executed |
| GREEN, per function | `./SystemSemaphoreTest <fn>` for each of the five functions; log `test-5-green-per-function.log` | all five **EXIT=0** |
| GREEN, full binary | `./SystemSemaphoreTest > test-6-green-full-binary.log` | **EXIT=0**, `Totals: 7 passed, 0 failed` |
| Affected family | `ctest -R '^(RemotePluginClientE2ETest\|RemotePluginAudioPortsTest\|SystemSemaphoreTest)$' --output-on-failure > test-7-family.log` | **EXIT=0**, 3/3 passed (SystemSemaphoreTest 0.22 s; RemotePluginAudioPortsTest 31.78 s; RemotePluginClientE2ETest 11.81 s). Both remote tests were built first (`build-6-remote-family.log`, EXIT=0). `SystemSemaphore` is reached through `include/RemotePluginBase.h`. |
| zene build | `cmake --build . --target zene -j2` (build-3, build-5) | EXIT=0 |
| Complexity gate (final) | `bash tests/complexity-gate.sh --check > gate-complexity-2.log 2>&1; echo EXIT=$?` | **EXIT=1** — see below |
| File-length gate (final) | `bash tests/file-length-gate.sh --check > gate-filelength-2.log 2>&1; echo EXIT=$?` | **EXIT=1** — see below |

## Gate status (both red, not caused by this change)

- Complexity: `FAIL: complexity ratchet regressed` with 6 `REGRESSION: new function over target` lines, all in files this change does not touch (`tests/src/gui/XyPadPairTest.cpp`, `src/core/DawProjectReadTracks.cpp`, `src/core/ControlCommandsMixerTools.cpp`, `src/core/ControlCommandsMixerStrip.cpp`, `src/core/ProjectRevisions.cpp`, `src/gui/ControlCommandsShortcuts.cpp`).
- File length: `FAIL: file-length ratchet regressed` with a long list of `REGRESSION: new file over 500 lines` entries, none of them in this diff.
- `grep -c SystemSemaphore` on both final gate logs returns **0**. The changed functions are one-liners (CCN 1 or 2), and the test functions are straight-line QVERIFY sequences, so this change adds no function over CCN 10 and no file over 500 lines.
- I did **not** run the gates on pristine HEAD to measure the baseline, so "pre-existing" rests on the log evidence above, not a clean-tree run. Fixing those 68 functions and 19 files is far outside a one-bug port, and I did not change any limit or baseline.

## Other notes

- The round-0 QA review was not in the prompt, so there were no separate points to address beyond the port.
- clangd flagged an unused `<ctime>` in `SystemSemaphore.cpp` (around line 40). That include predates this change and I did not touch it.

## Commit

Files committed (explicit paths only):

- `src/common/SystemSemaphore.cpp`
- `tests/src/core/SystemSemaphoreTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-460-port.md`
