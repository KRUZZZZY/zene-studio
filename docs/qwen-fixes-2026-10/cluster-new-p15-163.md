FIXED

# Cluster new-p15-163: allocator size multiplication can wrap (vendored asio)

## Verdict in one paragraph

The defect is real and is fixed. The vendored asio 1.38.2 copy under
`src/3rdparty/ableton-link` multiplied `sizeof(T) * n` unchecked in three
`allocate()` functions. A count of `SIZE_MAX / sizeof(T) + 1` wrapped to a small
size and returned a live block instead of throwing. Each of the three now refuses
such a count with `std::bad_array_new_length`, as `std::allocator` does. Nothing
in Zene passes a count that overflows, so the fix changes no behaviour for any
legitimate caller. The vendored file now differs from its pinned upstream blob by
that guard only. That difference is declared in `tests/upstream-modifications.txt`
(the ledger for inherited files this repo may change), with a regression test
beside it. The earlier NEEDS-CONTEXT verdict is withdrawn: the ledger exists for
exactly this kind of declared change.

## Evidence: the defect (before)

Finding path `recycling_allocator.hpp` (the finding's `.h` is a typo; the file is `.hpp`).

- `src/3rdparty/ableton-link/asio/include/asio/detail/recycling_allocator.hpp:50-57` (pre-fix):
  `allocate(n)` computes `sizeof(T) * n` for `thread_info_base::allocate` (line 55) and `aligned_new` (line 57).
- `.../any_completion_handler.hpp:516-523` (pre-fix): `allocate(n)` computes `sizeof(T) * n` for `fn_table_->allocate` (line 522).
- `.../execution_context.hpp:402-405` (pre-fix): `allocate(n)` computes `sizeof(T) * n` for `impl_->allocate` (line 404).

Reproduced: for `std::uint64_t`, `allocate(SIZE_MAX / 8 + 1)` returned a live pointer on all three paths
(see RED below).

## Evidence: the fix (after)

Each of the three `allocate()` functions starts with the same guard (the Zene patch comment marks it):

```
if (n > std::size_t(-1) / sizeof(T))
{
  std::bad_array_new_length ex;
  asio::detail::throw_exception(ex);
}
```

plus the includes the guard needs: `<new>` in all three, and `asio/detail/throw_exception.hpp` in
`detail/recycling_allocator.hpp` and `execution_context.hpp`. `any_completion_handler.hpp` already
used `asio::detail::throw_exception`. The `std::size_t(-1)` form compares before the multiply, so the
product cannot wrap.

Diff: `git diff -- src/3rdparty/ableton-link/asio/include/asio/` (3 files, 23 insertions).

## Sibling and caller check

- **`allocate()` sites changed (3):** recycling_allocator.hpp (the finding), execution_context.hpp, any_completion_handler.hpp. These are the only three `allocate()` functions in the vendored asio that multiply `sizeof(T) * n` on a caller-supplied `n`.
- **`deallocate()` sites left unchanged (3):** recycling_allocator.hpp:66-ish, any_completion_handler.hpp (deallocate), execution_context.hpp (deallocate). A `deallocate(p, n)` is only reached for a block that `allocate(n)` returned. `allocate` now refuses any count whose byte size wraps, so the matching `deallocate` size cannot wrap either. Changing them would be dead code.
- **Callers of the three allocators:** asio's own call sites pass `n = 1` (for example `impl/executor.hpp:148`, `any_completion_handler.hpp:99`). The guard never trips for them.
- **Translation units that include the changed headers, all rebuilt against the fix:** `zene_ableton_link` (`src/core/AbletonLinkSession.cpp`), `zene` (relink), `AbletonLinkSessionTest`, `AbletonLinkInteropTest`, and the new `AbletonLinkAllocatorTest`. `include/LinkVendored.h` is the only route into Link's headers and is included by those TUs only.
- **Second vendored copy, not changed:** `plugins/CarlaBase/carla/source/modules/hylia/link/asio/detail/recycling_allocator.hpp` holds the same allocator in a separate pinned copy under the Carla plugin tree. I did not change it, and I did not check whether that copy's line numbers match. It is a follow-up for the owner, not part of this commit.

## Evidence: the regression test

- `tests/src/core/AbletonLinkAllocatorTest.cpp` (new). Four QTest slots:
  `recyclingAllocatorRejectsWrappingCount`, `recyclingAllocatorStillAllocatesAndFrees`,
  `executionContextAllocatorRejectsWrappingCount`, `anyCompletionHandlerAllocatorRejectsWrappingCount`.
- Registered in `tests/CMakeLists.txt` inside `IF(LMMS_HAVE_ABLETON_LINK)` next to `AbletonLinkSessionTest`, with
  `target_link_libraries(AbletonLinkAllocatorTest PRIVATE ableton_link)` (same as `AbletonLinkInteropTest`).
- Registered in `tests/fork-sources.txt` (fork-NEW test file).

## Evidence: RED and GREEN (exact commands and exit codes)

Product patch (the 3 asio headers) saved as `logs/fix.patch` (84 lines).

| Step | Command | EXIT |
| --- | --- | --- |
| RED build (real test, fix reverted) | `git apply -R logs/fix.patch`; `CCACHE_DISABLE=1 cmake --build build --target AbletonLinkAllocatorTest -j4` (`build-red-test-1.log`) | 0 / 0 |
| RED run | `cd build/tests && ctest -R "^AbletonLinkAllocatorTest$" --output-on-failure` (`ctest-red-test-1.log`) | **8** (3 FAILED, 3 passed of the QTest checks: `Totals: 3 passed, 3 failed`) |
| GREEN build (fix applied) | `git apply logs/fix.patch`; `CCACHE_DISABLE=1 cmake --build build --target AbletonLinkAllocatorTest -j4` (`build-green-test-2.log`) | 0 |
| GREEN run | same ctest command (`ctest-green-test-2.log`) | **0** (`100% tests passed, 0 tests failed out of 1`) |
| First GREEN run, before the revert step | same ctest command (`ctest-green-test-1.log`) | 0 |

The RED failures name the three overflow checks:
`recyclingAllocatorRejectsWrappingCount`, `executionContextAllocatorRejectsWrappingCount`, and
`anyCompletionHandlerAllocatorRejectsWrappingCount`, each `returned FALSE`. The normal-path check passes in both runs.

Standalone harness, mirroring the test with the build's asio defines (`allocator-harness.cpp`, `run-harness.sh`):

| Run | RUN_EXIT | Result |
| --- | --- | --- |
| Unfixed, C++20 (`harness-red2-run.log`) | 3 | FAILURES=3 |
| Fixed (`harness-green1-run.log`) | 0 | FAILURES=0 |
| Fix reverted via 4b (`harness-red-4b-run.log`) | 3 | FAILURES=3 |
| Fix reapplied via 4b (`harness-green-4b-run.log`) | 0 | FAILURES=0 |

The first harness run without `-std` compiled with the compiler default; it's kept only as context and is not evidence.

Syntax check of the test file with the sibling test's compile flags: `syntax-test-3.log`, EXIT 0.

### Affected-family run (fix applied)

`cd build/tests && ctest -R AbletonLink --output-on-failure` (`ctest-affected-final.log`): **EXIT 8**, 3 tests executed.

| Test | Result |
| --- | --- |
| AbletonLinkAllocatorTest | Passed (new, this commit) |
| AbletonLinkInteropTest | Passed |
| AbletonLinkSessionTest | **SEGFAULT, pre-existing** (see below) |

**Pre-existing failure, not caused by this change.** `AbletonLinkSessionTest` segfaults
in `twoParticipantsFormOneSession`. The host did not deliver a multicast datagram, so
the test's `joinBoth()` calls `QSKIP`; `QSKIP` returns only from `joinBoth()`, and the
caller goes on to dereference `m_a`, which is still null (`tests/src/core/AbletonLinkSessionTest.cpp:213-222`,
crash in `AbletonLinkSession::numPeers()`). To prove it predates this change, I reverted the
fix, rebuilt only `AbletonLinkSessionTest` (`build-baseline-session-1.log`, EXIT 0), and ran it:
`ctest-baseline-session-1.log` **EXIT 8**, SegFault, same as with the fix. I reapplied the fix
and rebuilt (`build-restored-session-1.log`, EXIT 0). I did not change that test here; the owner
should decide whether to fix the `QSKIP` caller.

### Dependent builds (after the fix)

| Target | EXIT | Note |
| --- | --- | --- |
| zene_ableton_link | 0 | first attempt EXIT 2: ccache could not write its temp file under the bunny directory (`build-dependents-zene_ableton_link.log`). Rebuilt with `CCACHE_DISABLE=1` (`-2.log`, EXIT 0). |
| zene | 0 | `build-dependents-zene-2.log` |
| AbletonLinkSessionTest | 0 | `build-dependents-AbletonLinkSessionTest-2.log` |
| AbletonLinkInteropTest | 0 | `build-dependents-AbletonLinkInteropTest-2.log` |

The `build/` tree was reconfigured by make itself when these targets were built, which is how the new
test target appeared. I did not run `cmake -S . -B build` (the lane guard blocks it; `configure-1.log`, EXIT 97).

## Gates

`feature_check.py` itself cannot run in this jail: it writes its logs into the shared `projects/` directory and fails with `PermissionError` (`feature-check-2.log`). I ran each gate command it lists, from the worktree, with `run-gates-4.sh` (`gate-summary-4.log`):

| Gate | EXIT |
| --- | --- |
| fork-sources | 0 |
| all-sources-reproduce | 0 |
| no-upstream-regression | 0 |
| check-namespace | 0 |
| no-tautology | 0 |
| doc-figures | 0 |
| complexity (`--check`) | 1 (7 REGRESSION lines, all train items) |
| file-length (`--check`) | 1 (22 REGRESSION lines, all train items) |

The two ratchets are red on the train on purpose. I checked their REGRESSION file names against this
branch's changed files (`changed-files-4.txt`: the 3 asio headers, the report, `tests/CMakeLists.txt`,
`tests/fork-sources.txt`, `tests/upstream-modifications.txt`, the new test): no overlap.

