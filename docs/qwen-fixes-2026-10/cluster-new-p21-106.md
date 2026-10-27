Verdict: FALSE-POSITIVE

new-p21-106: FALSE-POSITIVE

Evidence: the null dereference cannot happen. `mix` is non-null when it reaches the resample call, and `resampleBuffer()` never returns null, so `mix->size()` at [ControlStemSupport.cpp:249](src/core/ControlStemSupport.cpp:249) is always on a live buffer.

1. The guard runs first. `SampleBuffer::fromFile()` returns a shared_ptr ([SampleBuffer.h:93](include/SampleBuffer.h:93)); [ControlStemSupport.cpp:231](src/core/ControlStemSupport.cpp:231) returns a typed error for `!mix || mix->empty()`. From line 232 on, `mix` is non-null.
2. `fitsAtModelRate()` at [ControlStemSupport.cpp:66-69](src/core/ControlStemSupport.cpp:66) is a pure predicate; it does not touch `mix`.
3. `resampleBuffer()` ([SampleBufferResample.cpp:58](src/core/SampleBufferResample.cpp:58)) has exactly two kinds of return:
   - `return input;` at [SampleBufferResample.cpp:67](src/core/SampleBufferResample.cpp:67) for empty input, zero source rate, zero target rate, or source rate equal to target. `input` is the non-null `mix` from step 1.
   - `return std::make_shared<const SampleBuffer>(...)` at [SampleBufferResample.cpp:88](src/core/SampleBufferResample.cpp:88), which is non-null by construction.
   The declared contract ([include/SampleBufferResample.h:37-42](include/SampleBufferResample.h:37)) says the input is returned when already at the target rate, and says nothing about a null return.
4. Allocation failure does not produce null. The only allocation is `std::vector<SampleFrame> output(wanted + 8192)` at [SampleBufferResample.cpp:74](src/core/SampleBufferResample.cpp:74), and `make_shared` at line 88. Both throw `std::bad_alloc` on failure; neither returns a null pointer.
5. A thrown `bad_alloc` is already a typed error, not a crash. The command dispatch wraps the handler in `try` with `catch (const std::exception&)` and `catch (...)` at [ControlRegistry.cpp:300-318](src/core/ControlRegistry.cpp:300), and `runHandlerGuarded` does the same at [ControlRegistry.cpp:451-455](src/core/ControlRegistry.cpp:451). Both return `controlErrorFromException`.

So the requested typed-error check after `resampleBuffer()` would be dead code: it tests a value the function cannot return as null. The sibling caller at [RenderQueue.cpp:207](src/core/RenderQueue.cpp:207) already relies on the same non-null contract. No product file changed, so no RED/GREEN regression test applies (procedure step 2 skips to step 6).

Observation, not changed (outside this finding): `fitsAtModelRate()` at [ControlStemSupport.cpp:66-69](src/core/ControlStemSupport.cpp:66) admits frame counts up to `INT_MAX` at the model rate. The resample allocation at [SampleBufferResample.cpp:74](src/core/SampleBufferResample.cpp:74) then scales with that count. If `SampleFrame` is two floats (sizeof not checked here), the worst case is about 16 GiB. On this 30 GB shared machine, that may end in an OOM kill rather than a `bad_alloc` error reply. This is a resource-limit question for a separate finding; it is not a null dereference.

| Evidence | Command | Result |
| --- | --- | --- |
| Feature gate, as the procedure states it | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-106 > .../logs/001-feature-check.log 2>&1; echo EXIT=$?` | EXIT=1. The script creates its log folder beside the worktree (`zene-qwen-new-p21-106-feature-check`), which is outside the write-jail, so it failed with `PermissionError` before any gate ran. Log: `logs/001-feature-check.log`. |
| Same gate, output directory redirected in a copy | Copy of `feature_check.py` in `logs/gate/`, with two changes: the log folder points to `logs/gate/feature-check-out`, and `ROOT` points to the original gate directory so the same FIX-BASE and gate-baselines are used. Gate logic is otherwise unchanged. Run: `python3 logs/gate/feature_check_logdir.py <worktree> > logs/002-feature-check.log 2>&1` | EXIT=1 on the first attempt, because `ROOT` resolved to the copy's directory and `FIX-BASE` was not found (`logs/002-feature-check.log`). |
| Same gate, after fixing `ROOT` in the copy | `python3 logs/gate/feature_check_logdir.py <worktree> > logs/003-feature-check.log 2>&1; echo EXIT=$?` | EXIT=0. RESULT: PASS. All eight rows PASS: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity (no new item), file-length (no new item). Log: `logs/003-feature-check.log`. |
| Product diff (no product change) | `git diff --stat -- src include tests` | empty; no product file changed |
| Regression test RED/GREEN | not run | No product change, so there is no fix to reverse (step 4b). No regression test was added because the claim is not real. |

Files committed for this verdict: `docs/qwen-fixes-2026-10/cluster-new-p21-106.md`. No product file and no test file changed.
