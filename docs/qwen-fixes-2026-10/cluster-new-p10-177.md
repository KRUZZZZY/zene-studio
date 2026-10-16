# Cluster new-p10-177

**Verdict: BLOCKED.** The MPE layer bug is fixed and the regression passes, including the required reverse-patch proof. The two required ratchet gates still fail on unrelated files/functions outside this change. Their check mode reports the violations in the logs below; I did not alter their baselines or unrelated code.

## Evidence

- [InstrumentTrack.cpp](../../src/tracks/InstrumentTrack.cpp#L434): when the owner consumes an MPE expression event, it now forwards that event to its live layer tracks before returning. The normal forwarding path remains in place for unconsumed events.
- [MpePlaybackTest.cpp](../../tests/src/core/MpePlaybackTest.cpp#L294): starts a live note on the owner and layer, sends pitch bend, channel pressure, and CC74, and checks all three values on both note handles.
- Before the fix, the layer handle reported `mpePitchCents() == 0` where the test expected `2400` (ctest exit 8). Reversing only the production patch produced the same failure. Restoring it passed.
- Final affected MPE family: 5/5 ctest cases passed (`MpeExpressionTest`, `MpeInputPathTest`, `MpeNoteStorageTest`, `MpePlaybackTest`, `MpeInputActionTest`).
- Final gate logs list complexity regressions in `XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`, `ControlCommandsMixerTools.cpp`, `ControlCommandsMixerStrip.cpp`, `ProjectRevisions.cpp`, and `ControlCommandsShortcuts.cpp`; the file-length gate reports 19 overlength files. Neither list names a file changed for this fix.

## Commands and exit codes

All build and test output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-177.r1/logs`.

| Evidence | Exact command | Exit |
| --- | --- | ---: |
| Initial original-code test build | `cmake --build build --target MpePlaybackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-177.r1/logs/build-1.log 2>&1; echo EXIT=$?` | 0 |
| Initial RED on original code | `ctest -R '^MpePlaybackTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-177.r1/logs/test-1.log 2>&1; echo EXIT=$?` | 8 |
| Fresh original-code build | `cmake --build build --target zene MpePlaybackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-177.r1/logs/build-2.log 2>&1; echo EXIT=$?` | 0 |
| RED on original code | `ctest -R '^MpePlaybackTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-177.r1/logs/test-2.log 2>&1; echo EXIT=$?` | 8 |
| Fixed build | `cmake --build build --target zene MpePlaybackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-177.r1/logs/build-3.log 2>&1; echo EXIT=$?` | 0 |
| GREEN | `ctest -R '^MpePlaybackTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-177.r1/logs/test-3.log 2>&1; echo EXIT=$?` | 0 |
| Save and reverse only the production patch | `git diff -- src/tracks/InstrumentTrack.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-177.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-177.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Reversed-patch build | `cmake --build build --target zene MpePlaybackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-177.r1/logs/build-4.log 2>&1; echo EXIT=$?` | 0 |
| RED with production patch reversed | `ctest -R '^MpePlaybackTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-177.r1/logs/test-4.log 2>&1; echo EXIT=$?` | 8 |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-177.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Restored-patch build | `cmake --build build --target zene MpePlaybackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-177.r1/logs/build-5.log 2>&1; echo EXIT=$?` | 0 |
| GREEN after restore | `ctest -R '^MpePlaybackTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-177.r1/logs/test-5.log 2>&1; echo EXIT=$?` | 0 |
| Build four MPE targets | `cmake --build build --target zene MpeExpressionTest MpeInputPathTest MpeNoteStorageTest MpePlaybackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-177.r1/logs/build-6.log 2>&1; echo EXIT=$?` | 0 |
| First family run (four passed; `MpeInputActionTest` executable was absent) | `ctest -R '^Mpe.*Test$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-177.r1/logs/test-6.log 2>&1; echo EXIT=$?` | 8 |
| Build missing family target | `cmake --build build --target zene MpeInputActionTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-177.r1/logs/build-7.log 2>&1; echo EXIT=$?` | 0 |
| Final family run, 5/5 passed | `ctest -R '^Mpe.*Test$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-177.r1/logs/test-7.log 2>&1; echo EXIT=$?` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-177.r1/logs/gate-3.log 2>&1; echo EXIT=$?` | 1 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-177.r1/logs/gate-4.log 2>&1; echo EXIT=$?` | 1 |

## Files in this commit

- `src/tracks/InstrumentTrack.cpp`
- `tests/src/core/MpePlaybackTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-177.md`
