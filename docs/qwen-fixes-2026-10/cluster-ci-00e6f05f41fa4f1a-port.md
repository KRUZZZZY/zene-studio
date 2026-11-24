ALREADY-FIXED

# Cluster ci-00e6f05f41fa4f1a-port — build / windows-arm64

## Verdict

ALREADY-FIXED. The original failure was real: the Windows-arm64 runner is
clang/mingw, which defines `_WIN32` but not `_MSC_VER`, so
`SfzDistoBuffersTest`'s aligned `operator new` fell through to
`posix_memalign` and the target failed to compile with
`use of undeclared identifier 'posix_memalign'` (CI run 43764792400). The
product change that fixes it (`_MSC_VER` -> `_WIN32` on the include, allocation
and matching free) is already present in this tree and ancestral to HEAD via
`f3b5426267`; the commit named in the diagnostic checkpoint,
`1f1c5192fe`, is not an ancestor, so its regression artifact was missing. This
job ports that artifact and proves it RED/GREEN.

## Code evidence (current tree)

- `tests/src/plugins/SfzDistoBuffersTest.cpp:11` — the `<malloc.h>` include is
  guarded by `#if defined(_WIN32)`.
- `tests/src/plugins/SfzDistoBuffersTest.cpp:59-63` — aligned `operator new`
  calls `_aligned_malloc` under `_WIN32`; `posix_memalign` is the `#else` branch.
- `tests/src/plugins/SfzDistoBuffersTest.cpp:87-91` — aligned `operator delete`
  calls `_aligned_free` under `_WIN32`; `std::free` is the `#else` branch.
- `tests/src/core/AllocationProbe.cpp:31,124,190` — the sibling allocation probe
  already uses the same `_WIN32` contract, so both sites agree.
- `sed -n '/_MSC_VER/p' tests/src/plugins/SfzDistoBuffersTest.cpp` prints
  nothing; the three guards are the only `_WIN32` lines in the file
  (`evidence-03-guard-contract.log`).
- `git merge-base --is-ancestor f3b5426267 HEAD` exits `0`;
  `git merge-base --is-ancestor 1f1c5192fe HEAD` exits `1`
  (`evidence-01-ancestry.log`). The `f3b5426267` diff changes exactly those
  three `_MSC_VER` guards to `_WIN32` (`evidence-02-product-fix-diff.log`).

## Ported regression artifact

1. **Marked blocks.** `tests/src/plugins/SfzDistoBuffersTest.cpp:10-14,53-67,83-93`
   now carry the `zene-portable-aligned-include/new/delete: begin|end`
   markers the probe extracts.
2. **New probe.** `tests/windows-arm64-aligned-new-check.py` reads those three
   blocks from the real source and compiles plus runs them twice:
   - native: no `-D_WIN32`; the POSIX branch must allocate 32-byte-aligned
     storage and free it;
   - simulated Windows: `-D_WIN32`, with a shim `malloc.h` that declares
     `_aligned_malloc`/`_aligned_free` and a poisoned `posix_memalign` returning
     `ENOMEM`. A wrong guard selects the POSIX branch, throws `std::bad_alloc`,
     and the probe exits non-zero.
3. **Registration.** `tests/CMakeLists.txt:6339-6348` registers
   `WindowsArm64AlignedNewCheck` on Linux with Python, matching the existing
   `Train*Portability` source-derived checks. The CMake reconfigure emitted the
   CTest entry at `build/tests/CTestTestfile.cmake`.
4. **Manifests.** `tests/fork-sources.txt` registers the new probe and this
   report; `tests/upstream-modifications.txt` records the inherited
   `tests/CMakeLists.txt` registration.

## Test matrix

- Normal path: native host compile/run of the extracted include/new/delete
  blocks; aligned allocation and matching free succeed.
- Failure path: simulated `_WIN32` with `posix_memalign` poisoned; a wrong
  guard is reported as a failure, not silently skipped.
- Exhaustion path: the poisoned POSIX allocator returns `ENOMEM`, which becomes
  `std::bad_alloc` from the test allocator and a non-zero probe exit.
- Reconfiguration path: the check is exercised both directly and through the
  registered CTest target after CMake re-ran.
