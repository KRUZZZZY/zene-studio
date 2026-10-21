FIXED

# Cluster new-p10-178-port-port: overlapping same-key live notes overwrite layer routing

## Decision: REAL

At branch tip `b61e5f965`, `InstrumentLayers` kept a single atomic mask for each MIDI key. In
`src/core/InstrumentLayers.cpp:481-486`, a note-on overwrote it with `live.store(layers)`, and a note-off
consumed it with `live.exchange(0)`. A second same-key note-on therefore replaced the first note's route.
The live input path is `InstrumentTrack::processInEvent` to `InstrumentLayers::forwardInEvent`
(`src/tracks/InstrumentTrack.cpp:482, 487`).

The fix stores each note-on's mask in a fixed per-key route FIFO (`include/InstrumentLayers.h:189-204`).
Note-offs take the oldest route by wrap-safe age (`src/core/InstrumentLayers.cpp:486-496, 528-548`), and
key pressure reaches the union of active routes. At the 128-route bound, the oldest route receives an
immediate note-off; its later note-off is swallowed before the remaining routes are consumed. Layer insert
and removal remap every active route mask (`src/core/InstrumentLayers.cpp:206, 267, 558-580`), preserving
the existing held-note behavior. This state is runtime-only; saved project data and commands do not change.

The regressions exercise the actual live path, capacity behavior and sequence wrap. Each failed with the
product patch reversed and passed after restoration. The accepted commits were re-applied by hand; no
merge, cherry-pick or rebase was used.

## Verification evidence

All commands were run in this worktree. CTest commands ran from `build/tests`; logs are new files in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-178-port-port.r1/logs`.

| Check | Exact command | Exit | Result |
|---|---|---:|---|
| RED build, product reversed | `cmake --build build --target zene RackZoneFeedRunTest InstrumentLayersCommandsTest InstrumentSetupUiTest ArticulationPlaybackTest --parallel 4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-178-port-port.r1/logs/build-6-final-red-reverted.log 2>&1; echo EXIT=$?` | 0 | Rebuilt the original product with all regressions retained. |
| RED family, product reversed | `ctest -R '^(InstrumentLayersCommandsTest|ArticulationPlaybackTest|InstrumentSetupUiTest|RackZoneFeedRunTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-178-port-port.r1/logs/test-7-final-red-reverted.log 2>&1; echo EXIT=$?` | 8 | Three regressions failed: overlap (`firstOffReleasedSoftOnly`), capacity (`hardSurvivedStolenRouteOffs`) and wrap order (first route was 2, expected 1). |
| GREEN build, patch restored | `cmake --build build --target zene RackZoneFeedRunTest InstrumentLayersCommandsTest InstrumentSetupUiTest ArticulationPlaybackTest --parallel 4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-178-port-port.r1/logs/build-7-final-restored.log 2>&1; echo EXIT=$?` | 0 | Rebuilt the fixed product and all four test binaries. |
| GREEN family, patch restored | `ctest -R '^(InstrumentLayersCommandsTest|ArticulationPlaybackTest|InstrumentSetupUiTest|RackZoneFeedRunTest)$' -V --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-178-port-port.r1/logs/test-8-final-restored-green.log 2>&1; echo EXIT=$?` | 0 | 4/4 CTest executables passed; 34 Qt cases passed, 14 skipped, 0 failed. All three new regressions passed. |
| Product patch reverse | `git diff -- include/InstrumentLayers.h src/core/InstrumentLayers.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-178-port-port.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-178-port-port.r1/logs/fix.patch; echo EXIT=$?` | 0 | Reversed only the product patch; tests stayed in place. |
| Product patch restore | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-178-port-port.r1/logs/fix.patch; echo EXIT=$?` | 0 | Restored the product patch and repeated the final green build and test. |
| Feature gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-178-port-port > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-178-port-port.r1/logs/gates-4-feature-check.log 2>&1; echo EXIT=$?` | 0 | All eight rows PASS, including complexity and file-length (`no new item in your files`). |
| Whitespace | `git diff --check; echo EXIT=$?` | 0 | No whitespace errors. |

The affected family totals were 8 passed in `RackZoneFeedRunTest`, 5 passed and 10 skipped in
`InstrumentLayersCommandsTest`, 17 passed in `ArticulationPlaybackTest`, and 4 passed and 4 skipped in
`InstrumentSetupUiTest`.

## Files committed

- `include/InstrumentLayers.h`
- `src/core/InstrumentLayers.cpp`
- `tests/src/core/InstrumentLayersCommandsTest.cpp`
- `tests/src/core/RackZoneFeedRunTest.cpp`
- `tests/src/gui/InstrumentSetupUiTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-178-port-port.md`

## Self-check

- **Finding:** REAL. The old one-mask overwrite at `src/core/InstrumentLayers.cpp:481-486` was reproduced by the retained regression after reversing the product patch; see `test-7-final-red-reverted.log`.
- **Regression proof:** `InstrumentSetupUiTest::overlappingLiveSameKeyNotesKeepTheirLayerNoteOffRoutes`, `RackZoneFeedRunTest::liveSameKeyRouteCapacityStillForwardsTheNextZone` and `InstrumentLayersCommandsTest::liveRouteOrderingStaysFifoAcrossTheSequenceWrap` fail on the reversed product (CTest exit 8) and pass after restoration (CTest exit 0). The tests synchronously inject fixed `MidiEvent`s; the wrap test seeds fixed sequence values. No clock, sleep, timer or race is used.
- **Cause and siblings:** `forwardInEvent` is the only caller of `liveLayersFor`. Note-on admission, note-off consumption, key pressure, channel-wide messages, insert/remove remapping and `clear()` were checked. Base instrument note routing remains on its separate accepted/skipped counters; `InstrumentLayersCommandsTest` and `ArticulationPlaybackTest` passed.
- **Realtime:** `rememberLiveLayers`, `takeOldestLiveLayers`, `activeLiveLayers` and `admitLiveNoteOn` use fixed atomic storage and stack arrays. No allocation, deallocation, mutex, logging, I/O, signal or blocking wait was added to the live note path.
- **Persistence, undo and capacity:** Live routes are runtime-only; no project serialization format, control command or undo behavior changed, and no new undo step was introduced. Existing save/load coverage `InstrumentLayersCommandsTest::layersAddZoneRemoveUndoReload` passed. The 128-route bound applies only to live layer routing for one key. The next note-on is admitted by stealing the oldest route and pairing its later off, so the bound does not suppress another caller or a valid later note. Insert/remove route-mask remapping remains covered by `InstrumentSetupUiTest::liveNoteOffFollowsLayerAfterUndoInsert`.
- **Registration:** No new source file was added. The existing `InstrumentLayers` header/source and core test files are registered in `tests/fork-sources.txt`; the inherited `src/core/InstrumentLayers.cpp` ledger reason was extended in `tests/upstream-modifications.txt`.
- **ALREADY-FIXED / FALSE-POSITIVE question:** Not applicable; the verdict is REAL and the unfixed behavior was reproduced.
- **Feature gates:** `feature_check.py` exited 0; every reported row passed.
