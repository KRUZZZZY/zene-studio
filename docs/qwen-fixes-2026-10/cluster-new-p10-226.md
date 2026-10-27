Verdict: FIXED

new-p10-226: LocklessAllocator constructor accepted dimensions whose rounding, pool byte count or ptrdiff_t addressing wrapped size_t, so it could allocate a pool smaller than the offsets `alloc()` later hands out.

Evidence (pre-fix tree, `src/core/LocklessAllocator.cpp` at `639e1d23e`):

- `:60` `m_capacity = align( nmemb, SIZEOF_SET )` and `:61` `m_elementSize = align( size, sizeof( void * ) )` - the `align()` helper (`:43-51` before the fix) added the padding with no overflow check, so `nmemb` near SIZE_MAX rounds to 0, and `size` near SIZE_MAX rounds to 0 (then `free()` divides by `m_elementSize` at the old `:147`).
- `:62` `m_pool = new char[m_capacity * m_elementSize]` - the product was unchecked. For example `nmemb = 32`, `size = 2^59` gives a product of 2^64, which wraps to 0; `size = 2^59 + 8` wraps to 256 bytes, while `alloc()` (`:134-135`) returns offsets up to 31 * elementSize.
- The zero-size guard at `:58` (added by new-p10-225) is the only check before the fix.

Fix (after the fix, `src/core/LocklessAllocator.cpp`):

- `:47-55` `align()` throws `std::length_error` when the padding would overflow size_t.
- `:69-75` the constructor refuses a pool whose byte count exceeds `PTRDIFF_MAX` (free() computes offsets as `ptrdiff_t`, `:161`). The check is `m_capacity > maxPoolSize / m_elementSize`; `m_elementSize >= 1` is guaranteed by the zero check and by `align()`, so the division is safe. Nothing is allocated before the checks.
- Ordinary sizes are unchanged: `nmemb = 0` still gives an empty pool, and every existing caller (`LocklessList(PlayHandle::MaxNumber)` in `src/core/AudioEngine.cpp:497`, and the teardown test) passes small values.

Regression: `tests/src/core/ArrayVectorTest.cpp:146` `overflowingLocklessAllocatorDimensionsTest` - four wrap cases and one `ptrdiff_t` case, each expecting `std::length_error`.

