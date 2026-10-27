NEEDS-CONTEXT

# Cluster new-p15-163: allocator size multiplication can wrap (vendored asio)

## Verdict in one paragraph

The defect is real. `sizeof(T) * n` is computed unchecked in the vendored asio
allocator, and a count of `SIZE_MAX / sizeof(T) + 1` wraps to a small size and
returns a live block instead of throwing. Nothing in Zene calls it with such a
count today, so it is latent. The fix is three lines, but it lives in vendored
third-party code (Ableton Link's standalone asio 1.38.2, BSL-1.0) that the
project pins byte-identical to upstream. The same unchecked multiplication is
repeated in three more vendored asio sites. Patching them means diverging a
pinned vendored tree, which is an owner decision, not a bug-fix lane decision.
No product code is changed in this commit.

## Evidence: the code

Finding path `recycling_allocator.hpp` (the finding's `.h` is a typo; the file is `.hpp`).

- `src/3rdparty/ableton-link/asio/include/asio/detail/recycling_allocator.hpp:55`
  `thread_info_base::allocate(Purpose(), ..., sizeof(T) * n, alignof(T))`
- `src/3rdparty/ableton-link/asio/include/asio/detail/recycling_allocator.hpp:57`
  `asio::aligned_new(alignof(T), sizeof(T) * n)` (the `ASIO_DISABLE_SMALL_BLOCK_RECYCLING` branch)
- `src/3rdparty/ableton-link/asio/include/asio/detail/recycling_allocator.hpp:66`
  `deallocate`: `thread_info_base::deallocate(..., sizeof(T) * n)`

Same unchecked multiplication, sibling sites in the same vendored tree:

- `src/3rdparty/ableton-link/asio/include/asio/any_completion_handler.hpp:522` (`allocate`) and `:532` (`deallocate`)
- `src/3rdparty/ableton-link/asio/include/asio/execution_context.hpp:404` (`allocate`) and `:410` (`deallocate`)
- A second vendored copy under `plugins/CarlaBase/carla/source/modules/hylia/link/asio/detail/recycling_allocator.hpp`
  carries the same allocator. I did not check its line numbers.

## Evidence: the code is byte-identical to its pin (so a fix is a divergence)

- `src/3rdparty/ableton-link/MANIFEST.tsv:251` pins that file's blob to
  `14d186c74caba636d9d58f7769682b70365aaed6`.
- `git hash-object src/3rdparty/ableton-link/asio/include/asio/detail/recycling_allocator.hpp`
  prints `14d186c74caba636d9d58f7769682b70365aaed6`. The copy is still byte-identical.
- `tests/upstream-modifications.txt:1636` records the file as a fork-ADDED vendored copy, "not a change to inherited code",
  with the owner decision of 2026-10-07.
- `src/3rdparty/ableton-link/README.lmms` says every vendored asio header is kept unmodified.

A fix would change a file whose provenance the README and MANIFEST state as
unmodified, and would have to be repeated in the three sibling files. The
correct long-term place for the fix is upstream (`chriskohlhoff/asio`); a local
patch gets reverted by the next asio bump unless it is tracked.

## Evidence: reachability from Zene

- Zene's own sources do not reference `asio::recycling_allocator`,
  `get_recycling_allocator` or the asio allocators. Search: `rg -n "recycling_allocator|get_recycling_allocator|asio::recycling" --glob '!build*'`
  outside `src/3rdparty/ableton-link/asio/**` returns only the MANIFEST,
  upstream-modifications, and the Carla vendored copy.
- Inside asio, the call sites I read pass a constant count of 1:
  `src/3rdparty/ableton-link/asio/include/asio/impl/executor.hpp:148`
  (`allocator_.allocate(1)`) and `src/3rdparty/ableton-link/asio/include/asio/any_completion_handler.hpp:99` (`allocate(1)`).
  I did not audit every call site of `execution_context::allocate` or of
  `any_completion_handler` allocation paths.
- Link's own sources under `src/3rdparty/ableton-link/link/` use asio sockets, timers and executors, not these allocators: `rg -c "recycling|allocate\(" src/3rdparty/ableton-link/link` returned no matches (rc=1).

So no reachable path in this product overflows today. The risk is that a future
caller, or a future Link/asio update, passes an unchecked count.

## Evidence: RED reproduction (out of repo, standalone)

No repo test is committed. A test that proves the wrap would fail on the current
branch and turn ctest red. Instead I reproduced it in a standalone program that
includes the vendored header unchanged:

- Program: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-163.r1/logs/repro-recycling-allocator.cpp`
  (`asio::detail::recycling_allocator<std::uint64_t>().allocate(SIZE_MAX / 8 + 1)`)
- Log: `.../logs/red-repro-1.log`
- Command: `g++ -std=c++17 -I<worktree>/src/3rdparty/ableton-link/asio/include -DASIO_STANDALONE -DASIO_NO_DEPRECATED demo.cpp -o demo -pthread; ./demo`
- Output:
  ```
  COMPILE_EXIT=0
  sizeof*n wraps to 0
  allocate(n) RETURNED 0x55ed2a836030 (no throw)
  RUN_EXIT=0
  ```

Contract: `std::allocator` throws `std::bad_array_new_length` for this count;
this allocator returns a block. The defect is confirmed. The pointer is into
an allocation that cannot be sized correctly.

## What the owner needs to decide

Pick one:

1. **Patch the vendored copy locally.** Add `if (n > std::size_t(-1) / sizeof(T)) throw std::bad_array_new_length();`
   before the multiplication in `detail/recycling_allocator.hpp`, and in the three sibling sites. Each file gets an
   `upstream-modifications.txt` row with the reason, the MANIFEST blob shas get updated, and a regression test is added
   in `tests/src/core/` linking the `ableton_link` target (the same way `AbletonLinkInteropTest` does). Lane can do this
   on a decision.
2. **Report upstream and wait for an asio bump.** Keep the pin. Record this as a latent, unreachable finding against
   `chriskohlhoff/asio`.
3. **Close as latent.** No Zene path reaches the overflow; the reviewer accepts the pinned copy as-is.

## Commands run and exit codes

| Command | EXIT |
| --- | --- |
| `git hash-object src/3rdparty/ableton-link/asio/include/asio/detail/recycling_allocator.hpp` | (no exit check; value matched MANIFEST:251) |
| `g++ ... demo.cpp -o demo -pthread > red-repro-1.log` | 0 (COMPILE_EXIT) |
| `./demo` | 0 (RUN_EXIT) |
| `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | see Self-check |
| `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p15-163` | see Self-check |

No product file was changed, so no build target, no ctest run and no
regression test was added. The lane's RED/GREEN steps (4, 4b) are not applicable
to this commit: there is no product change to revert and re-apply.

## Self-check

Verdict is NEEDS-CONTEXT, so there is no product fix to test. Each item below is answered against that.

- **Test FAILS with the fix reverted and PASSES with it.** N/A in this commit: no product change, no committed test.
  The RED behaviour is shown by the standalone reproduction (`red-repro-1.log`, RUN_EXIT=0, `allocate(...)` returned a
  pointer instead of throwing). A committed suite test would fail on the current branch and turn ctest red, so it waits
  for the owner's choice (see "What the owner needs to decide").
- **Deterministic test.** N/A (no test committed). The reproduction uses no timing, sleeps or threads.
- **Fix at the cause, every caller and sibling checked.** N/A (no fix). The sibling family is recorded above
  (`any_completion_handler.hpp:522,532`, `execution_context.hpp:404,410`, and the Carla copy, unchecked line-by-line).
- **No collateral damage.** N/A (no fix). Nothing in Zene calls these allocators; the Link asio callers that were read pass 1.
- **ALREADY-FIXED / FALSE-POSITIVE evidence.** N/A: the verdict is NEEDS-CONTEXT, not either of those.

Gates, run by hand because `feature_check.py` could not run in this jail. It writes its logs to
`<worktree>/../<name>-feature-check/`, which is the shared `projects/` directory, and it failed with
`PermissionError` there (`feature-check-1.log`, exit 1). I ran each gate command it lists from the worktree
(`run-gates-3.sh`, `gate-summary-3.log`):

| Gate | EXIT |
| --- | --- |
| fork-sources (`bash tests/fork-sources-gate.sh`) | 0 |
| all-sources-reproduce | 0 |
| no-upstream-regression | 0 |
| check-namespace | 0 |
| no-tautology | 0 |
| doc-figures | 0 |
| complexity (`--check`) | 1 (7 REGRESSION lines, all on train files) |
| file-length (`--check`) | 1 (22 REGRESSION lines, all on train files) |

The two ratchets are red on the train on purpose. `git diff <merge-base>..HEAD` against the fix base
`050/m0-train-17` is empty (`branch-changed-files-3.txt`, 0 lines), and none of the regression files
(e.g. `src/wasm/WasmSandbox.cpp`, `src/core/DawProjectReadTracks.cpp`, `plugins/Vst3Effect/Vst3Host.cpp`) is among
the branch's changes. By the feature_check rule they should PASS; the raw exit is 1 because the gate prints every
train item.

`submit_check.py` (`submit-check-1.log`, exit 1) reports: commits (none, until this report is committed), clean tree
(this file, until committed), tests added (none, by design), and gates (the feature_check.py permission failure above).
