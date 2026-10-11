# Cluster new-p9-294

Verdict: **FIXED**

`MonoDelay::setSampleRate()` reallocated the ring but left `m_maxLength` and
`m_length` at their old sample-rate values. `tick()` uses those values for its
ring modulo and read wrap, so a 48 kHz to 44.1 kHz change left a 96,000-frame
ring bound on an 88,200-sample allocation.

## Evidence

- [MonoDelay.cpp:59](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-294/plugins/Flanger/MonoDelay.cpp:59) uses `m_maxLength` for `tick()` indexing.
- [MonoDelay.cpp:77](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-294/plugins/Flanger/MonoDelay.cpp:77) recomputes the allocation bound. Line 80 scales the delay length, lines 86 and 87 update the bound and reset the write index, and line 89 allocates using that bound.
- [PluginPortsMigrationTest.cpp:442](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-294/tests/src/plugins/PluginPortsMigrationTest.cpp:442) exercises 48 kHz to 44.1 kHz and checks the updated bound, scaled delay, and reset index before a stale bound can cause an out-of-bounds tick.
- Before the fix, the new assertion failed: actual `m_maxLength` was 96000; expected 88200.

## Verification

Commands below were run in the worktree unless the working directory is stated. CTest commands ran from `build/tests`.

| Step | Exact command | Exit |
| --- | --- | ---: |
| Create product-only proof patch | `git diff -- plugins/Flanger/MonoDelay.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-294.r1/logs/fix.patch` | 0 |
| Reverse production fix for RED proof | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-294.r1/logs/fix.patch` | 0 |
| RED build | `cmake --build build --target zene flanger PluginPortsMigrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-294.r1/logs/build-proof-red-2.log 2>&1` | 0 |
| RED test | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-294.r1/logs/test-proof-red-2.log 2>&1` | 8 |
| Restore production fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-294.r1/logs/fix.patch` | 0 |
| GREEN build | `cmake --build build --target zene flanger PluginPortsMigrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-294.r1/logs/build-proof-green-2.log 2>&1` | 0 |
| GREEN test | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-294.r1/logs/test-proof-green-2.log 2>&1` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-294.r1/logs/complexity-gate-2.log 2>&1` | 0 |
| File length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-294.r1/logs/file-length-gate-2.log 2>&1` | 0 |

CTest matched 1 affected-family test. The RED QtTest run reported 8 passed and
the new regression failed. Both required gates passed. No CMake reconfiguration
was run.

## Files committed

- `plugins/Flanger/MonoDelay.cpp`
- `tests/src/plugins/PluginPortsMigrationTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-294.md`
