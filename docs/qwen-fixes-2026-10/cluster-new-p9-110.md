# Cluster new-p9-110

Verdict: **FIXED**

## Evidence

`include/Delay.h:136-139` and `include/Delay.h:208-211` are the `CombFeedfwd` and
`CombFeedbackDualtap` declarations; this fix adds `public:` so clients can construct the stereo
aliases and call their delay operations. The aliases are at `include/Delay.h:364-365`. The regression in
`tests/src/core/AudioResamplerRatioTest.cpp:172-190` constructs both aliases, configures
their delay and gain, processes samples, clears history, and grows the delay buffer.

The first RED build diagnosed the private constructors and methods. It also exposed invalid
constructor initializer names in `Delay.h` (`m_feedBack`, and the dual tap's `m_delay` and
`m_fraction`), including in the neighboring comb and allpass classes. Those initializer lists
now name the declared gain and delay fields, initializing them to zero so the header compiles.

The patrol's suggested `MonoDelay`, `StereoDelay`, and `DelayControls` names are not registered
CTest tests in this build tree. The regression was added to the existing registered
`AudioResamplerRatioTest`; CTest ran 1 test, containing 7 test slots and 9 passing QtTest entries
including setup and cleanup. No fff MCP search tool was available in this session, so `rg` was
used for source and log searches.

| Evidence | Exact command | Exit | Result |
|---|---|---:|---|
| RED before fix | `cmake --build build --target zene AudioResamplerRatioTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-110.r1/logs/build-1.log 2>&1` | 2 | Compile failed on private API access (and the invalid initializers). |
| GREEN build | `cmake --build build --target zene AudioResamplerRatioTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-110.r1/logs/build-2.log 2>&1` | 0 | `zene` and the affected test target built. |
| GREEN test | `ctest -R '^AudioResamplerRatioTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-110.r1/logs/test-1.log 2>&1` (cwd: `build/tests`) | 0 | 1/1 CTest passed. |
| Reverse production patch | `git diff -- include/Delay.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-110.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-110.r1/logs/fix.patch` | 0 | Removed only the production fix for the proof run. |
| RED with patch reversed | `cmake --build build --target zene AudioResamplerRatioTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-110.r1/logs/build-3.log 2>&1` | 2 | Regression again failed on private constructors and methods. |
| Reapply production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-110.r1/logs/fix.patch` | 0 | Restored the production fix. |
| GREEN proof build | `cmake --build build --target zene AudioResamplerRatioTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-110.r1/logs/build-4.log 2>&1` | 0 | `zene` and the affected test target rebuilt. |
| GREEN proof test | `ctest -R '^AudioResamplerRatioTest$' --output-on-failure --verbose > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-110.r1/logs/test-2.log 2>&1` (cwd: `build/tests`) | 0 | 1/1 CTest passed; QtTest reported 9 passed, 0 failed. |
| Inherited-file registration | `bash tests/no-upstream-regression-gate.sh > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-110.r1/logs/registration-1.log 2>&1` | 0 | All inherited changes are declared. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-110.r1/logs/complexity-gate-final.log 2>&1` | 0 | No regressions. |
| File length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-110.r1/logs/file-length-gate-final.log 2>&1` | 0 | No regressions. |

## Files committed

- `include/Delay.h`
- `tests/src/core/AudioResamplerRatioTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-110.md`
