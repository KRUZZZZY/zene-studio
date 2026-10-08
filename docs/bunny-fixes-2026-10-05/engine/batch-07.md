# Engine batch-07 — Luna clusters 5157, 4288, 5154, 4279, 1512, 5156

> **Correction, 2026-10-08 (rework, rounds 1 and 2; see `rework-2026-10-08.md`).** The
> source review of 2026-10-06 and an independent review of the rework found the claims
> below false. The state of each cluster on the current tree:
>
> - **4288 (`2fa142d5b`)**: the original guard was regressive. It skipped master (num 0) on
>   load, so a saved master lost its state. Round 1 (`2ff89cf2c`) restored master. Round 2
>   (`2d3c4f1b1`) found that a missing or non-numeric `num` also parsed as 0 and overwrote
>   the master. Such an element is now skipped too. Only a negative or malformed `num` is
>   refused; master (0) loads.
> - **5154 (`4d50f628b`)**: the original claim that the two walks kept separate sets was
>   false. One visited set was shared by the receiver walk and the sender-only walk, so a
>   channel reached sender-only was skipped by the receiver walk and an upstream channel
>   stayed muted. Fixed in `133960a71` with one visited set per walk direction.
> - **5156 (`f1a74cc54`)**: the claim "no allocation, no locking" was false. The original
>   read called `valueBuffer()`, which takes `m_valueBufferMutex` on the audio thread. The
>   attempted fix (`0cfcbfa14`) removed the lock but also changed the behaviour for
>   controller-driven and manually changed amounts. Round 2 (`74c567843`) restored the
>   original `valueBuffer()` read exactly, so the lock is back. **This cluster has no lock
>   fix on the current tree; it is BLOCKED-ON-2647**, the mixer-wide realtime item. The
>   per-frame behaviour (ramp, controller, interpolation) is covered by tests, and that
>   coverage is the only thing this cluster now provides.
>
> The verdicts and red/green results below describe the ORIGINAL commits. They are kept for
> the record and are not a statement about the current tree, except where the correction
> above says otherwise. Clusters 5157, 4279 and 1512 were not in the rework and are unchanged.
> The verdicts and red/green results below describe the ORIGINAL commits. They are kept for
> the record and are not a statement about the current tree, except where the correction
> above says otherwise. Clusters 5157, 4279 and 1512 were not in the rework and are unchanged.

Worktree `zene-bunny-engine`, branch `bugs/bunny-engine-2026-10-05`. All six findings were
checked against the current tree; all six were real and are fixed. Baseline before this
batch: `dbfb326d7`.

### Cluster 5157 — Updating existing routes races the audio thread
Verdict: FIXED
Evidence: `src/core/Mixer.cpp` — the update branches of `Mixer::createChannelSend` and
`Mixer::createSidechainSend` wrote `preFader` and the sidechain `mode` on an existing
route without taking the change boundary that route creation and deletion already take, so
a control-thread edit could land while the render thread read those plain fields.
Commit: `497436e60`
Test: `MixerConcurrencyTest::updatingAnExistingRouteWaitsForTheRenderPeriodToEnd`
Red/green: build `cmake --build build --target zene MixerConcurrencyTest` EXIT=0;
`ctest -R '^MixerConcurrencyTest$'` EXIT=8 RED (`!flippedWhileRendering` false — the
render thread saw the half-written field); after the fix EXIT=0; related run
`ctest -R '^(MixerRoutingBackwardCompatTest|MixerStripTest|MixerConcurrencyTest)$'`
EXIT=0 (3/3); complexity gate EXIT=0, file-length gate EXIT=0.

### Cluster 4288 — Invalid bus or sidechain channel numbers can index outside the channel vector
Verdict: FIXED
Evidence: `src/core/Mixer.cpp`, `Mixer::loadSettings` — the bus pre-pass indexed
`m_mixerChannels[busNum]` and a sidechain `<sidechain-send channel="-1">` was passed
straight into route creation, where the channel number is converted to an unsigned index.
Negative values were neither rejected nor range-checked before indexing.
Commit: `2fa142d5b`
Test: `MixerRoutingBackwardCompatTest::aNegativeChannelNumberIsRejectedNotIndexed`
Red/green: build EXIT=0; `ctest -R '^MixerRoutingBackwardCompatTest$'` EXIT=8 RED
(SIGSEGV in the negative-index load); after the fix EXIT=0; related run
`ctest -R '^(MixerRoutingBackwardCompatTest|MixerStripTest|MixerConcurrencyTest)$'`
EXIT=0 (3/3); complexity gate EXIT=0, file-length gate EXIT=0.