## Ledger and provenance

- `tests/upstream-modifications.txt`: the three existing rows (any_completion_handler.hpp, detail/recycling_allocator.hpp, execution_context.hpp) had reasons that said "not a change to inherited code". Each now says the file has one declared local change (this guard), points to the test, and says it differs from the MANIFEST pin by that guard only. The no-upstream-regression gate passes with these rows.
- `src/3rdparty/ableton-link/MANIFEST.tsv` still lists the **upstream** blob pins (`14d186c7…` for recycling_allocator.hpp). I did not change them, since they record the upstream pin. The three files now differ from those pins by the guard.
- `src/3rdparty/ableton-link/README.lmms` still says every vendored asio header is unmodified. That statement is out of date for these three files. I did not edit that README; the owner should decide whether to add a note there.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it (exit codes).** Yes. Real test: RED run EXIT 8 (3 FAILED checks, `ctest-red-test-1.log`), GREEN run EXIT 0 (`ctest-green-test-2.log`); the revert and reapply are the 4b steps in the table above. The standalone mirror also gives RUN_EXIT 3 reverted and 0 fixed.
- **The test is deterministic (name the seam or injected values).** No clock, sleep, thread or race. The overflowing count is computed from the element type: `overflowingCount<T>() = SIZE_MAX / sizeof(T) + 1`. The only inputs are `sizeof(T)` and `std::size_t` limits. The execution-context test builds an `asio::io_context` and never runs it. The any-completion-handler test builds a handler from a no-op lambda. Nothing waits on the network.
- **The fix is at the cause and complete (every caller and sibling checked).** The cause is the unchecked `sizeof(T) * n` in the three `allocate()` functions; all three are guarded. Each `deallocate()` is intentionally unchanged, because a size that wraps can only come from a count `allocate` already refused (see "Sibling and caller check"). Callers and the TUs that include the headers were rebuilt and checked. The Carla copy is not changed (follow-up).
- **No collateral damage.** Legitimate counts satisfy `n <= SIZE_MAX / sizeof(T)` and never reach the throw, so the guard only adds one comparison to a normal allocation. The throw path runs only for an impossible count. No saved or loaded data passes through these allocators. The normal path allocates nothing extra and takes no lock. The `bad_array_new_length` object lives on the stack, and only the throw path needs exception memory. Realtime rule: none of the new code runs on the audio thread's normal path. The `AbletonLinkSessionTest` segfault is pre-existing and unchanged by this (baseline reproduced, EXIT 8 with the fix reverted).
- **If the verdict is ALREADY-FIXED or FALSE-POSITIVE.** N/A. The verdict is FIXED.
- **Gates item in `submit_check.py` (`submit-check-3.log`, still FAIL).** This is the exit code of `feature_check.py`, and that script cannot run in this lane. It crashes with `PermissionError` writing `<worktree>/../<name>-feature-check/` in the shared `projects/` directory (`feature-check-2.log`), before any table is printed. Its `resolve()` also defeats a symlink workaround. The six gate commands it runs exit 0 and the two ratchets are red only on train items (`gate-summary-4.log`, `changed-files-4.txt`). The gates item needs the parent to run `feature_check.py` from a context where that directory is writable.
