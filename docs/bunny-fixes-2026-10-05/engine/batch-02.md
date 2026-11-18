# Engine lane, batch 02 — 2026-10-05

Branch `bugs/bunny-engine-2026-10-05`. Six findings: one fixed, five not.

### Cluster 3714 — Deterministic rendering has a worker race

Verdict: SKIPPED-SCOPE

Evidence: `src/core/AudioEngineWorkerThread.cpp:290` already re-reads
`deterministicProcessing()` immediately before `globalJobQueue.run()` inside the
wake loop, and the mode flag itself is a `std::atomic<bool>` accessed with
acquire/release (`src/core/AudioEngineWorkerThread.cpp:48`, `:52`, `:57`). The
residual window — a worker that has already passed the check when the render
enables the flag — is closed only by making the mode change wait for every worker
to be parked, which means synchronization on the job-drain path itself. That is a
design change to the multicore scheduling contract, not a defect-sized edit, and
rule 6 forbids putting a lock there. The comments at
`src/core/AudioEngineWorkerThread.cpp:245` and `:283` already argue this
deliberately, with the measurement that `RenderJobQueueTest::
inlineModeNeverLetsThePoolTakeAJob` took a job on a pool thread without the check.

Commit: none

Test: none

Red/green: none

### Cluster 4593 — Invalid channel-count configuration becomes mono instead of the default

Verdict: ALREADY-FIXED

Evidence: `src/core/AudioInputPath.cpp:52` is the `configInt()` helper, which takes
`toInt(&ok)` and returns the fallback when parsing fails;
`configuredPlan()` calls it with `DefaultChannels` at
`src/core/AudioInputPath.cpp:92`. `audioinput/channels=abc` therefore yields two
channels as the comment promises, not a zero clamped to `MinChannels`. The
finding quotes a revision in which the check was already present.

Commit: none

Test: none

Red/green: none

### Cluster 22 — Direct routing ignores additional pins in the same channel pair

Verdict: FALSE-POSITIVE

Evidence: the pair test at `src/core/AudioPortsModel.cpp:300` already requires the
exact pattern — `m.enabled(tc, 0) && !m.enabled(tc, 1) && !m.enabled(tc + 1, 0)
&& m.enabled(tc + 1, 1)`. The two negated terms reject exactly the cross
connections the finding describes, and because the caller has already established
`ins`/`outs` of 0 or 2 (`src/core/AudioPortsModel.cpp:267`), those four pins are
the whole matrix. There is no state in which an extra pin survives the test.

Commit: none

Test: none

Red/green: none

### Cluster 23 — Audio port edits race with audio routing

Verdict: SKIPPED-SCOPE

Evidence: `Matrix::setPin()` (`src/core/AudioPortsModel.cpp:374`) writes
`std::vector<bool>` rows unsynchronized, as described. The proposed remedy —
immutable routing snapshots or synchronization at an audio boundary — changes how
the router reads its model on the render path and is a redesign of
`AudioPorts`' hot path, not a defect-sized fix; any variant that guards the write
with a lock puts a lock on the play path, which rule 6 forbids. The cached
`m_usedTrackChannels` bitset and `m_trackChannelsUpperBound` are likewise written
from the same method. Left alone deliberately.

Commit: none

Test: none

Red/green: none

### Cluster 5522 — Shrinking track channels leaves stale used-channel bits

Verdict: FIXED

Evidence: `Matrix::updateUsedTrackChannels()`
(`src/core/AudioPortsModel.cpp:468`) iterated only over `trackChannelCount()`, so
bits for rows a shrink had removed stayed set in the `MaxTrackChannels`-wide
`std::bitset` declared at `include/AudioPortsModel.h:189`. Observable through
`src/gui/PinConnector.cpp:301`, which draws a passthrough line per unused track
channel, and `src/core/ControlCommandsPorts.cpp:127`, which lists these bits
verbatim in the ports command result. The pre-existing case at
`tests/src/core/AudioPortsTest.cpp:313` did not catch it because it never
connected the removed rows, so their bits were never set. Fixed by clearing the
bitset before recomputing it.

Commit: 2562b651b

Test: AudioPortsModelTest::shrinkingTrackChannelCountClearsUsedBits

Red/green: `cd build/tests && ctest -R '^AudioPortsModelTest$' --output-on-failure`
EXIT=8 ("'!model.out().usedTrackChannels()[3]' returned FALSE") before the fix;
`ctest -R '^AudioPorts(Model)?Test$'` EXIT=0 after (2/2), and
`ctest -R '^(AudioBusTest|RemotePluginAudioPortsTest|AudioPluginTest)$'` EXIT=0
(3/3). Gates: `bash tests/complexity-gate.sh --check` EXIT=0,
`bash tests/file-length-gate.sh --check` EXIT=0.

### Cluster 5518 — Resampler quality changes allocate on the render path

Verdict: SKIPPED-SCOPE

Evidence: `AudioResampler::setMode()` does call `src_new()`
(`src/core/AudioResampler.cpp:118`), and it is reachable from the render path via
`Sample::play` at `src/core/Sample.cpp:120`. However it is guarded by a mode
comparison that is false for every render not asking for a non-default converter
(`src/core/Sample.cpp:118`), and `ProjectRenderer` sets the quality once per
render rather than per buffer (`src/core/ProjectRenderer.cpp:157`), so the state
is created at most once per render. The header at
`include/AudioResampler.h:135` documents this contract and the filter-history
trade-off. Removing the allocation means pre-creating one converter state per
mode off the audio thread and switching to a prepared state — a redesign of
`AudioResampler` and its `PlaybackState` ownership, beyond a defect-sized change,
and the render-path hazard is deliberate and argued in place.

Commit: none

Test: none

Red/green: none

Leftovers: cluster 3714 (worker/queue mode-change window) and cluster 23
(unsynchronized pin edits vs the router) are both real but left as
SKIPPED-SCOPE design work on the audio path; whoever owns the render-path
synchronization contract should schedule them together. No test file, CMake
registration or fork-sources entry was needed for this batch.