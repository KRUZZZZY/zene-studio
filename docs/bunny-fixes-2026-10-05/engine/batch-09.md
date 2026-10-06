# Engine lane — batch 09 (branch `bugs/bunny-engine-2026-10-05`)

Six findings: one FIXED (the real defect), one REAL-UNTESTED (a genuine race I could not make
deterministic headlessly, so no product code was touched), and four FALSE-POSITIVE against the
current tree.

### Cluster 4280 — Reopening a bus-only route adds a direct send to master

Verdict: FALSE-POSITIVE

Evidence: `src/core/Mixer.cpp:2469-2478` — `allocateChannelsTo()` is what builds channels during a
load, and after each `createChannel()` it immediately calls
`deleteChannelSend(m_mixerChannels.size()-1, 0)` with the comment "delete the default send to
master" (`Mixer.cpp:2476-2477`). The seeded master send is therefore removed at allocation time,
before the `<send>` elements are read at `Mixer.cpp:2591-2612`, so nothing survives to add a
duplicate master route for a bus-only channel. The seed in `clearChannel()`
(`Mixer.cpp:2280-2291`) is exactly what `allocateChannelsTo()` undoes for the load path.

Commit: none

Test: none

Red/green: none

### Cluster 5153 — Feedback sends read sender state while its worker is still running

Verdict: REAL-UNTESTED

Evidence: `src/core/Mixer.cpp:57-61` — `delivers()` short-circuits on `m_from->m_hasInput ||
m_from->m_stillRunning` (plain `bool`, `include/Mixer.h:100` and `:109`) before it reaches the
`(feedback() && !feedbackCommittedQuiet())` term. Those two fields are written by the sender
worker (`Mixer.cpp:723-732`, `src/core/HardwareIo.cpp:220`) while the receiver's worker reads
them here, and `bakedFor()` (`src/core/MixerPostInserts.cpp:43-46`) likewise reads the sender's
non-atomic `m_baked` before testing `route->feedback()`. The race and the read ordering are as
described. I did not change the code: this is a concurrency defect on the process path, and a
C++ data race between two workers cannot be made to fail deterministically in a headless test
(the failure needs an interleaving, and there is no non-racy observable to assert on). Per the
lane rules I am leaving the product code alone and recording the evidence instead.

Commit: none

Test: none

Red/green: none

### Cluster 1501 — Mixer snapshots omit whether sends are pre-fader

Verdict: FIXED

Evidence: `include/MixerSnapshots.h:39-45` — `MixerSendState` held `to`, `amount`, `pan` and
`enabled` and no tap point, `stateOf()` captured only those four
(`src/core/MixerSnapshots.cpp:58-62`) and `applySends()` wrote only those four back
(`MixerSnapshots.cpp:83-85`), so a recall left a send wherever the user had since moved it — and
the `<send>` element dropped the setting through the project file too. The capture, the recall
and the XML attribute now carry `preFader`; a snapshot written before this loads the route
constructor's default (post-fader). The control-command result schema is untouched:
`channelJson()` (`src/core/ControlCommandsMixerSnapshots.cpp:75-85`) names its send fields
explicitly, so the extra struct member does not reach `mixer.snapshot_list`.

Commit: 1e1ccd541

Test: ProjectStateTest::aSnapshotRestoresASendsFaderTap

Red/green:
- `cmake --build build --target zene ProjectStateTest > build/bunny-b.log 2>&1` — BUILD=0
- `cd build/tests && ctest -R '^ProjectStateTest$' --output-on-failure` — EXIT=8, `FAIL!:
  ProjectStateTest::aSnapshotRestoresASendsFaderTap() 'route->preFader()' returned FALSE` (with
  the recall line temporarily reverted: 8 passed, 1 failed)
- same build + `ctest -R '^ProjectStateTest$'` with the fix — EXIT=0 (8 passed, 0 failed)
- `cmake --build build --target zene ProjectStateTest LibraryMenusTest` — BUILD=0;
  `ctest -R '^(ProjectStateTest|LibraryMenusTest)$'` — EXIT=0 (2/2 passed)
- `bash tests/complexity-gate.sh --check` — EXIT=0
- `bash tests/file-length-gate.sh --check` — EXIT=0

### Cluster 1500 — Applying a mixer snapshot does not restore its send routing

Verdict: FALSE-POSITIVE

Evidence: `include/MixerSnapshots.h:69-71` states the contract this report calls a defect:
"Recall writes the parts it is asked for onto the channels and sends that still exist and counts
the ones that do not; it never creates or deletes a channel or a send, so it cannot change the
mixer's graph." `applySends()` (`src/core/MixerSnapshots.cpp:75-88`) is exactly that: it updates
matching routes and counts the absent ones into `Applied::missingSends`. Both reported symptoms —
an added send surviving a recall, and a deleted send staying missing — are the documented
partial-application semantics, stated again in the command's own description
(`src/core/ControlCommandsMixerSnapshots.cpp:177-180`: "Channels and sends that no longer exist
are skipped and counted; nothing is created or deleted").

Commit: none

Test: none

Red/green: none

### Cluster 4255 — Mixer strip indexes a `SampleBuffer` with `operator[]`

Verdict: FALSE-POSITIVE

Evidence: `include/Mixer.h:104-105` declares `SampleFrame* m_buffer` — a raw heap array
(`new SampleFrame[framesPerPeriod()]`, `Mixer.cpp:214`), not a `SampleBuffer`, so
`m_buffer[f][0]` at `src/core/MixerStrip.cpp:50` is plain pointer indexing into that array and the
`[side]` is `SampleFrame::operator[]`, which exists at `include/SampleFrame.h:96-104`. The rule-10
`SampleBuffer` trap does not apply to this member. The file builds and its tests pass.

Commit: none

Test: none

Red/green: none

### Cluster 5164 — Baked post-fader inserts apply the fader twice to meters

Verdict: FALSE-POSITIVE

Evidence: `updateMeters()`'s third parameter is named `volume`
(`include/MixerStrip.h:88`, `src/core/MixerStrip.cpp:273`), but the caller does not pass the
fader — it passes `meterGain` from `m_postInserts.process(*this, v, fpp)`
(`src/core/Mixer.cpp:816`), and `process()` returns `1.f` exactly when it baked the fader into the
buffer (`src/core/MixerPostInserts.cpp:69`), returning the raw fader only on the un-baked path
(`MixerPostInserts.cpp:58`). So when the buffer already includes the fader, the RMS and clip lines
at `MixerStrip.cpp:301-303` multiply by 1, not by the fader a second time. The pre-fader readings
take the separately kept copy (`MixerStrip.cpp:292`), which is the MX-082 behaviour the comment
describes.

Commit: none

Test: none

Red/green: none

Leftovers: cluster 5153 is an unfixed data race on the process path — `delivers()`
(`src/core/Mixer.cpp:57-61`) reads the sender's non-atomic `m_hasInput`/`m_stillRunning` and
`bakedFor()` (`src/core/MixerPostInserts.cpp:43-46`) reads the sender's non-atomic `m_baked`
before testing `feedback()`. It needs either an atomic/committed-snapshot read order or a TSan
run to test; neither is available in this headless lane, so it is reported, not fixed.