# Cluster new-p8-117: mixer sharing loss count

**Verdict: FIXED**

## Evidence

- Before the fix, `src/core/DawProjectSession.cpp:261-263, 293-295, 355-362` incremented `mixerSharingLost` for every additional track on a mixer channel. The same conversion already assigned each track that channel's ID at lines 284-292.
- The fixed extraction keeps that shared IDREF at `src/core/DawProjectSession.cpp:260-291` and no longer counts repeated channel assignments. The writer resolves the IDREF and writes `destination` at `src/core/DawProjectWrite.cpp:397-418, 449-452`; the reader retains it at `src/core/DawProjectReadTracks.cpp:343-350`, and the session importer resolves it to the channel at `src/core/DawProjectSession.cpp:528-544`.
- `tests/src/core/DawProjectInterchangeRoundTripTest.cpp:398-428` creates two instrument tracks targeting `mixer0`, checks that write/read retains both references, and requires `mixerSharingLost == 0`.
- The loss field documentation and DAWproject interchange/release descriptions now reflect that multiple tracks may share a destination channel.

## Verification

Commands below ran in the worktree named in the task. Build and test output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-117.r1/logs/`.

| Step | Command | EXIT | Result |
|---|---|---:|---|
| Initial regression build | `cmake --build build --target DawProjectInterchangeRoundTripTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-117.r1/logs/build-1.log 2>&1` | 0 | Built the new regression against the original implementation. |
| Initial RED | `cd build/tests && ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-117.r1/logs/test-1.log 2>&1` | 8 | `loss.mixerSharingLost` was 1; 34 passed and 1 failed. |
| Reverse-patch RED build | `cmake --build build --target zene DawProjectInterchangeRoundTripTest ProjectContainerTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-117.r1/logs/build-7.log 2>&1` | 0 | Rebuilt `zene` and both test targets with the product fix reversed. |
| Reverse-patch RED | `cd build/tests && ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-117.r1/logs/test-6.log 2>&1` | 8 | The final regression again failed specifically with `loss.mixerSharingLost == 1`. |
| Final build | `cmake --build build --target zene DawProjectInterchangeRoundTripTest ProjectContainerTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-117.r1/logs/build-8.log 2>&1` | 0 | Rebuilt after reapplying the fix. |
| Final GREEN | `cd build/tests && ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-117.r1/logs/test-8.log 2>&1` | 0 | The DAWproject test passed. |
| Header consumer test | `cd build/tests && ctest -R ProjectContainerTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-117.r1/logs/test-9.log 2>&1` | 0 | The other test target that includes the changed header passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-117.r1/logs/gate-3.log 2>&1` | 0 | Passed; baseline unchanged. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-117.r1/logs/gate-4.log 2>&1` | 0 | Passed; baseline unchanged. |

Affected-family count: 2 CTest tests executed (`DawProjectInterchangeRoundTripTest` and `ProjectContainerTest`); both passed.

The reverse-patch proof used `git diff -- src/core/DawProjectSession.cpp src/core/DawProjectModel.cpp src/core/DawProjectWrite.cpp include/DawProjectInterchange.h docs/DAWPROJECT-INTERCHANGE.md docs/RELEASE-NOTES-v0.3.0-alpha.md > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-117.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-117.r1/logs/fix.patch` (EXIT=0), followed by `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-117.r1/logs/fix.patch` (EXIT=0).

## Files committed

- `docs/DAWPROJECT-INTERCHANGE.md`
- `docs/RELEASE-NOTES-v0.3.0-alpha.md`
- `include/DawProjectInterchange.h`
- `src/core/DawProjectModel.cpp`
- `src/core/DawProjectSession.cpp`
- `src/core/DawProjectWrite.cpp`
- `tests/src/core/DawProjectInterchangeRoundTripTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-117.md`
