# Cluster new-p12-142-port: malformed mixer send targets

**Verdict: FIXED** (port of `43e9a1ea5` from `bugs/qwen-new-p12-142`, re-applied by hand onto `050/m0-train-5`).

**Gate status: both ratchet gates are RED, and the failures predate this change.** They fail with and without this change, with identical regression lists (see Gates below). They are not caused by this fix and were not "fixed" here, because the failing items are in unrelated files and baselines must not be re-anchored.

## Finding

`Mixer::loadSettings` read the `channel` attribute of `<send>` and `<sidechain-send>` with a bare `toInt()`. A missing or non-numeric attribute returns 0, which is the master's index, so the malformed element was routed to master. The earlier negative-index guard (`sendTo < 0`) did not cover this case.

Pre-fix evidence on this line (`b02a0ff2b`, the file before my edit):

- `src/core/Mixer.cpp:2682` `int sendTo = chDataItem.attribute( "channel" ).toInt();` (regular send)
- `src/core/Mixer.cpp:2713` `int sendTo = chDataItem.attribute( "channel" ).toInt();` (sidechain send)

## Fix

`src/core/Mixer.cpp` (loadSettings, both send branches): `toInt( &sendToOk )`, and `continue` when `!sendToOk`. The existing negative-target guard is kept as it was. Load-only; no audio-path code changed, so no realtime rule is touched.

- regular send: `src/core/Mixer.cpp:2682-2686`
- sidechain send: `src/core/Mixer.cpp:2716-2725`

## Regression

`tests/src/core/MixerRoutingBackwardCompatTest.cpp`, new slot
`aMalformedSendTargetIsSkippedNotRoutedToMaster()`. The fixture has three malformed regular sends (`channel="bad"`, `channel="2x"`, no attribute), two malformed sidechain sends (`channel="bad"`, no attribute), and one well-formed send and sidechain send to channel 2 as a control. The test asserts that channel 1 ends with exactly one regular and one sidechain route. The file is already registered in `tests/CMakeLists.txt` and `tests/all-sources.txt`; no new file was added, so `tests/fork-sources.txt` is unchanged.

## Verification evidence

