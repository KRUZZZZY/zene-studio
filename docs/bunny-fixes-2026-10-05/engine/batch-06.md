# Engine batch 06 — mixer latency, loop detection and MIDI learn

Worktree `zene-bunny-engine`, branch `bugs/bunny-engine-2026-10-05`. Six findings,
three fixed with regression tests, one closed as a false positive, one skipped as
out of scope.

| Verdict | Count | Findings |
| --- | --- | --- |
| FIXED | 3 | 3287, 574, 5155 |
| FALSE-POSITIVE | 2 | 571, 4282 |
| SKIPPED-SCOPE | 1 | 4226 |

---

### Cluster 3287 — NaN swing values survive clamping

Verdict: FIXED

Evidence: `src/core/MidiPlayback.cpp:55` fed the raw value to `std::clamp(swing,
0.f, MaxSwing)`. Clamp is two comparisons, and every comparison against NaN is
false, so NaN passes through unchanged; `load()` (`src/core/MidiPlayback.cpp:113`)
hands it `toFloat()` straight from the project file, so a malformed
`swing="nan"` installed a NaN that persistence and playback then treat
inconsistently. Real. Fixed by rejecting a non-finite value before clamping,
falling back to the default 0 — the same policy prior commit `e5deecb57` applied
to malformed attributes.

Commit: 02b5e4fa6

Test: `NotePlaybackTest::nonFiniteSwingLoadsBounded`

Red/green:
- Red: `ctest -R '^NotePlaybackTest$' --output-on-failure` → EXIT=8,
  `FAIL! : NotePlaybackTest::nonFiniteSwingLoadsBounded() 'std::isfinite(playback.swing())' returned FALSE` at `NotePlaybackTest.cpp:279`
- Green: same command → EXIT=0 (11/11); both gates EXIT=0

---

### Cluster 574 — Disabled sends still raise PDC alignment

Verdict: FIXED

Evidence: `Mixer::resolveLatency` (`src/core/Mixer.cpp:1820`) skipped only
feedback routes on the way through `channel->m_receives`, never
`route->enabled()`. A send switched off — which `MixerRoute::enabled()`
(`include/Mixer.h:340`) makes deliver nothing by design (0.5.0 L08 / MX-047) —
still contributed its sender's tap latency, delaying every other path into the
receiver and inflating the published total. `updateLatencyCompensation` runs on
the audio thread, so the fix is one read/branch over an existing `BoolModel`
value: no allocation, no locking, no logging.

Commit: 5b1fe709f

Test: `PdcMixerTest::disabledSendDoesNotRaiseAlignment`

Red/green:
- Red: `ctest -R '^PdcMixerTest$' --output-on-failure` → EXIT=8,
  `FAIL! : PdcMixerTest::disabledSendDoesNotRaiseAlignment() 'offReceiverLatency == 0' returned FALSE. (a send that was switched off still raises the receiver's alignment point to 257 frames)`;
  evidence line `PDC_DISABLED_SEND latency=257 aligned_receiver=257 aligned_master=257 disabled_receiver=257 disabled_master=257`
- Green: same command → EXIT=0 (10/10); plus `MixerConcurrencyTest`, `HardwareIoTest`, `RecordingLatencyTest`, `MixerLatencyTextTest`, `DeviceToolsTest` → EXIT=0 (6/6); both gates EXIT=0

---

### Cluster 5155 — Loop detection recurses forever through an existing feedback cycle

Verdict: FIXED

Evidence: `Mixer::checkInfiniteLoop` (`src/core/Mixer.cpp:1630`) recursed over
`to->m_sends` and `to->m_sidechainSends` with no visited set, and feedback routes
are stored in `m_sends` (`src/core/Mixer.cpp:1386`, flagged at `:1380`). The
feedback submode makes those cycles valid by construction, so once A→B→A exists,
any later check reaching A walked A→B→A→B until the stack was gone. Real.
Fixed by carrying a visited set on the traversal and skipping feedback edges,
which by construction never close a *new* cycle. Every caller
(`createRoute`, `createSidechainSend`, `isInfiniteLoop` from the GUI routing
arrows) is control-thread, so the one visited vector per entry is not on a
process/render/play path. `include/Mixer.h` changed with `src/core/Mixer.cpp`.

Commit: faa6e3fa6

Test: `MixerRoutingBackwardCompatTest::anExistingFeedbackCycleDoesNotRecurseForever`

Red/green:
- Red: `ctest -R '^MixerRoutingBackwardCompatTest$' --output-on-failure` → EXIT=8,
  `Received signal 11 (SIGSEGV), code 1, for address 0x00007ffc47992ff8` in `anExistingFeedbackCycleDoesNotRecurseForever function time: 7ms`
- Green: same command → EXIT=0; plus `MixerConcurrencyTest`, `PdcMixerTest`, `HardwareIoTest`, `MixerStripTest`, `MixerToolsTest`, `FeedbackModeActionTest`, `MixerLatencyTextTest` → EXIT=0 (8/8); both gates EXIT=0

---

### Cluster 571 — Channel EQ settings never reach the audio path

Verdict: FALSE-POSITIVE

Evidence: the EQ stage is called, twice-removed from the line the report quotes.
`MixerChannel::doProcessing` calls `mixerstrip::applyOutput`
(`src/core/Mixer.cpp:744`), and `applyOutput` calls `applyChannelEq`
(`src/core/MixerStrip.cpp:169`), which is the separate stage declared at
`include/MixerStrip.h:76` and defined at `src/core/MixerStrip.cpp:111`. The
report read the `applyInput` call site and did not follow the chain past it. The
stage is also already covered by `tests/src/core/MixerToolsTest.cpp`, so a
missing call would have failed an existing test.

Commit: none

Test: none

Red/green: none

---

### Cluster 4282 — The selected pan law is ignored by channel balance pan

Verdict: FALSE-POSITIVE

Evidence: `MixerChannel::doProcessing` calls `mixerstrip::applyPanMode`
(`src/core/Mixer.cpp:745`), and for `PanMode::Balance` that delegates to
`applyPanLaw` (`src/core/MixerStrip.cpp:203` → `:187`), which reads
`MixerOptions::gains(law, ...)` (`src/core/MixerOptions.cpp:51-68`). `applyPanLaw`
returns false for exactly one law — `PanLaw::Balance` — and the classic stage
(`src/core/Mixer.cpp:748-756`) then applies balance gains, which is the
documented intent of that split rather than a bypass of the setting. The report
cited `Mixer.cpp:~760`, a few lines past the delegation it describes.

Commit: none

Test: none

Red/green: none

---

### Cluster 4226 — MIDI Learn bindings do not create an undo step

Verdict: SKIPPED-SCOPE

Evidence: the observation is accurate — `MidiLearn::bindFocusedControl`
(`src/core/MidiLearn.cpp:249-286`) calls `bindMidiController` →
`target->setControllerConnection(connection)` directly and then only
`song->setModified()`, and `src/core/MidiLearn.cpp` contains no journalling call
at all. But the identical non-journalled write is the GUI path's existing
behaviour (`src/gui/AutomatableModelView.cpp:230`), so this is a consistent
existing design rather than a defect local to MIDI Learn. Making the binding
undoable needs a new reversible journalling command for the connection change —
a control-surface design change (rule 8/rule 7) well past the ~150-line budget,
so the code was left alone.

Commit: none

Test: none

Red/green: none

---

Leftovers: none — all six findings reached a verdict; clusters 571 and 4282 were closed on call-chain evidence without a code change, and cluster 4226 is left as a design question for the parent rather than a defect to patch.