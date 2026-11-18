# Cluster new-p10-146: Missing-effect fallback discards the saved stable ID

**Verdict: FIXED**

## Evidence

`EffectChain::loadSettings()` constructs a `DummyEffect` for an unavailable plugin at `src/core/EffectChain.cpp:166-175`. The chain later saves that dummy's original XML at `src/core/EffectChain.cpp:130-138`, so its stored `id` survived while the runtime object retained the fresh ID from the `Effect` constructor.

`include/DummyEffect.h:101-107` now restores a valid ID for project document elements. It uses `Effect::setId()`, which calls `ProjectIds::observe()` (`src/core/Effect.cpp:99-106`), and counts an ID-less or malformed project element as a load assignment. Copy payloads continue to keep the receiving instance's ID, matching `Effect::loadSettings()`.

The regression in `tests/src/core/StableTrackIdsTest.cpp:482-507` loads a missing effect with ID `4242`, checks the runtime ID and the advanced project counter, then checks that saving preserves `4242`.

## Verification

The RED run used the original production code and failed at `dummy->id() == 4242` (`test-1.log`: actual `3`, expected `4242`). The fixed test passed. The reversal check also failed with the product patch removed and passed after reapplying it. The affected CTest family ran **1/1 test target** (`StableTrackIdsTest`) on each run.

| Check | Exact command | Exit |
|---|---|---:|
| RED build | `cmake --build build --target StableTrackIdsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-146.r1/logs/build-1.log 2>&1; echo EXIT=$?` | 0 |
| RED test, from worktree root | `cd build/tests && ctest -R '^StableTrackIdsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-146.r1/logs/test-1.log 2>&1; echo EXIT=$?` | 8 |
| Fixed build | `cmake --build build --target zene StableTrackIdsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-146.r1/logs/build-2.log 2>&1; echo EXIT=$?` | 0 |
| GREEN test, from worktree root | `cd build/tests && ctest -R '^StableTrackIdsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-146.r1/logs/test-2.log 2>&1; echo EXIT=$?` | 0 |
| Save production patch | `git diff -- include/DummyEffect.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-146.r1/logs/fix.patch` | 0 |
| Reverse production patch | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-146.r1/logs/fix.patch` | 0 |
| Reversal build | `cmake --build build --target zene StableTrackIdsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-146.r1/logs/build-3.log 2>&1; echo EXIT=$?` | 0 |
| Reversal test | `cd build/tests && ctest -R '^StableTrackIdsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-146.r1/logs/test-3.log 2>&1; echo EXIT=$?` | 8 |
| Reapply production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-146.r1/logs/fix.patch` | 0 |
| Restored build | `cmake --build build --target zene StableTrackIdsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-146.r1/logs/build-4.log 2>&1; echo EXIT=$?` | 0 |
| Restored GREEN test | `cd build/tests && ctest -R '^StableTrackIdsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-146.r1/logs/test-4.log 2>&1; echo EXIT=$?` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-146.r1/logs/gate-1.log 2>&1; echo EXIT=$?` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-146.r1/logs/gate-2.log 2>&1; echo EXIT=$?` | 0 |
| Whitespace check | `git diff --check` | 0 |

The `fff` grep tool was unavailable in this session, so I searched the designated log directory with `rg` for `error`, `FAILED`, and `Assertion`.

## Files committed

- `include/DummyEffect.h`
- `tests/src/core/StableTrackIdsTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-146.md`
