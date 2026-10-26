IMPLEMENTED

Q050-146: IMPLEMENTED
Q050-094: IMPLEMENTED

Integration (round 5) of the accepted batch `feat/050-Q050-146-oc` @ `a06112903` (base `2da2ed356`,
train 8) onto `050/m0-train-16` @ `cf3618e61`. Per-unit records: `docs/050-features/Q050-146.md`,
`docs/050-features/Q050-094.md`. Logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-146.r5/logs`.

## Conflicts and how they were resolved

| File(s) | Conflict | Resolution |
|---|---|---|
| `include/AudioEngine.h`, `src/core/AudioEngine.cpp`, `include/AudioWideInputStage.h`, `src/core/AudioWideInputStage.cpp`, `src/core/audio/AudioDevice.cpp` | The line merged cluster 3705 (`9102db166`, `dea6dbab9`), an independent fix of the **same** wide/stereo skew: `drainCaptureStages()` drains `min(wide, stereo)` staged frames as one block, and `publishCaptured` pushes the stereo pair only for the frames the wide stage took. The accepted Q050-146 seqlock (`beginInputPublish`/`endInputPublish`/`drainInputStages`) edits the same lines. | Kept the line's version (3705) unchanged. The seqlock would be a second gate for the same property and would delay a block already complete in both rings by one period whenever a render collides with the next publication. Not a new design: the line's existing one. Semantic difference from the accepted patch: no bracket API; the fix on the branch is 3705's. |
| `tests/src/core/CapturePublisherTest.cpp` | Both sides added cases at the same place. | Kept the line's three 3705 cases and both Q050-146 cases. In the schedule case, the two bracket calls are dropped (the API no longer exists). Both Q050-146 cases now start with `resetInputStream()` (what a device start does). The line's `aBlockTheWidePathRefuses...` case leaves a wide block staged without its pair, and under the paired drain two renders never clear it. The first integrated run failed for that reason (`test-1.log`, EXIT 8). The comparisons themselves are unchanged. |
| `include/SampleClip.h` | The line replaced `m_rubberBand` with per-mode pools. Q050-094 added lane helpers next to them. | Both: the line's `m_rubberBands` / `m_rubberBandPublished` / `ensureRubberBandPool(mode)` and Q050-094's `addRecordedLane(take, newestIsActive)`, `addHistoryLane`, `newLaneClip`, `installFirstPass`, `overdub`. |
| `tests/src/core/RecordingRealtimeTest.cpp` | Helpers were added at the same place. | Both: the line's `feedInputToHandle` and Q050-094's record-mode helpers. |
| `tests/CMakeLists.txt`, `tests/fork-sources.txt` | Neighbouring list edits (`CPULoadWidgetTest`, `RecordingRecoveryMenuTest` on the line). | Both entries kept. |
| `tools/mcp-zene-control/zene_control/commands_snapshot.json` | Different capture metadata. | Regenerated live (`snapshot-1.log`, EXIT 0): 849 ids / 76 groups, unchanged. Only the `transport.record_options` `mode` enum (`keep_history`) and its description changed. |

## Failure paths and supported-platform hazards (together)

- **Capture/render race (Q050-146).** The publisher pushes wide before stereo. The drain reads the stereo count before the wide count, and `min()` can only under-count a block that is in flight, which then waits a period. Wide refusal publishes to neither path. A device restart (`resetInputStream`) clears `m_captureWideInUse`, so the next drain empties each ring on its own and re-aligns them. Both rings have 16384 frames. Local TSan does not run on this box, so this is reasoned, not machine-checked.
- **Realtime.** No audio-thread code was added by this integration. The kept code is 3705's relaxed atomics plus bounded copies. Q050-094 edits only the main-thread completion path (`SampleClip::acceptRecordedTake` → `installFirstPass` / `overdub` / `addHistoryLane` / `addRecordedLane`).
- **Undo and projects (Q050-094).** One undo step per pass, via the arm's track checkpoint. Config values 0/1 are unchanged. Lanes round-trip through save and reopen.
- **Portability.** The resolution adds no API. `std::numeric_limits<f_cnt_t>` in `drain` is the line's own code. No Qt-6-only API, no `min`/`max` identifiers, no `std::filesystem`, no POSIX calls.

## Q050-146 - Keep wide and stereo capture blocks aligned

| # | Criterion | Test | Result |
|---|---|---|---|
| 1 | Block-boundary regression test compares unique wide-channel impulses with the stereo-bus frames under an interleaved drain schedule | `CapturePublisherTest::aDrainBetweenTheWideAndStereoPushesKeepsBothViewsOnOneBlock` (wide push, render, stereo push), `...aRenderThreadDrainingWhileABlockIsPublishedNeverSkewsTheViews` (producer thread through `publishCaptured`, 3000 blocks) | `ctest -R ^CapturePublisherTest$` EXIT 0 (`test-2.log`); `--repeat until-fail:3` EXIT 0 (`test-4.log`); final batch EXIT 0 (`test-5-final.log`) |
| 2 | If it reproduces skew, the fix publishes a consistent block | Same tests with the line's paired drain reverted to two independent drains (`paired = stereoStaged`, wide drains `wideStaged`) | Fails: `stereo bus holds 64 frames, wide stage 128`, EXIT 8 (`build-4-reversal146.log` EXIT 0, `test-3-reversal146.log`). The 3705 case `theWideBlockAndItsStereoPairAreDrainedAsOneBlock` also fails. Restored: `build-5.log` EXIT 0, `test-4.log` EXIT 0. Pair-selection, mono and no-allocation cases pass in both states. |

## Q050-094 - Make audio record modes honor history

| # | Criterion | Test | Result |
|---|---|---|---|
| 1 | The record-mode control and `transport.record_options` expose the same choices for MIDI and audio | `RecordModeMenuTest` (triggers the menu QActions and the header readout), `RecordingPassTest::theRecordModeIsOneChoiceForMidiAndAudio`, `theStoredModeNumbersAreStable`, `ControlCommandsSnapshot` | EXIT 0 (`test-5-final.log`, 9/9 passed). The accepted round's reversal evidence for this row is `Q050-146.r1/logs/test-11-reversal094c.log` and `test-12-reversal094d.log` (EXIT 8); these lines merged cleanly and were not re-reversed here. |
| 2 | Audio tests prove overdub, replace, keep-history; keep-history creates recoverable lanes; undo restores | `RecordingRealtimeTest::anOverdubPassIsMixedIntoTheEarlierAudio`, `aReplacePassReplacesTheEarlierAudio`, `aKeepHistoryPassKeepsTheEarlierAudioAsATakeLane`, `oneUndoRestoresTheClipAKeepHistoryPassSplit`, `keepHistoryLanesSurviveSaveAndReopen`, `anOverdubOverATrimmedClipAddsTheLayerWithoutTouchingIt`, `aKeepHistoryPassOnAnEmptyClipIsALane`; `TakeLaneTest`, `TakeLaneCompPlaybackTest` | EXIT 0 (`test-5-final.log`, `test-7.log`). Re-proved on the integrated tree: with `installFirstPass` reduced to the old replace, 6 cases fail, EXIT 8 (`build-7-reversal094.log` EXIT 0, `test-6-reversal094.log`: 47 passed, 6 failed). The Replace case passes by design because it pins the pre-existing behaviour. Restored: `build-8.log` EXIT 0, `test-7.log` EXIT 0. |

## Self-check

- **Each new test fails with the product change reverted and passes restored?** Yes. Q050-146: `test-3-reversal146.log` EXIT 8, then `test-4.log` EXIT 0. The product change now proven is the line's 3705 paired drain, which is the fix kept. Q050-094: `test-6-reversal094.log` EXIT 8, then `test-7.log` EXIT 0. There are two exceptions. The threaded Q050-146 stress case passed in the reversed run; the schedule case is the deterministic reproduction. `aReplacePass...` pins existing behaviour.
- **Every criterion proven by a test I ran?** Yes. The final batch (`test-5-final.log`, EXIT 0) ran all 9 of: `CapturePublisherTest`, `RecordingRealtimeTest`, `RecordingPassTest`, `RecordModeMenuTest`, `RecordRemainingTest`, `TakeLaneTest`, `TakeLaneCompPlaybackTest`, `ControlCommandsSnapshot`, `ReversibilityContractTest`.
- **GUI and agent, the same command?** Yes. The Record Mode submenu and the Rec mode readout run `transport.record_options`, and `RecordModeMenuTest` triggers the actions.
- **One undo step; honest reversibility?** No command was added. `transport.record_options` stays `true_inverse`. `ReversibilityContractTest` EXIT 0, and the A16 histogram is unchanged.
- **Snapshot and doc figures?** `snapshot_live.py` EXIT 0 (`snapshot-1.log`). `tests/doc-figures-gate.py` EXIT 0 (`docfig-1.log`, 849 ids / 76 groups).
- **Gates?** `feature_check.py` RESULT: PASS on every row (`feature-check-2.log`): fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, and complexity/file-length (no new item). The original script failed because it creates a log folder beside the worktree, outside the write jail (`feature-check-1.log`, EXIT 1). I ran a copy whose only change is the log folder (`/tmp/q146-fc/logs`).
- **`submit_check.py`** (`submit-check-1.log`, EXIT 1) passes every row except "gates (feature_check.py)". That row fails because it runs the original checker, which hits the same jail `PermissionError` (`feature-check-3-original.log`). The copy's run at the committed HEAD is PASS on all rows (`feature-check-4.log`, EXIT 0). The canary WARN lists existing ids (`transport.record_options` and others), not new ones. The figure is unchanged at 849 / 76, and `doc-figures-gate.py` passes.
- **Realtime, projects, portability?** See "Failure paths" above. This integration adds no audio-thread code and no format change.
- **Build:** `build-2.log` EXIT 0 (zene plus the 7 test targets). ccache's configured temp directory is outside the write jail (`build-1.log`), so builds set `CCACHE_TEMPDIR=/tmp/ccache-tmp-q146`.
