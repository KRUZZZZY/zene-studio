Verdict: FIXED
new-p17-148: FIXED
3434: ALREADY-FIXED
new-p14-240: FIXED

# Cluster new-p17-148 (same-file batch in plugins/VstBase/RemoteVstPlugin.cpp)

One commit per fixed bug; the batch report carries all three verdicts.

## Verdicts and decisions

| cluster | verdict | one-line reason |
| --- | --- | --- |
| new-p17-148 | FIXED | `process()` early returns left `_out` untouched; now silenced with the cached count |
| 3434 | ALREADY-FIXED | no `stable_sort` on the period path; `vst::sortByDeltaFrames` is already in place and allocation-free |
| new-p14-240 | FIXED | `process()` used function-local `static` dispatch scratch shared by all instances; now per-instance members |

### new-p17-148: REAL, fixed

Evidence before the fix: `plugins/VstBase/RemoteVstPlugin.cpp:1100-1109` returned on `!tryLockShm()` and on `!isShmValid()` without touching `_out`, while the success path at `1132-1136` memsets every output plane. `RemotePluginClient::doProcessing` (`include/RemotePluginClient.h:363-364`) reuses the same shared block each period, so a reused non-zero block passed stale samples downstream on those paths. The fix calls `vst::silenceOutputPlanes(_out, RemotePluginClient::outputCount(), bufferSize())` on both early returns (`RemoteVstPlugin.cpp:1106,1114`), using the cached count (`RemotePluginClient::outputCount()`), not the live `outputCount()` override which can exceed the block after a refused `audioMasterIOChanged` (see the comment at `1117-1124`). The helper is `plugins/VstBase/VstMidiEventBatch.h:94` and is a plain `memset` per plane: no allocation, no lock, no log.

### 3434: ALREADY-FIXED

No product change. The finding (`std::stable_sort` allocates on the period path) was fixed by `ca5b95981d fix(vst-client): cap the per-period MIDI queue and sort it without allocating (cluster 3433, 3434)`. On this branch the period path uses `vst::sortByDeltaFrames` (`RemoteVstPlugin.cpp:1082`, header `VstMidiEventBatch.h:47`), an in-place stable insertion sort documented as allocation-free; a search for `stable_sort` in `RemoteVstPlugin.cpp` returns no matches. The existing slots `sortAllocatesNothing` and `controlStableSortAllocates` prove the helper property and that the probe sees `std::stable_sort`'s buffer. Independent QA added `processUsesAllocationFreeMidiSort` to pin the production call site; see the QA supplement below.

### new-p14-240: REAL, fixed

Evidence before the fix: `RemoteVstPlugin.cpp:1071-1072` declared `static char eventsBuffer[...]` and `static VstMidiEvent vme[...]` inside `process()`, shared by every `RemoteVstPlugin` instance. Parallel processing could make one instance's dispatch overwrite the other's events while the other was dispatching. The fix adds per-instance members `m_eventsBuffer` / `m_midiScratch` (`RemoteVstPlugin.cpp:493-494`) and `process()` now uses them (`1088,1094-1095`). The members outlive the synchronous `pluginDispatch(effProcessEvents, ...)` that reads them, and cap is unchanged (`MIDI_EVENT_BUFFER_COUNT`, clamped again at `1086`). No new clock, slot, or host change.

## Regression tests (`tests/src/core/VstMidiEventBatchTest.cpp`)

The client is an out-of-process executable that cannot run in-process, so the helpers are pinned on `VstMidiEventBatch.h` plus source checks on `RemoteVstPlugin.cpp` (the same seam as `VstChannelCountTest` and the `ca5b95981d` batch). All new slots are deterministic: fixed vectors, no sleeps, no threads.

