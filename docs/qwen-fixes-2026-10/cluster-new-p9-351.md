# Cluster new-p9-351

**Verdict: FIXED**

`PatmanInstrument::loadPatch` now checks that the declared sample bytes remain in the file and that cumulative decoded frames stay within 128 MiB of retained stereo PCM before allocating. The bounds apply across all samples in a patch. The regression builds a tiny valid Patman header with a near-maximum 16-bit sample length and guards large array allocations in the test process.

## File evidence

- `plugins/Patman/Patman.cpp:89-102` defines the retained PCM budget and checks the remaining file payload using large-file offsets.
- `plugins/Patman/Patman.cpp:300-361` tracks cumulative frames and rejects truncated or oversized samples before allocations at lines 366, 388, and 405.
- `tests/src/plugins/PatmanLoadTest.cpp:82-111` exercises the malformed header and asserts that the loader does not reach a large array allocation.
- `tests/CMakeLists.txt:426,982-995` registers the test and compiles it with the Patman implementation.
- `tests/fork-sources.txt:2064` registers the new test source; `tests/upstream-modifications.txt:1054-1055` records the inherited CMake and Patman changes.

## Verification evidence

Commands below were run from the worktree root except ctest commands, which were run from `build/tests`. The regression's RED assertion was `!allocationThrew` returning false with “truncated patch length reached the sample allocation.” The final GREEN run executed one test: `1/1 PatmanLoadTest Passed`.

| Stage | Exact command | Exit |
|---|---|---:|
| RED build, Patman fix reverted | `cmake --build build --target zene PatmanLoadTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-351.r1/logs/build-7.log 2>&1; echo EXIT=$?` | 0 |
| RED regression | `QT_QPA_PLATFORM=offscreen ctest -R '^PatmanLoadTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-351.r1/logs/test-5.log 2>&1; echo EXIT=$?` | 8 (expected failing assertion) |
| GREEN build, fix restored | `cmake --build build --target zene PatmanLoadTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-351.r1/logs/build-8.log 2>&1; echo EXIT=$?` | 0 |
| GREEN regression | `QT_QPA_PLATFORM=offscreen ctest -R '^PatmanLoadTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-351.r1/logs/test-6.log 2>&1; echo EXIT=$?` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-351.r1/logs/gate-3.log 2>&1; echo EXIT=$?` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-351.r1/logs/gate-4.log 2>&1; echo EXIT=$?` | 0 |

The required revert proof used the same product patch and regression:

```text
git diff -- plugins/Patman/Patman.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-351.r1/logs/fix.patch; echo EXIT=$?  -> 0
git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-351.r1/logs/fix.patch; echo EXIT=$?  -> 0
build-7 and test-5 above: 0 and 8 (RED)
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-351.r1/logs/fix.patch; echo EXIT=$?  -> 0
build-8 and test-6 above: 0 and 0 (GREEN)
```

## Files in this commit

- `plugins/Patman/Patman.cpp`
- `tests/src/plugins/PatmanLoadTest.cpp`
- `tests/CMakeLists.txt`
- `tests/fork-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-351.md`
