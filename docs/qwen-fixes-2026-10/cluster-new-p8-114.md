# Cluster new-p8-114 — out-of-range note fields

**Verdict: FIXED**

`src/core/DawProjectReadTracks.cpp:73` validates note channel, key, velocity and release velocity. The checks use the engine MIDI domains (`InstrumentMidiParts::Channels` and `NumKeys`) and the interchange model's normalized velocity range. At `:127`, invalid notes set a read error and are not appended to the clip model. The affected import test is `tests/src/core/DawProjectInterchangeRoundTripTest.cpp:1275`; it accepts boundary values and refuses low and high invalid values for each field.

Range evidence: `include/InstrumentMidiParts.h:63` declares 16 MIDI channels; `include/Note.h:92` declares 128 keys; `include/DawProjectInterchange.h:182` documents velocity and release velocity as 0..1; `docs/DAWPROJECT-INTERCHANGE.md:202` documents key 0..127 and vel/rel 0..1.

Build commands ran from the worktree root. CTest commands ran from `build/tests`. Logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-114.r1/logs`.

| Phase | Exact command | Exit | Evidence |
|---|---|---:|---|
| RED build | `cmake --build build --target zene DawProjectInterchangeRoundTripTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-114.r1/logs/build-1.log 2>&1; echo EXIT=$?` | 0 | Built current test against original reader. |
| RED test | `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-114.r1/logs/test-1.log 2>&1; echo EXIT=$?` | 8 | Failed because `channel="-1"` was accepted. |
| GREEN build | `cmake --build build --target zene DawProjectInterchangeRoundTripTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-114.r1/logs/build-2.log 2>&1; echo EXIT=$?` | 0 | Rebuilt app and affected test target. |
| GREEN test | `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-114.r1/logs/test-2.log 2>&1; echo EXIT=$?` | 0 | 1/1 registered CTest test passed. |
| Remove fix | `git diff -- src/core/DawProjectReadTracks.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-114.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-114.r1/logs/fix.patch; echo EXIT=$?` | 0 | Reversed only the production patch. |
| Revert-proof build | `cmake --build build --target zene DawProjectInterchangeRoundTripTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-114.r1/logs/build-3.log 2>&1; echo EXIT=$?` | 0 | Rebuilt with the fix removed. |
| Revert-proof test | `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-114.r1/logs/test-3.log 2>&1; echo EXIT=$?` | 8 | Failed again because `channel="-1"` was accepted. |
| Restore fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-114.r1/logs/fix.patch; echo EXIT=$?` | 0 | Restored the production patch. |
| Proof GREEN build | `cmake --build build --target zene DawProjectInterchangeRoundTripTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-114.r1/logs/build-4.log 2>&1; echo EXIT=$?` | 0 | Rebuilt with the fix restored. |
| Proof GREEN test | `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-114.r1/logs/test-4.log 2>&1; echo EXIT=$?` | 0 | 1/1 registered CTest test passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-114.r1/logs/gate-1.log 2>&1; echo EXIT=$?` | 0 | Passed. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-114.r1/logs/gate-2.log 2>&1; echo EXIT=$?` | 0 | Passed. |

Explicit commit paths:

- `src/core/DawProjectReadTracks.cpp`
- `tests/src/core/DawProjectInterchangeRoundTripTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-114.md`