| bug | test | what it checks |
| --- | --- | --- |
| new-p17-148 | `silenceZeroesOutputPlanes` | 3 planes of 96 floats filled with `7.0f` are zeroed; probe shows 0 allocations |
| new-p17-148 | `silenceIgnoresDegenerateInputs` | null, non-positive count, zero frames touch nothing; only requested planes are zeroed (tail stays `9.0f`) |
| new-p17-148 | `processEarlyReturnsSilenceOutputs` | both early returns call `silenceOutputPlanes` with `RemotePluginClient::outputCount` |
| new-p14-240 | `midiScratchIsPerInstance` | two per-instance scratch buffers alias nothing and are independent; `process()` has no `static char eventsBuffer` / `static VstMidiEvent vme` |
| 3434 | existing `sortIsChronologicalAndStable`, `sortAllocatesNothing`, `controlStableSortAllocates`, `queueCapsAndCountsDrops`; `processUsesAllocationFreeMidiSort` | helper properties plus the `process()` call site |

## Evidence

Logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-148.r1/logs`. Builds run from the worktree; every `ctest` runs from `build/tests` (guard: `-R VstMidiEventBatchTest --output-on-failure`).

| bug | step | command | EXIT | log |
| --- | --- | --- | --- | --- |
| new-p17-148 | RED (test before fix) | `cmake --build build --target VstMidiEventBatchTest` | 0 | `build-1.log` |
| new-p17-148 | RED | `ctest -R VstMidiEventBatchTest --output-on-failure` | 8 | `test-1.log`: `FAIL! processEarlyReturnsSilenceOutputs() 'body.count(silenceOutputPlanes) >= 2' returned FALSE` |
| new-p17-148 | GREEN | `cmake --build build --target VstMidiEventBatchTest` | 0 | `build-2.log`, `build-5-green2.log` |
| new-p17-148 | GREEN | `ctest -R VstMidiEventBatchTest --output-on-failure` | 0 | `test-2.log`, `test-green-silence2.log`; `./VstMidiEventBatchTest` `test-2b.log`: 9 passed, 0 failed |
| new-p17-148 | 4b RED proof | `git diff -- plugins/VstBase/RemoteVstPlugin.cpp > fix-silence-integration.patch` / `git apply -R fix-silence-integration.patch` / `cmake --build build --target VstMidiEventBatchTest` | 0 / 0 / 0 | `fix-silence-integration.patch`, `build-4-red.log` |
| new-p17-148 | 4b RED proof | `ctest -R VstMidiEventBatchTest --output-on-failure` (fix reversed) | 8 | `test-red-silence.log`: same `processEarlyReturnsSilenceOutputs` FAIL, 8 passed 1 failed |
| new-p17-148 | 4b GREEN proof | `git apply fix-silence-integration.patch` / rebuild / ctest | 0 / 0 / 0 | `test-green-silence2.log`: 1/1 passed |
| new-p17-148 | client compile | `cmake --build build --target NativeLinuxRemoteVstPlugin64` | 0 | `build-3.log` |
| new-p14-240 | RED (test before fix) | `cmake --build build --target VstMidiEventBatchTest` | 0 | `build-6.log` |
| new-p14-240 | RED | `ctest -R VstMidiEventBatchTest --output-on-failure` | 8 | `test-red-scratch2.log`: `FAIL! midiScratchIsPerInstance() '!body.contains(static char eventsBuffer)' returned FALSE` |
| new-p14-240 | GREEN | `cmake --build build --target VstMidiEventBatchTest` | 0 | `build-7.log`, `build-green-scratch.log` |
| new-p14-240 | GREEN | `ctest -R VstMidiEventBatchTest --output-on-failure` | 0 | `test-green-scratch.log`, `test-green-scratch-proof.log` |
| new-p14-240 | GREEN (verbose) | `./VstMidiEventBatchTest` | 0 | `test-green-scratch-verbose.log`: 10 passed, 0 failed |
| new-p14-240 | 4b RED proof | `git diff -- plugins/VstBase/RemoteVstPlugin.cpp > fix-scratch.patch` / `git apply -R fix-scratch.patch` / rebuild | 0 / 0 / 0 | `fix-scratch.patch`, `build-red-scratch.log` |
| new-p14-240 | 4b RED proof | `ctest -R VstMidiEventBatchTest --output-on-failure` (fix reversed) | 8 | `test-red-scratch-proof.log`: `midiScratchIsPerInstance` FAIL, 9 passed 1 failed |
| new-p14-240 | 4b GREEN proof | `git apply fix-scratch.patch` / rebuild / ctest | 0 / 0 / 0 | `test-green-scratch-proof.log`: 1/1 passed |
| new-p14-240 | client compile | `cmake --build build --target NativeLinuxRemoteVstPlugin64` | 0 | `build-8.log` |
| batch | `zene` rebuild | `cmake --build build --target zene` | 0 | `build-zene-final.log` |
| batch | gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 | `feature-check-silence.log`, `feature-check-final.log`: `RESULT: PASS` |

Affected family: `ctest -R VstMidiEventBatchTest` (1 ctest entry, 11 QtTest results including init/cleanup). After the batch and QA regression: 11 passed, 0 failed.

### Independent QA supplement (2026-11-06)

The helper tests above did not detect a regression that changed only `RemoteVstPlugin::process()` back to `std::stable_sort`; the focused CTest still passed with that mutation (EXIT 0). Added `processUsesAllocationFreeMidiSort` to pin the client call site to the tested helper. With the process call temporarily changed to `std::stable_sort`, `cmake --build build --target zene VstMidiEventBatchTest NativeLinuxRemoteVstPlugin64 -j12` exited 0 and `ctest -R VstMidiEventBatchTest --output-on-failure` exited 8 at that assertion (`qa-review-test-3434-red-1.log`). Restored the branch product file with `git checkout -- plugins/VstBase/RemoteVstPlugin.cpp`, rebuilt the same targets (EXIT 0), and reran the same CTest (EXIT 0; one CTest entry, 11 QtTest results: 11 passed, 0 failed). The independently rerun feature gate exited 0; complexity and file-length reported no new item in the touched files (`qa-review-feature-check-1.log`). Full command logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-148.r1/logs/`.