All commands ran from the worktree root, or from `build/tests` for ctest. The `ccache` temp directory was set to `/tmp` (`export CCACHE_TEMPDIR=/tmp/ccache-tmp-new-p12-142`), because the default `/run/user/1000` path is not writable here. Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-142-port.r1/logs`.

| # | Step | Exact command | EXIT | Result |
|---|---|---|---:|---|
| 1 | Build test, first attempt | `cmake --build build --target MixerRoutingBackwardCompatTest > build-1.log 2>&1` | 2 | ccache could not create temp file under `/run/user/1000` (environment, not code). Not evidence. |
| 2 | Build test, ccache in /tmp, pre-fix | `cmake --build build --target MixerRoutingBackwardCompatTest > build-2.log 2>&1` | 0 | Built |
| 3 | **RED** (pre-fix) | `cd build/tests && ctest -R '^MixerRoutingBackwardCompatTest$' --output-on-failure > test-2.log 2>&1` | **8** | `FAIL! ... aMalformedSendTargetIsSkippedNotRoutedToMaster() 'm_sends.size() == 1' returned FALSE. (2 regular sends survived; malformed targets were routed to master)`; 15 passed, 1 failed |
| 4 | Apply fix | (edit `Mixer.cpp` lines 2682-2686, 2716-2725) | - | - |
| 5 | Build zene + test | `cmake --build build --target zene MixerRoutingBackwardCompatTest > build-3.log 2>&1` | 0 | Built, no errors |
| 6 | **GREEN** (fixed) | `cd build/tests && ctest -R '^MixerRoutingBackwardCompatTest$' --output-on-failure > test-3.log 2>&1` | **0** | 100% passed, 1/1 ctest test; `-V` run (test-5.log): 16 passed, 0 failed |
| 7 | Negative control: save fix | `git diff -- src/core/Mixer.cpp > fix.patch` | 0 | Patch saved |
| 8 | Negative control: revert fix | `git apply -R fix.patch` | 0 | Production change removed |
| 9 | Rebuild without fix | `cmake --build build --target zene MixerRoutingBackwardCompatTest > build-4.log 2>&1` | 0 | Built |
| 10 | **RED** without fix (proof) | `cd build/tests && ctest -R '^MixerRoutingBackwardCompatTest$' -V > test-4.log 2>&1` | **8** | `FAIL! ... aMalformedSendTargetIsSkippedNotRoutedToMaster() ... (2 regular sends survived; ...)`; 15 passed, 1 failed |
| 11 | Re-apply fix | `git apply fix.patch` | 0 | Fix restored |
| 12 | Rebuild with fix | `cmake --build build --target zene MixerRoutingBackwardCompatTest > build-5.log 2>&1` | 0 | Built |
| 13 | **GREEN** with fix (proof) | `cd build/tests && ctest -R '^MixerRoutingBackwardCompatTest$' -V > test-5.log 2>&1` | **0** | `PASS : ...aMalformedSendTargetIsSkippedNotRoutedToMaster()`; 16 passed, 0 failed |
| 14 | Affected family build | `cmake --build build --target MixerListenTest MixerAbRegressionTest MixerChannelPanTest MixerStripTest MixerConcurrencyTest MixingRoutingTest > build-6.log 2>&1` | 0 | Built |
| 15 | Affected family run | `cd build/tests && ctest -R '^(MixerListenTest\|MixerAbRegressionTest\|MixerChannelPanTest\|MixerRoutingBackwardCompatTest\|MixerStripTest\|MixerConcurrencyTest\|MixingRoutingTest)$' > test-6.log 2>&1` | 0 | **7 of 7** passed (tests run: 7 ctest tests) |

Notes on the table:

- Row 3 is the valid RED. The earlier run `test-1.log` was not used: its binary was built before the new test existed and is not evidence.
- The fixture produces two surviving regular routes in the RED case rather than four. `createChannelSend` seems to merge routes with the same target, so `bad`, `2x`, and the missing attribute all collapse onto one master route, and the valid send to channel 2 is the second. The assertion message is accurate for that outcome.

### Gates

| Gate | Command | EXIT (final tree) | EXIT (all three edits reverted) |
|---|---|---:|---:|
| complexity | `bash tests/complexity-gate.sh --check > gate-1.log 2>&1` (control: `gate-3.log`) | **1** | **1** |
| file length | `bash tests/file-length-gate.sh --check > gate-2.log 2>&1` (control: `gate-4.log`) | **1** | **1** |

- Complexity: `68 functions exceed CCN 10` on the final tree and in the control. The `REGRESSION:` lines are identical in both logs (6 lines). None names `src/core/Mixer.cpp`, `loadSettings`, or the test file. The regressed functions are in `XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`, `ControlCommandsMixerTools.cpp`, `ControlCommandsMixerStrip.cpp`, `ProjectRevisions.cpp`, and `ControlCommandsShortcuts.cpp`.
- File length: `19 exceed 500 code lines` on the final tree and in the control. The `REGRESSION:` lines are identical (14 lines). None names `Mixer.cpp` or the test file. The regressed files include `TakeLane.cpp`, `DeviceDspTest.cpp`, `NotePlaybackTest.cpp`, and others that this line has not re-anchored.
- Conclusion: the committed baselines on `050/m0-train-5` (and so on this branch) do not match the current tree. Fixing that is out of scope for this one bug, and baselines must not be changed, so both gates stay red and are reported here.

## Files committed

- `src/core/Mixer.cpp` (send and sidechain-send parse check)
- `tests/src/core/MixerRoutingBackwardCompatTest.cpp` (regression slot)
- `tests/upstream-modifications.txt` (reason appended to the last `src/core/Mixer.cpp` entry; `Mixer.cpp` is an inherited file)
- `docs/qwen-fixes-2026-10/cluster-new-p12-142-port.md` (this report)

## Not done

- `tests/fork-sources.txt` has no entry for `MixerRoutingBackwardCompatTest.cpp`, which the earlier `43e9a1ea5` added. The file is already in `tests/all-sources.txt` and `tests/CMakeLists.txt` on this line, and this change does not add a file, so no registration was made. This is noted, not verified against `fork-sources-gate.sh`.