### Cluster 5154 — Solo traversal recurses forever through a feedback loop
Verdict: FIXED
Evidence: `src/core/Mixer.cpp` — `unmuteForSolo`, `unmuteSenderForSolo` and
`unmuteReceiverForSolo` walked the send/receive graph recursively with no visited-channel
guard. Feedback mode allows cycles in the send graph, so a solo inside one recursed until
the stack overflowed.
Commit: `4d50f628b`
Test: `MixerRoutingBackwardCompatTest::soloingAChannelInAFeedbackCycleTerminates`
Red/green: build EXIT=0; `ctest -R '^MixerRoutingBackwardCompatTest$'` EXIT=8 RED
(SIGSEGV from the unbounded recursion); after the fix EXIT=0; related run
`ctest -R '^(MixerRoutingBackwardCompatTest|MixerStripTest|MixerConcurrencyTest)$'`
EXIT=0 (3/3); complexity gate EXIT=0, file-length gate EXIT=0. Fix adds a
`std::set<MixerChannel*>` visited-set overload in `include/Mixer.h` /
`src/core/Mixer.cpp`; the sender and receiver walks keep separate sets so acyclic
propagation semantics are unchanged. The set is control-thread only — solo propagation is
not on a process/render callback.

### Cluster 4279 — Clearing a channel leaves its strip processing settings behind
Verdict: FIXED
Evidence: `src/core/Mixer.cpp`, `Mixer::clearChannel` — the method reset volume, pan,
mute and solo only, leaving trim, polarity, mono, swap, EQ, pan mode, width, solo-safe and
the crossfader-side models applied to subsequent audio.
Commit: `49968e2c2`
Test: `MixerStripTest::clearingAChannelResetsItsStripSettings`
Red/green: build EXIT=0; `ctest -R '^MixerStripTest$'` EXIT=8 RED (trim still -6.0206 after
clear, expected 0); after the fix EXIT=0; related run
`ctest -R '^(MixerRoutingBackwardCompatTest|MixerStripTest|MixerConcurrencyTest)$'`
EXIT=0 (3/3); complexity gate EXIT=0, file-length gate EXIT=0. Fix adds
`mixerstrip::reset(MixerChannel&)` in `include/MixerStrip.h` / `src/core/MixerStrip.cpp`
and calls it from `clearChannel`; the reset values mirror `mixerstrip::load()` defaults,
so a cleared channel is indistinguishable from a fresh one.

### Cluster 1512 — A saved post-fader send to a bus reloads as pre-fader
Verdict: FIXED
Evidence: `src/core/Mixer.cpp` — `createChannelSend` forced `preFader || to->isBus()`, so
every bus send was pre-fader regardless of the stored value, and `saveSettings` did not
persist `prefader` for bus receivers at all; an explicitly post-fader bus send could not
survive a save/load round trip.
Commit: `262d47f65`
Test: `MixerRoutingBackwardCompatTest::aPostFaderSendToABusSurvivesSaveLoad`
Red/green: build EXIT=0; `ctest -R '^MixerRoutingBackwardCompatTest$'` EXIT=8 RED
(`!loaded->preFader()` false — the post-fader send came back pre-fader); after the fix
EXIT=0; related run `ctest -R '^(MixerRoutingBackwardCompatTest|MixerStripTest|
MixerConcurrencyTest)$'` EXIT=0 (3/3); complexity gate EXIT=0, file-length gate EXIT=0.
Pre-fader remains the default for NEW bus sends; only an explicitly persisted value is
honoured, so legacy files that omit the attribute are unchanged.

### Cluster 5156 — Sidechain send automation is reduced to one value per block
Verdict: FIXED
Evidence: `src/core/Mixer.cpp`, `MixerChannel::sumSidechainInputs` — the amount was read
with `route->amount()->value()` and that one scalar multiplied every frame of the period,
while a regular send already multiplies with the amount model's sample-exact `ValueBuffer`
in `doProcessing`. An automated sidechain send therefore lost its curve.
Commit: `f1a74cc54`
Test: `PhaseDSidechainTest::anAutomatedSidechainAmountIsAppliedPerFrame`
Red/green: build `cmake --build build --target zene PhaseDSidechainTest` EXIT=0;
`ctest -R '^PhaseDSidechainTest$'` EXIT=8 RED (`summed[0] < 0.05f` false — first frame 1,
the whole-block constant applied to every frame); after the fix EXIT=0; related run
`ctest -R '^(MixerRoutingBackwardCompatTest|MixerStripTest|MixerConcurrencyTest|
PhaseDSidechainTest)$'` EXIT=0 (4/4); complexity gate EXIT=0, file-length gate EXIT=0.
The sum now reads the per-frame buffer when automation published one for the period and
keeps the existing scalar path otherwise — no allocation, no locking, no growth, since
`valueBuffer()` is the model's own fixed-capacity member.

Leftovers: none. All six findings are fixed with a deterministic red/green regression each,
both gates EXIT=0 before every commit, and the tree is clean.