### Commits

- `91b9bdf2e3 fix(cluster new-p17-148): silence output planes on process() early returns` — `plugins/VstBase/VstMidiEventBatch.h`, `plugins/VstBase/RemoteVstPlugin.cpp`, `tests/src/core/VstMidiEventBatchTest.cpp`, `tests/upstream-modifications.txt`.
- `4edb232afb fix(cluster new-p14-240): give each VST instance its own MIDI dispatch scratch` — `plugins/VstBase/RemoteVstPlugin.cpp`, `tests/src/core/VstMidiEventBatchTest.cpp`, `tests/upstream-modifications.txt`. (3434 has no commit: nothing to change.)

Registration: no new files. `VstMidiEventBatch.h` and the test were already in `tests/fork-sources.txt` and `tests/all-sources.txt`. `RemoteVstPlugin.cpp` is an inherited file; its line in `tests/upstream-modifications.txt` (`:776`) now names both fixes and still ends with a newline.

## Self-check

### The regression test FAILS with your product change reverted and PASSES with it (exit codes).

- new-p17-148: reverted the `RemoteVstPlugin.cpp` hunk via `fix-silence-integration.patch` and ran `ctest -R VstMidiEventBatchTest --output-on-failure` from `build/tests`: EXIT 8, `test-red-silence.log` (1 failed: `processEarlyReturnsSilenceOutputs`). Restored with `git apply fix-silence-integration.patch` and the same ctest: EXIT 0, `test-green-silence2.log` (1/1 passed, 9 functions). The pre-fix RED was `test-1.log` EXIT 8 with the same failing assertion.
- new-p14-240: reverted the `RemoteVstPlugin.cpp` hunk via `fix-scratch.patch` (`git apply -R fix-scratch.patch`), rebuilt, `ctest -R VstMidiEventBatchTest --output-on-failure`: EXIT 8, `test-red-scratch-proof.log` (1 failed: `midiScratchIsPerInstance`). Restored and the same ctest: EXIT 0, `test-green-scratch-proof.log` (1/1 passed, 10 functions). The pre-fix RED was `test-red-scratch2.log` EXIT 8.
- 3434: ALREADY-FIXED, no batch product change to revert; `processUsesAllocationFreeMidiSort` fails with the processing call replaced by `std::stable_sort` (EXIT 8), then passes after restoring the branch source (EXIT 0).