Commands run (EXIT codes are from the unpiped shell; logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-226.r1/logs/`):

| Step | Command | EXIT | Result |
| --- | --- | --- | --- |
| Build, first try | `cmake --build build --target ArrayVectorTest -j4 > build-1-red.log 2>&1` | 2 | ccache could not create its temp file under the sandbox-denied `~/.local/share/zene-bunny-fix/dsh/...` path; no compile happened |
| Build, RED | `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp/ccache-tmp cmake --build build --target ArrayVectorTest -j4 > build-2-red.log 2>&1` | 0 | built |
| RED test | `cd build/tests && ctest -R '^ArrayVectorTest$' --output-on-failure > test-1-red.log 2>&1` | 8 | `51 passed, 1 failed`: `overflowingLocklessAllocatorDimensionsTest() Expected exception of type std::length_error to be thrown but no exception caught` |
| Build, GREEN (first) | `cmake --build build --target ArrayVectorTest -j4 > build-3-green.log 2>&1` | 0 | built |
| Test, GREEN (first) | `ctest -R '^ArrayVectorTest$' --output-on-failure > test-2-green.log 2>&1` | 0 | 100% passed, 1/1 ctest |
| 4b revert | `git diff -- src/core/LocklessAllocator.cpp > logs/fix.patch && git apply -R logs/fix.patch` | 0 / 0 | fix removed from the product file |
| 4b build, reverted | `cmake --build build --target ArrayVectorTest -j4 > build-4-revert.log 2>&1` | 0 | built |
| 4b test, reverted (RED) | `ctest -R '^ArrayVectorTest$' -V > test-3-revert-red.log 2>&1` | 8 | `FAIL! ... overflowingLocklessAllocatorDimensionsTest() Expected exception of type std::length_error ... no exception caught`; `51 passed, 1 failed` |
| 4b re-apply | `git apply logs/fix.patch` | 0 | fix restored |
| 4b build, re-applied | `cmake --build build --target ArrayVectorTest -j4 > build-5-reapplied.log 2>&1` | 0 | built |
| 4b test, GREEN | `ctest -R '^ArrayVectorTest$' -V > test-4-green.log 2>&1` | 0 | `PASS: overflowingLocklessAllocatorDimensionsTest()`; `Totals: 52 passed, 0 failed`; 1/1 ctest |
| Dependent build | `cmake --build build --target zene AudioEngineTeardownTest -j4 > build-6-dependents.log 2>&1` | 0 | `LocklessList` user rebuilt |
| Dependent test | `ctest -R '^AudioEngineTeardownTest$' -V > test-5-teardown.log 2>&1` | 0 | `Totals: 12 passed, 0 failed`; 1/1 ctest |

Notes on the RED run: QVERIFY stops the function at its first failing assertion, so the RED log shows the first case (`LocklessAllocator(maxSize - 5, 1)`). The other four cases are verified by reasoning about the pre-fix arithmetic, not by an observed RED: the wrap cases construct without throwing and the `2^63` case would have reached `new char[2^63]`. Each of them is also covered by the GREEN run.

Gates. `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` exited 1 before running any gate: it creates `<worktree>/../zene-qwen-new-p10-226-feature-check/` (`feature_check.py:70-71`, log `logs/gates-1.log`), which is outside this run's write-jail (`PermissionError`). I did not change the script. Instead I ran each gate command it runs, from the worktree, with logs in the LOG FOLDER:

| Gate | Command | EXIT | Note |
| --- | --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 | PASS |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 | PASS |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 | PASS; `upstream-modifications.txt` has the two new entries |
| check-namespace | `tests/scripted/check-namespace` | 0 | PASS |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 | PASS |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 | PASS |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 | 68 grandfathered functions over CCN 10; none in `LocklessAllocator.cpp`, `ArrayVectorTest.cpp` or `upstream-modifications.txt` (7 REGRESSION lines, all in other files) - PASS by feature_check's rule |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 | 22 REGRESSION lines, none in the changed files (`rg -n "LocklessAllocator|ArrayVectorTest|upstream-modifications"` finds nothing in either log) - PASS by feature_check's rule |

The feature_check rule for the two ratchets fails only on items in changed files. Since none of the items is in a changed file, there was no need to compute the base listings (which would write a worktree into `zene-cachyos` and the qwen cache, both outside the jail).

## Self-check

Result of `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p10-226` (EXIT=1, `logs/submit-check-1.log`): PASS commits on branch, PASS working tree clean, PASS report present, PASS tests changed; FAIL "Self-check section" (answered here); FAIL "gates (feature_check.py)" - see the paragraph above. The gates row is an environment failure, not a gate result: feature_check.py exits 1 before running any gate because it cannot create `<worktree>/../zene-qwen-new-p10-226-feature-check/` (`feature_check.py:70-71`, PermissionError outside this run's write-jail). The gate commands it runs were executed one by one from the worktree (table above): all six simple gates exit 0, the two ratchets exit 1 with no item in the changed files. Nothing was changed to get around this.

1. **RED/GREEN proof.** RED: `ctest -R '^ArrayVectorTest$' -V`, EXIT 8 with the product change reverted (`test-3-revert-red.log`, `FAIL! ... no exception caught`, 51 passed / 1 failed). GREEN: same command with the fix re-applied, EXIT 0 (`test-4-green.log`, 52 passed / 0 failed). The revert and re-apply were done with the `git apply -R` / `git apply` pair from the procedure (both EXIT 0). The RED run stops at its first failing case; see the note above the gates table.

2. **Determinism.** No clock, sleep, thread or random input. The test only calls the constructor with fixed `size_t` values built from `std::numeric_limits<std::size_t>::max()` and shifts, and every case throws before any allocation (the pool `new` comes after the checks, `src/core/LocklessAllocator.cpp:66-75`).

3. **Cause and siblings.** `align()` (`src/core/LocklessAllocator.cpp:45-57`) has exactly two callers, both in the constructor (`:66-67`); both are now overflow-checked. The product `m_capacity * m_elementSize` (`:77`) is now bounded by `PTRDIFF_MAX` because `free()` computes `(char *)ptr - m_pool` as `ptrdiff_t` (`:161`). The `free()` modulus `diff % m_elementSize` (`:162`) can no longer see zero, because the zero check (`:64`) and the aligned size stay non-zero. `alloc()` (`:125-154`) computes offsets `(SIZEOF_SET * set + bit) * m_elementSize` only inside `[0, capacity)`, so they now stay inside the pool. The header `include/LocklessAllocator.h` is unchanged (its `LocklessAllocatorT` forwards `size_t nmemb` and `sizeof(T)` unchanged), so no other target needs a rebuild for the header. Callers of the allocator: `include/LocklessList.h:47-49` (instantiated by `include/AudioEngine.h:838` with `PlayHandle::MaxNumber`, constructed at `src/core/AudioEngine.cpp:497`) and `tests/src/core/AudioEngineTeardownTest.cpp:367,393,402` (sizes 1 and `kListCapacity`). All of them pass small values, and `AudioEngineTeardownTest` passes 12/12 (`test-5-teardown.log`, EXIT 0).

4. **No collateral damage.** The new rejections trigger only when `nmemb` or `size` rounding wraps size_t, or when the pool exceeds `PTRDIFF_MAX` bytes (about 8 EiB on this 64-bit platform). No saved project, control command or audio path constructs an allocator in that range; the allocator is memory-only and not serialised. Ordinary values (`nmemb = 0` gives an empty pool, as before) are unchanged. The one behaviour change is that an input that used to produce a corrupt allocator now throws `std::length_error` before any memory is touched.

5. **Verdict.** FIXED, so the ALREADY-FIXED/FALSE-POSITIVE evidence item does not apply.
