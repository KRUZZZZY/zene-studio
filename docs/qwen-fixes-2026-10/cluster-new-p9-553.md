# Cluster new-p9-553

**Verdict: FIXED**

## Evidence

- `include/ArrayVector.h:80` and `:87` now check sized constructors before constructing elements. The input range constructor at `:94` checks before dereferencing/constructing past capacity and destroys any partial construction if an iterator or element throws.
- `include/ArrayVector.h:149`, `:161`, `:173`, `:262`, `:273`, and `:322` apply the same capacity contract to initializer-list assignment, `assign`, both `insert` growth paths, and `emplace_back`. Operations that can now throw no longer promise `noexcept`.
- `tests/src/core/ArrayVectorTest.cpp:202`, `:219`, `:256`, `:402`, `:413`, `:423`, `:677`, `:708`, and `:836` cover oversized construction, assignment, insertion, and append. Throwing values and a single-pass iterator make the original implementation fail before any test writes outside the inline storage.
- `tests/upstream-modifications.txt:1054-1055` records both changed inherited files.

The original code was tested first: the ArrayVector family reported 39 passed and 10 failed. The reverse-patch proof repeated the same 39/10 result after removing only the production change. With the fix applied, the verbose run reported 49 passed, 0 failed; CTest ran 1/1 family target.

## Commands and exit codes

All build and test output was redirected to the numbered logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-553.r1/logs`.

| Purpose | Command | EXIT | Result |
|---|---|---:|---|
| Build regression before fix | `cmake --build build --target zene ArrayVectorTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-553.r1/logs/build-1.log 2>&1` | 0 | App and regression target built. |
| Original-code RED | `cd build/tests && ctest -R '^ArrayVectorTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-553.r1/logs/test-1.log 2>&1` | 8 | 39 passed, 10 failed. |
| Guarded aggregate build attempt | `cmake --build build --target all > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-553.r1/logs/build-2.log 2>&1` | 97 | `bunny-guard: target 'all' not allowed`; rebuilt explicit affected targets below. |
| Explicit affected targets, initial fixed build | `cmake --build build --target zene ArrayVectorTest AudioBufferTest PatcherCommandsTest RoutingGraphLiveTest RoutingGraphScheduleTest AudioPluginTest ClapEffectIntegrationTest RackTest SidechainInputsTest sf2player partc_ref_sf2player vocoder receiver compressor gate > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-553.r1/logs/build-3.log 2>&1` | 0 | Succeeded. |
| Fixed-code GREEN | `cd build/tests && ctest -R '^ArrayVectorTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-553.r1/logs/test-2.log 2>&1` | 0 | 1/1 CTest target passed. |
| Fixed-code count | `cd build/tests && ctest -R '^ArrayVectorTest$' --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-553.r1/logs/test-3.log 2>&1` | 0 | 49 Qt cases passed. |
| Save and reverse production patch | `git diff -- include/ArrayVector.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-553.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-553.r1/logs/fix.patch` | 0 | Production fix removed for proof. |
| Rebuild without fix | `cmake --build build --target zene ArrayVectorTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-553.r1/logs/build-4.log 2>&1` | 0 | Rebuilt before the proof test. |
| Reverse-patch RED | `cd build/tests && ctest -R '^ArrayVectorTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-553.r1/logs/test-4.log 2>&1` | 8 | Same 39 passed, 10 failed. |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-553.r1/logs/fix.patch` | 0 | Fix restored. |
| Final rebuild of affected targets | `cmake --build build --target zene ArrayVectorTest AudioBufferTest AudioPluginTest ClapEffectIntegrationTest PatcherCommandsTest RackTest RoutingGraphLiveTest RoutingGraphScheduleTest SidechainInputsTest partc_ref_sf2player clapeffect compressor gate receiver sf2player vocoder > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-553.r1/logs/build-5.log 2>&1` | 0 | Rebuilt app and all configured targets whose dependency files include `ArrayVector.h`. |
| Final GREEN | `cd build/tests && ctest -R '^ArrayVectorTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-553.r1/logs/test-5.log 2>&1` | 0 | 1/1 CTest target passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-553.r1/logs/gate-1.log 2>&1` | 0 | PASS. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-553.r1/logs/gate-2.log 2>&1` | 0 | PASS. |

The `fff grep` tool was not available in this session; each build/test log was searched with `rg` for `error`, `FAILED`, and `Assertion`.

## Files committed

- `include/ArrayVector.h`
- `tests/src/core/ArrayVectorTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-553.md`