### The test is deterministic: no wall-clock timing, sleeps or races (name the seam or injected values it uses).

- Deterministic. Sort/queue uses a fixed LCG (`makeEvents`, seed `12345`, 1024 events). Silence uses a fixed 96-frame block filled with `7.0f` (and `5.0f`/`9.0f` sentinels). Scratch uses two fixed local structs with `{} ` init and a fixed tag `99`. The integration slots read `RemoteVstPlugin.cpp` text from the worktree (relative paths `plugins/...`, `../../plugins/...`, `../../../plugins/...`) and assert on exact substrings; no timing, no sleeps, no threads, no shared-memory, no plugin binary.

### The fix is at the cause and complete: every caller and sibling of what you changed was checked and stays consistent.

- new-p17-148: `process()` has exactly two early returns (`tryLockShm`, `isShmValid`), both now call `silenceOutputPlanes` with the cached count. Sibling check: the success path (`1136-1140`) still memsets the same cached planes, so the normal path is unchanged; `_out == nullptr`, count `<= 0` and `bufferSize() == 0` are no-ops in the helper and in `doProcessing`'s size check. The only caller of `process()` is `RemotePluginClient::doProcessing` (`include/RemotePluginClient.h:363-364`); no other call site reuses `_out` differently. `processMidiEvent`/`pluginDispatch(effProcessEvents)` run before the early returns as before.
- new-p14-240: the two `static` buffers were referenced only inside the `if (m_midiEvents.size())` block of `process()` (`1071-1095`). Both are now members (`m_eventsBuffer`/`m_midiScratch`) with identical sizes (`sizeof(VstEvents) + sizeof(VstMidiEvent*)*1024` and `1024` events) and the same cap (`std::min` at `1086`). Sibling check: `processMidiEvent`'s stack-local `VstMidiEvent vme` (`1195` area) is untouched and never shared; `m_midiEvents` reserve/cap logic (`VstMidiEventBatch.h` `appendBounded`) is unchanged. No other `static` buffer remains in `process()`.
- 3434: sibling checked — `process()` still calls `vst::sortByDeltaFrames` once per period; nothing else sorts MIDI.

### No collateral damage: old projects and saved data still load as before, and any limit or check you added does not hit other callers that legitimately exceed it.

- No saved-data, project, or JNI format changes. No new limit: the silence count is the cached count the host already sized the block for (same count the success path memsets); the scratch size is unchanged (1024). Behaviour outside the bugs is preserved: the success path still runs the plugin, `m_currentSamplePos` still advances only on success, `m_midiEvents.clear()` still runs after dispatch. `NativeLinuxRemoteVstPlugin64` and `zene` rebuild cleanly (`build-3.log`, `build-8.log`, `build-zene-final.log`, all EXIT 0).

### If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended contract that shows it.

- 3434 ALREADY-FIXED: `RemoteVstPlugin.cpp:1082` calls `vst::sortByDeltaFrames(m_midiEvents.data(), m_midiEvents.size())`, defined at `plugins/VstBase/VstMidiEventBatch.h:47` as an in-place stable insertion sort whose comment states the rationale (host audio callback waits; O(n^2) on a bounded batch). `rg stable_sort plugins/VstBase/RemoteVstPlugin.cpp` has no matches. Fix commit `ca5b95981d`. Contract: the period path must not allocate (`VstMidiEventBatchTest::sortAllocatesNothing` EXIT 0 in every GREEN log; the control `controlStableSortAllocates` shows the probe does see `std::stable_sort`'s buffer when it is used).
