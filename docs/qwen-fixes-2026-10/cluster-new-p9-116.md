# Cluster new-p9-116: Random LFO initialization

**Verdict: FIXED**

The finding was real. `m_random` was read on the first Random LFO block even when playback was at a nonzero phase, before any phase-zero update had assigned it. The member now has a defined initial value, and the Random branch generates its first sample before reading it. Later cycle-boundary refreshes retain their existing behavior.

## Evidence

- [include/EnvelopeAndLfoParameters.h:201](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-116/include/EnvelopeAndLfoParameters.h:201): initializes the held sample and its first-generation flag.
- [src/core/EnvelopeAndLfoParameters.cpp:230](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-116/src/core/EnvelopeAndLfoParameters.cpp:230): generates the first random value when rendering Random at any phase; subsequent values still refresh at phase zero.
- [tests/src/core/ModulationLayerTest.cpp:406](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-116/tests/src/core/ModulationLayerTest.cpp:406): poisons storage, advances playback, selects Random off phase zero, and checks the rendered sample is finite.
- `tests/upstream-modifications.txt:1054-1055` records the two inherited production files and their reasons.

Affected family: `ctest -R ModulationLayerTest` ran **1 CTest target**. The Qt test binary has 11 test functions; the RED runs reported 10 passed and the regression failed, while the GREEN CTest runs passed.

## RED/GREEN and patch-removal proof

All build and test logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-116.r1/logs`.

| Stage | Exact command | Exit | Result |
|---|---|---:|---|
| Original-code build (`build-1.log`) | `cmake --build build --target zene ModulationLayerTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-116.r1/logs/build-1.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-116.r1/logs/build-1.log; exit "$status"` | 0 | Built `zene` and test target before RED. |
| RED (`test-1.log`) | `ctest -R ModulationLayerTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-116.r1/logs/test-1.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-116.r1/logs/test-1.log; exit "$status"` | 8 | Regression failed: non-finite output; 10 other test functions passed. |
| Fixed build (`build-2.log`) | `cmake --build build --target zene ModulationLayerTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-116.r1/logs/build-2.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-116.r1/logs/build-2.log; exit "$status"` | 0 | Rebuilt `zene` and test target. |
| GREEN (`test-2.log`) | `ctest -R ModulationLayerTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-116.r1/logs/test-2.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-116.r1/logs/test-2.log; exit "$status"` | 0 | 1/1 CTest target passed. |
| Save and reverse production patch | `git diff -- include/EnvelopeAndLfoParameters.h src/core/EnvelopeAndLfoParameters.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-116.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-116.r1/logs/fix.patch` | 0 | Temporarily removed only the production fix. |
| Reverse-patch build (`build-3.log`) | `cmake --build build --target zene ModulationLayerTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-116.r1/logs/build-3.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-116.r1/logs/build-3.log; exit "$status"` | 0 | Rebuilt before the negative control. |
| Negative-control RED (`test-3.log`) | `ctest -R ModulationLayerTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-116.r1/logs/test-3.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-116.r1/logs/test-3.log; exit "$status"` | 8 | The same regression failed again; 10 other functions passed. |
| Reapply production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-116.r1/logs/fix.patch` | 0 | Restored the fix. |
| Final build (`build-4.log`) | `cmake --build build --target zene ModulationLayerTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-116.r1/logs/build-4.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-116.r1/logs/build-4.log; exit "$status"` | 0 | Rebuilt `zene` and test target. |
| Final GREEN (`test-4.log`) | `ctest -R ModulationLayerTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-116.r1/logs/test-4.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-116.r1/logs/test-4.log; exit "$status"` | 0 | 1/1 CTest target passed. |

## Ratchet gates

| Gate | Exact command | Exit |
|---|---|---:|
| Complexity (`gate-1.log`) | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-116.r1/logs/gate-1.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-116.r1/logs/gate-1.log; exit "$status"` | 0 |
| File length (`gate-2.log`) | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-116.r1/logs/gate-2.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-116.r1/logs/gate-2.log; exit "$status"` | 0 |

## Files committed

- `include/EnvelopeAndLfoParameters.h`
- `src/core/EnvelopeAndLfoParameters.cpp`
- `tests/src/core/ModulationLayerTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-116.md`
