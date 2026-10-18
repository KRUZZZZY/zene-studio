# Cluster new-p10-433: frozen-take tick parsing

**Verdict: BLOCKED** — the bug fix and regression are verified, but both required ratchet gates fail on unrelated existing paths, so the gates are not green.

## Evidence

- [`src/core/Track.cpp:213`](../../src/core/Track.cpp) parses ticks with conversion-success and `tick_t` range checks. The muted-clip parser skips invalid pairs at line 234. `Track::loadFrozenTake()` at line 1239 discards a frozen record when a present start or end tick is invalid; absent attributes keep the existing zero defaults.
- [`tests/src/core/FrozenTakeRoundTwoTest.cpp:191`](../../tests/src/core/FrozenTakeRoundTwoTest.cpp) loads saved project files containing malformed and out-of-range attributes. On original code, `!song->tracks().back()->isFrozen()` failed for `frozenStart="not-a-tick"`. Reversing the production patch reproduced the same assertion failure.
- The affected ctest family executed 1 registered test (`FrozenTakeRoundTwoTest`). The RED QtTest run reported 8 passed and 1 failed; the GREEN ctest runs passed 1/1.

## Commands and exit codes

Commands below ran from the worktree root unless the ctest working directory is shown. Build/test output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-433.r1/logs`.

| Purpose | Exact command | Exit |
|---|---|---:|
| Build regression on original code | `cmake --build build --target FrozenTakeRoundTwoTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-433.r1/logs/build-1.log 2>&1` | 0 |
| RED run (`build/tests`) | `ctest -R '^FrozenTakeRoundTwoTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-433.r1/logs/test-1.log 2>&1` | 8 |
| Build `zene` and test after fix | `cmake --build build --target zene FrozenTakeRoundTwoTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-433.r1/logs/build-2.log 2>&1` | 0 |
| GREEN run (`build/tests`) | `ctest -R '^FrozenTakeRoundTwoTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-433.r1/logs/test-2.log 2>&1` | 0 |
| Reverse production patch | `git diff -- src/core/Track.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-433.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-433.r1/logs/fix.patch` | 0 |
| Rebuild without fix | `cmake --build build --target zene FrozenTakeRoundTwoTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-433.r1/logs/build-3.log 2>&1` | 0 |
| Reverse-patch RED run (`build/tests`) | `ctest -R '^FrozenTakeRoundTwoTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-433.r1/logs/test-3.log 2>&1` | 8 |
| Reapply production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-433.r1/logs/fix.patch` | 0 |
| Rebuild with fix | `cmake --build build --target zene FrozenTakeRoundTwoTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-433.r1/logs/build-4.log 2>&1` | 0 |
| Reverse-proof GREEN run (`build/tests`) | `ctest -R '^FrozenTakeRoundTwoTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-433.r1/logs/test-4.log 2>&1` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 1 |
| File-length gate | `bash tests/file-length-gate.sh --check` | 1 |

The complexity gate reports six over-target new functions in other files; it does not list `parseTick`. The file-length gate reports 14 new over-500-line files in other paths; it does not list `FrozenTakeRoundTwoTest.cpp`. These gate failures predate this change in the current tree and cannot be corrected within this one-bug scope without changing unrelated behavior or re-anchoring a gate.

## Files in this change

- `src/core/Track.cpp`
- `tests/src/core/FrozenTakeRoundTwoTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-433.md`