- Audio-thread constraints: not applicable; this is test infrastructure (a test
  executable's replacement global `operator new/delete`), not product or audio
  code.
- Supported-platform build requirement: the include/declaration and both
  operator branches are compiled, not just source-scanned. A real hosted
  windows-arm64 run is not available locally and remains unverified here.

## Commands and exit codes

All logs are in
`/home/kruzzzzy/.config/kads/zene/state/runs/ci-00e6f05f41fa4f1a-port.r3/logs`.

| # | command | exit |
|---|---------|------|
| 1 | `python3 tests/windows-arm64-aligned-new-check.py --source-root . > test-01-green.log 2>&1` | `0` |
| 2 | `cmake --build build --target SfzDistoBuffersTest -j4 > build-01-sfzdisto.log 2>&1` | `0` |
| 3 | `cd build/tests && ctest -R "WindowsArm64AlignedNewCheck|SfzDistoBuffersTest" --output-on-failure > test-04-ctest-family.log 2>&1` | `0` (2/2) |
| 4 | mutate the three guards `_WIN32` -> `_MSC_VER`; `python3 tests/windows-arm64-aligned-new-check.py --source-root . > test-02-red.log 2>&1` | `1` |
| 5 | restore the guards; same probe `> test-03-green-restored.log 2>&1` | `0` |
| 6 | mutate again; `diff -u ... > fix.patch` (guard-only patch) | `1` (differences, expected) |
| 7 | `cd build/tests && ctest -R "WindowsArm64AlignedNewCheck" --output-on-failure > test-05-ctest-red.log 2>&1` | `8` (1 failed) |
| 8 | `git apply -R fix.patch` (restore) | `0` |
| 9 | `cd build/tests && ctest -R "WindowsArm64AlignedNewCheck" --output-on-failure > test-06-ctest-green-restored.log 2>&1` | `0` |
| 10 | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > gates-01.log 2>&1` | `0` |

RED diagnostic from `test-02-red.log` / `test-05-ctest-red.log`:

```
windows-arm64 aligned-new check: windows: the POSIX posix_memalign branch was selected
(stub returned ENOMEM); the aligned operators must use the Windows CRT under _WIN32
```

GREEN family run (`test-04-ctest-family.log`) is `2/2`:
`SfzDistoBuffersTest Passed` and `WindowsArm64AlignedNewCheck Passed`.

## Gates

`feature_check.py` exit `0`, all rows PASS (`gates-01.log`): fork-sources,
all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology,
doc-figures, complexity (no new item in these files) and file-length (no new
item in these files).

## Self-check

- **Regression test fails with the product change reverted, passes with it.**
  Reverting the three `_WIN32` guards to `_MSC_VER` makes the direct probe exit
  `1` (`test-02-red.log`) and CTest exit `8` (`test-05-ctest-red.log`);
  restoring `_WIN32` makes them exit `0` (`test-03-green-restored.log`,
  `test-06-ctest-green-restored.log`).
- **Deterministic.** The check is a compile-and-run of fixed extracted blocks;
  it has no wall-clock timing, sleeps, threads or races. The only seam is the
  injected `-D_WIN32` preprocessor definition and the shim `malloc.h`.
- **Cause and completeness.** The include, allocation and matching free are all
  guarded together, so declaration/allocation/deallocation agree on every
  compiler. The only sibling, `AllocationProbe.cpp`, already uses `_WIN32`
  (`evidence-03-guard-contract.log`). No `_MSC_VER` remains in
  `SfzDistoBuffersTest.cpp`.
- **No collateral damage.** The only edit to the existing test source is three
  marker comment pairs; the Linux native path is unchanged (real
  `posix_memalign`/`std::free`). No product code, saved data, project format,
  limit, undo or command is touched.
- **ALREADY-FIXED evidence.** `tests/src/plugins/SfzDistoBuffersTest.cpp:11,59,87`
  are `#if defined(_WIN32)`. The equivalent product commit `f3b5426267` is an
  ancestor of HEAD (exit `0`) while `1f1c5192fe` is not (exit `1`)
  (`evidence-01-ancestry.log`); the product diff is in
  `evidence-02-product-fix-diff.log`.
- **Sibling commands.** No command files were changed, so no sibling command
  family is affected.

## Other findings

None.
