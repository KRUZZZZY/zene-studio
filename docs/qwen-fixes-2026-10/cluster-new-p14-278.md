FALSE-POSITIVE

# Cluster new-p14-278 - "Comp playback treats a lane mentioned anywhere as active for the whole clip"

Verdict: **FALSE-POSITIVE**. `namedByComposite()` is not the limiter it is accused of being: it
is only the *admission* predicate that lets a MUTED take into the pass at all, and what a take
then renders is gated frame by frame to the composite's spans for its own lane, inside the play
handle. The reported symptom ("the clip can play across its full window, including outside that
segment") cannot occur on this branch, and the gate that prevents it predates the admission code
the report points at.

## The finding

- where: `src/tracks/SampleTrack.cpp:~99` (today `:116-123`)
- what: `namedByComposite()` checks only whether any segment names the clip's lane; "the comped
  predicate also admits every grouped clip"
- when: a muted clip's lane is selected for only part of a comp
- fix suggested: "schedule only the part of a clip that contributes to the composite at the
  current position"

## What the code actually does

1. `src/tracks/SampleTrack.cpp:114-123` - `namedByComposite()` answers one question, "does any
   segment of this track's composite name this clip's lane at all". It has exactly one caller,
   `src/tracks/SampleTrack.cpp:281`:
   `if( !st->isMuted() || namedByComposite( *this, *st ) )`. So it decides whether a **muted**
   take is scheduled (the comp, not the mute, decides a comped take's contribution - BUGS P6
   round 4). It does not decide how much of the take sounds.
2. `src/tracks/SampleTrack.cpp:126-130` - `activeTakeForPlayback()` does admit every clip of a
   take group while a composite (or an audition) is active. That is the design, not the defect:
   every take of the group is scheduled and each one gates *itself*.
3. `src/core/SamplePlayHandle.cpp:350-403` - `snapshotComp()`, run in the clip constructor
   (`:226`) for **every** clip handle, stores the composite's spans **for this clip's own lane**
   (`if (segment.laneIndex != lane ...) { continue; }`, `:377`) clamped to the clip window
   (`:378-380`), converted to output frames from the clip's start, and sets `m_comped`. A take
   whose lane no span names gets `m_compSpanCount == 0`.
4. `src/core/SamplePlayHandle.cpp:406-426` - `applyComp()` then multiplies every frame of the
   period by a gain that is `0` unless the frame lies inside one of those spans
   (`:412 float gain = 0.0f;` `:416 if (at < from || at >= to) { continue; }`), i.e. everything
   outside the composite's span for this lane is silenced. The 64-frame ramp (`:420-421`) is
   computed from the *inside* edge of a contiguous run, so it never leaks audio past a span.
5. `src/core/SamplePlayHandle.cpp:655-658` - `play()` runs `applyComp()` on every comped handle,
   after the clip's fades and take gate and before the clip's effects, for the whole life of the
   handle. So "outside the segment" is silence, not the take's own window.
6. Intended contract, stated where the feature lives: `include/TakeLane.h:38-40` - "The composite
   PLAYS: each take's play handle gates its lane's spans (R3.1)"; `docs/COMPING.md` §2 item 3 and
   `include/SamplePlayHandle.h` (`snapshotComp`/`applyComp` contract, `m_compSpans`).
7. History: the gate came first. `git log -S'applyComp' -- src/core/SamplePlayHandle.cpp` ->
   `8f1bbd8855` (Tue Sep 29 2026, "comp: the composite PLAYS, and a lane can be auditioned
   (R3.1)"); `git log -S'namedByComposite' -- src/tracks/SampleTrack.cpp` -> `50fc58f407`
   (Thu Oct 8 2026, cluster P6 round 4), and `git merge-base --is-ancestor 8f1bbd8855 50fc58f407`
   exits 0. The admission code was added on top of a gate that already limited playback, so the
   muted-take admission never produced the reported leak.

The suggested fix ("schedule only the part of a clip that contributes to the composite at the
current position") describes the shipped design in the wrong layer: the schedule creates one
handle per clip per pass and the handle limits its own render, which is what keeps the ramps,
the slipped reads (`include/CompSlip.h`) and the offline renders (`clip.consolidate`) consistent.

## Proof that the reported symptom is absent - and that these tests would catch it

Two tests were added to the existing area test file
`tests/src/tracks/TakeLaneCompPlaybackTest.cpp`:

- `aMutedTakeSoundsOnlyWhereTheCompositeSelectsItsLane` - the finding's own case through the real
  schedule: a muted take on lane 1, comp gapless with lane 1 over the middle third only. The pass
  admits it (`track.play()` returns true), and the render is `0.5000` inside that third,
  `0.0000` before and `0.0000` after. The same test then shows the admission is not "any lane":
  a muted take whose lane no segment names is not scheduled (`track.play()` false, no handle).
- `aGroupMemberTheCompositeDoesNotSelectRendersSilence` - the finding's second half: with a comp
  active both passes of a take group are scheduled (`selected->isPlaying() && unselected->isPlaying()`)
  and the unselected member renders `0.0000`, not its own `0.25` window.

Because no product change was needed, the tests pass both before and after; their value is that
they pin the exact behaviour the finding denies. To prove the oracle detects the reported defect
I temporarily replaced the gate's guard with `if (false && m_comped)` in
`src/core/SamplePlayHandle.cpp:655` (a scratch experiment, reverted - `git status` before it and
after the revert lists only the test file):

- with the gate removed (`logs/build-5.log`, `logs/test-7.log`):
  `MUTED_COMP_EVIDENCE comped[third,2third)=0.5000 before=0.5000 after=0.5000` ->
  `FAIL! 'before == 0.0' returned FALSE. (the muted take plays outside its comped span: 0.5)`
  and `GROUP_COMP_EVIDENCE selected=0.5000 unselected=0.2496` -> FAIL. Exit **2**, exactly the
  reported symptom: the take sounds over its whole window.
- with the gate restored (`logs/build-6.log`, `logs/test-8.log`): both PASS, exit **0**.

## Test matrix

| Path | Covered by | Result |
|---|---|---|
| normal: comped muted take sounds inside its lane's span | `aMutedTakeSoundsOnlyWhereTheCompositeSelectsItsLane` | PASS (0.5000 inside) |
| failure case of the report: audio outside the segment | same test, `before`/`after` slices = 0.0000 | PASS |
| admission edge: muted take whose lane no segment names | same test, second half (no schedule, no handle) | PASS |
| sibling admission: every group member scheduled while comped | `aGroupMemberTheCompositeDoesNotSelectRendersSilence` | PASS (`isPlaying` both, `unselected` = 0.0000) |
| reconfiguration: comp changed, audition toggled, no-comp track unchanged | existing `theCompositePlaysEachLaneOnlyWhereItIsSelected`, `auditioningALanePlaysItWholeAndSilencesTheOthers`, `aTrackWithNoCompositeRendersUnchanged`, `refuses*BeyondThePlaybackSpanBudget` | PASS |
| audio thread | no product code changed; no allocation/lock/log added anywhere | n/a |
| platforms | test uses QtTest + std C++ only (no Qt-6-only API, no POSIX paths); builds with the repo's toolchain | build EXIT 0 |

## Commands and EXIT codes (all logs in
`/home/kruzzzzy/.config/kads/zene/state/runs/new-p14-278.r1/logs`)

```
cmake --build build --target TakeLaneCompPlaybackTest -j4 > logs/build-4.log   EXIT 0
cd build/tests && ctest -R TakeLaneCompPlaybackTest --output-on-failure > logs/test-5.log  EXIT 0
build/tests/TakeLaneCompPlaybackTest aMutedTakeSoundsOnlyWhereTheCompositeSelectsItsLane \
    aGroupMemberTheCompositeDoesNotSelectRendersSilence > logs/test-6.log      EXIT 0
# oracle check: gate replaced by `if (false && m_comped)`, reverted after
cmake --build build --target TakeLaneCompPlaybackTest -j4 > logs/build-5.log   EXIT 0
build/tests/TakeLaneCompPlaybackTest <the two tests> > logs/test-7.log         EXIT 2  (both FAIL)
cmake --build build --target TakeLaneCompPlaybackTest -j4 > logs/build-6.log   EXIT 0
build/tests/TakeLaneCompPlaybackTest <the two tests> > logs/test-8.log         EXIT 0
cmake --build build --target zene -j4 > logs/build-7.log                       EXIT 0
cmake --build build --target TakeLaneTest TakeLanesPanelTest -j4 > logs/build-9.log  EXIT 0
cd build/tests && ctest -R TakeLane --output-on-failure > logs/test-11.log     EXIT 0  (4/4 passed)
python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/gates-1.log  EXIT 0
```

Gate table (`logs/gates-1.log`, unpiped EXIT 0):

```
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

## Self-check

- **Does the regression test FAIL with the product change reverted, and PASS with it?** There is
  no product change to revert (FALSE-POSITIVE): the change set is the two tests plus this report.
  The stronger evidence is the reverse experiment above - removing the *existing* gate
  (`src/core/SamplePlayHandle.cpp:655`) makes both tests FAIL with the reported numbers
  (`logs/test-7.log`, EXIT 2), and restoring it makes them PASS (`logs/test-8.log`, EXIT 0).
- **Determinism.** No sleeps, no wall-clock, no threads. The tests drive fixed ticks
  (`TimePos(0)`, `length/3`), `Engine::framesPerPeriod()`, and read the rendered frames; the
  level slices skip 512 frames clear of the 64-frame comp ramps.
- **Fix at the cause / siblings.** Nothing was changed, so nothing needs aligning; the whole
  `namedByComposite` caller set (one call site, `src/tracks/SampleTrack.cpp:281`) and the whole
  comp-gate family (`snapshotComp`, `applyComp`, `applyActiveTakeGate`, the audition branch, the
  `_clip_num >= 0` single-clip path) were read and are consistent with this verdict.
- **No collateral damage.** No product file differs from the branch's base; old projects, saved
  data and the render path are untouched. `git diff` touches `tests/` and `docs/` only.
- **ALREADY-FIXED / FALSE-POSITIVE evidence.** `src/tracks/SampleTrack.cpp:116-123` (admission
  only, one caller at `:281`), `src/tracks/SampleTrack.cpp:126-130` (group admission is by
  design), `src/core/SamplePlayHandle.cpp:350-403` (`snapshotComp` keeps only this lane's spans),
  `:406-426` (`applyComp` zeroes everything else), `:655-658` (it runs for every comped handle),
  contract `include/TakeLane.h:38-40`, and the history that the gate predates the admission
  (`git log -S`, `git merge-base --is-ancestor` above).

## Other findings (not fixed here)

1. **Audition is missing from the mute admission.** `namedByComposite()`
   (`src/tracks/SampleTrack.cpp:116-123`) looks only at `takeLanes().segments()`, while the
   handle's comp snapshot also honours `auditionLane()` (`src/core/SamplePlayHandle.cpp:362-372`).
   A MUTED take whose lane is auditioned but never named by a segment is therefore not scheduled,
   although auditioning a lane is documented to play that lane whole. Not this finding (that one
   is about too much playback, this one is about too little) and not fixed here.
2. **A muted take is scheduled when the composite names its lane only outside the clip's
   window.** `namedByComposite()` is not time aware, so such a take gets a handle that renders a
   fully gated silence. Audibly correct (the gate silences it), only a wasted handle.
