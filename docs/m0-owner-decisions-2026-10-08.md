# M0 owner decisions, 2026-10-08

Three behaviour decisions the owner made on 2026-10-08, implemented on `050/m0-owner-decisions`
(from `28cc3a0b5`). Each row: what changed, the tests, the red/green exit codes, the gates, and
what is left open. Line numbers are in the tree at the commit named.

| # | Decision | What changed (file:line) | Tests | Red / green | Gates | Open |
|---|---|---|---|---|---|---|
| 1 | A trim or split of one member of a link group applies to every member, in one undo step (`2e2ab6953`, snapshot `a0b10c0b9`) | New `include/LinkedClipEdits.h`, `src/core/LinkedClipEdits.cpp` (`planEdgeEdit` :67, `applyEdgeEdit` :95, `splitGroupAt` :135, `adoptTrimmedContent` :159). `clip.trim` moves every peer's edges by the same deltas and refuses whole if a peer would be emptied (`src/core/ControlCommandsClipTrim.cpp:189,200`). `clip.split` cuts every member at the same clip-relative position; left halves keep the group, right halves get a new one (`src/core/ControlCommandsClip.cpp:325-331`). The knife cuts a linked clip through the same function (`src/gui/clips/ClipView.cpp:1596`, `src/gui/clips/MidiClipView.cpp:916`, so a shift-knife on a linked clip is the non-destructive cut). The left-edge drag captures the peers at the press and moves their edges at the release (`ClipView.cpp:125,134,742,1088`). Clear-notes-out-of-bounds writes the trimmed, re-based notes onto the peers and moves their start offsets with them (`MidiClipView.cpp:391,429`). `docs/LINKED-CLIPS.md` section 2 updated. | `LinkedClipEditsTest` (new, 8 cases: trim, undo, whole-group refusal, unlinked trim, split into two groups, undo, short member, unlinked split); `MidiClipLinkedEditTest` (+4 GUI cases: content trim, head-trim offset, knife with shift, left-edge drag with undo) | Red (product change reverted): `LinkedClipEditsTest` exit 8, `MidiClipLinkedEditTest` exit 8. Green: both 0; `ClipLinkTest`, `ClipLinkPersistenceTest` 0 | complexity 0, file-length 0, fork-sources 0, no-upstream 0 | See notes 1.1 to 1.4 |
| 2 | Crossfade into a contained clip: offset fade on the clip model (`54a4558c4`, snapshot `a0b10c0b9`) | `ClipEdits::fadeInOffsetTicks` / `fadeOutOffsetTicks` (`include/ClipEdits.h:116-117`); envelope `fadeInFactor` / `fadeOutFactor` / `clipFadeGainAt` (`src/core/ClipEdits.cpp:164-200`); saved as `fadeinoffset` / `fadeoutoffset` only when non-zero (`src/core/Clip.cpp:474-475`, read :641-642); render: `src/core/SamplePlayHandle.cpp:219-258,349` (no allocation, no lock: arithmetic on members); the drop-overlap crossfade sets the offset (`src/core/ArrangementEdits.cpp:321`); `clip.crossfade` sets both offsets and reports them, `clip.set_fade` takes `fade_in_offset` / `fade_out_offset` (`src/core/ControlCommandsClipEdits.cpp:149-150,288-292`); pre-fades are not engaged for a clip with an offset (`src/core/ClipPreFades.cpp:83`); overlay draws the ramps at their offsets (`src/gui/clips/SampleClipOverlays.cpp:47-50,109-110`). Commands snapshot regenerated from a live instance (822 ids / 76 groups, unchanged). | `ClipOffsetFadeTest` (new, 7 cases: container render, no allocation, `clip.crossfade` + undo, container as the incoming clip, envelope ends before the clip end, save/load round trip and absence when 0, old-project envelope bit for bit over 400k+ points) | Red A (crossfade writers reverted): exit 8. Red B (envelope ignores offsets): exit 8. Green: 0. Old-render check: the four `ClipFadesRenderTest` renders hash identically to the pre-change binary built at the same base (`zene-m0-train2`, `28cc3a0b5`): no-edits `fe7ab966...`, fade-in `3033bbc8...`, crossfade `94d35cba...` | complexity 0, file-length 0, fork-sources 0, no-upstream 0, evidence 0 (snapshot 1047681 of 1048576 bytes), doc-figures 0 | See notes 2.1 to 2.4 |
| 3 | A script's removal that cannot be undone is refused (`40cdcca78`) | `applyRemoveEffect` returns with an `[error] removing effect N refused: ...` log line and leaves the device when `journalEffectRemoval` fails (`src/core/ScriptDawEdit.cpp:231`). Test seam `control::setRemovalCaptureFailsForTest` (`include/ControlStructuralSupport.h:146`, `src/core/ControlStructuralSupport.cpp:510-529`). `docs/LUA-API-STABILISATION.md` notes it. | `ScriptDawRemovalUndoTest::aScriptsRemovalThatCannotBeUndoneIsRefused` (all 13 cases ran, none skipped, after building the `amplifier` module fixture) | Red (old removal restored): exit 8 (chain emptied, expected 1). Green: 0. Shared suites `DeviceMovesUndoTest`, `UndoHonestyTest`, `UndoBoundsTest`, `ReversibilityContractTest`, `ScriptDawBindingTest`: 0 (after building `bassbooster`) | complexity 0, file-length 0, fork-sources 0, no-upstream 0 | See notes 3.1 to 3.2 |

Also green after the three changes (targeted, from `build/tests`): `ControlCommandsSnapshot`,
`ControlResultSchemaTest`, `ControlEditCommandsTest`, `ArrangementModesTest`, `ClipEditsTest`,
`ClipSerialisationTest`, `AudioEditingTest`, `ControlVcaLockedEditsTest`, `ControlVerbInverseTest`,
`ClipGainEnvelopeTest`, `W3EngineTest`, `CrossfadeEditorTest`, `ClipEditsMenuTest`, `ClipTransposeTest`,
`ClipConsolidateTest`, `RangeEditsTest`, `ClipJoinRippleTest`, `ControlAudioEditing`, and the new tests
with `ZENE_CONTROL_CHECK_RESULTS=1` (every new reply field is in its result schema).

## Notes and what is left open (round 1; round 2 below supersedes notes 1.1, 1.2, 1.3 in part, 2.1 and 2.2)

**1.1 Meaning of "trim".** Three paths are trims and all three now apply to the group: `clip.trim` and
the left-edge drag move edges (each peer keeps its own position; only the edge moves by the same delta,
and the source offset moves against the start, `clip.trim`'s own head-trim rule); the arranger's
clear-notes-out-of-bounds trims content. `clip.resize` and a right-edge drag are not changed: they stay
per-member, as `docs/LINKED-CLIPS.md` always said. If the owner wants the tail drag to propagate too, it is
the same `applyEdgeEdit` call in `ClipView::mouseReleaseEvent` for `Action::Resize`.

**1.2 Destructive cut.** A shift-knife on a linked clip now makes the plain (non-destructive) cut on every
member. A destructive cut of shared content cannot keep the members identical when their start offsets
differ, which is what the decision forbids. An unlinked clip keeps its destructive cut. Every other split
that goes through `arrangement::splitClipAt` (range split, `vca.edit_split`, `clip.split_at`, loop slicing)
is not group-aware; those clones keep the whole note list and the old group, so content cannot diverge, but
the peers are not cut.

**1.3 Peers that cannot follow.** A member too short to be cut at the position stays whole in the left group;
a lone right half is unlinked (`right_group` 0). A drag whose trim a peer cannot take puts the dragged clip
back (reported nowhere but the clip returning); the control command refuses with `invalid_args` instead.

**1.4 Peers on the same track.** Splits and the content trim capture the tracks of every member (a track
restore re-creates its clips). Trims capture the clips. Both are one `ProjectJournal` step in the command
path through the registry's merge, and in the GUI through the multi-object checkpoint or
`mergeCheckpointsFrom`. The GUI undo of the left-edge drag is tested; the GUI undo of the knife and of the
content trim is not (the command-path undo of the split is).

**2.1 What the container does.** The container's fade-out ends where the contained clip ends and the rest
of the container plays at unity. At the contained clip's end the container's gain steps from 0 to 1 under
the contained clip's last (unity) frame; that is the decision's behaviour (tail not faded) and is where a
click would be audible if the two sources differ in level. The symmetric case (the contained clip is `out`,
the container `in`) gets a fade-in start offset: the container is unfaded before the ramp.

**2.2 Not covered.** The offsets are not in `arrangement.get_state` / `clip.list` clip entries (the commands
snapshot is 0.9 KB under the 1 MiB evidence cap); they are in the `clip.set_fade`, `clip.set_gain` and
`clip.crossfade` replies and in `clip.fade_curve`. Pre-fades (`clip.pre_fade`) are not applied to a clip that
has an offset fade. The fade handles of such a clip are drawn at their offsets but are not draggable (edit
with `clip.set_fade`). The crossfade editor dialog was not changed and its use on a containing pair is not
tested. The pre-change hash comparison above was run by hand against a binary of the same base commit; the
registered test carries the platform-independent form (the dense bit-for-bit envelope check), not a golden
hash, because a render hash is not stable across compilers.

**2.3 MIDI clips.** Fades still apply to audio clips only; a MIDI clip's `ClipEdits` carries the offsets
(saved and loaded) but nothing renders them, as before for its fades.

**2.4 Snapshot.** Regenerated from a live instance of this tree through the harness, as the brief says.

**3.1 Seam scope.** `setRemovalCaptureFailsForTest` makes `journalRemovalDocument` fail for every caller,
including `plugin.unload` (whose `reversible:false` branch it therefore also reaches); only the script test
uses it. It is `LMMS_EXPORT` and test-only by name, like `setRevisionReplacementFailureForTest`.

**3.2 Not covered.** The `LuaEffect` raw-chain-pointer item of the script lane (REAL-UNTESTED) is untouched.

**Not run.** The full `ctest` suite and `tests/run-all-gates.sh` were not run (the brief forbids the full
suite). Gates were run one at a time; gates 2 (coverage) and 5 (mutation) were not run. The build/ tree
needed the `amplifier` and `bassbooster` plugin fixtures built to run the script and device tests; those
are build-tree artefacts, not committed.

## Round 2 (after the independent review)

The owner's rule is "a trim or split applies to all members"; the old per-member length rule in
`docs/LINKED-CLIPS.md` is superseded and the doc is updated.

| # | Review finding | Verdict | Commit | What changed (file:line) | Tests | Red / green |
|---|---|---|---|---|---|---|
| 1a | Tail drag and `clip.resize` changed one member | FIXED | `f971dd288` | `clip.resize` plans and applies the group's end delta (`src/core/ControlCommandsClip.cpp`, resize handler); the right-edge drag is captured and applied like the left (`src/gui/clips/ClipView.cpp:125-160`, `beginLinkedTrim(.., ResizeLeft or Resize, ..)`) | `LinkedClipGroupEditsTest::aResizeOfOneMemberResizesEveryMemberInOneStep`, `...aResizeThatWouldEmptyAPeerIsRefusedWholeAndMovesNothing`; `MidiClipLinkedEditTest::aRightEdgeDragResizesEveryLinkedMember` | red 8 and 8, green 0 and 0 |
| 1b | Range split, `vca.edit_split`, loop slicing did not reach peers | FIXED | `f971dd288` | `arrangement::splitClipAt` is group-aware, `splitClipAlone` is the primitive (`src/core/ArrangementEdits.cpp:58-75`); `vca.edit_split` uses it and never cuts a peer twice, `vca.edit_trim` carries the lock's trim to linked peers outside the lock (`src/core/ControlCommandsVcaEditMore.cpp`, `cutLockedClip`) | `LinkedClipGroupEditsTest::aRangeSplitReachesTheLinkedClipsOnOtherTracks`, `splitClipAtCutsTheGroup`, `aLockedSplitCutsTheLinkedPeersOutsideTheLockAndNeverTwice`, `aLockedTrimReachesTheLinkedPeersOutsideTheLock`, `anOverlapTrimLeavesThePeersWhole` | red 8, green 0 |
| 1c | Too-short peers stay whole | DOCUMENTED | `f971dd288` | Only a legacy group (members of different length) can have a member shorter than the cut position; documented in `include/LinkedClipEdits.h` and `docs/LINKED-CLIPS.md` | n/a | n/a |
| 1d | One undo step for the two checkpoints on the trim path | VERIFIED | `f971dd288`, `3da14ad62`.. | `ControlRegistry.cpp:416` merges the clip's and the peers' checkpoints; asserted as `undoDepth() == before + 1` for `clip.trim`, `clip.resize`, `clip.split`, `range.split`, `vca.edit_split` | `LinkedClipGroupEditsTest::aTrimIsOneUndoStep`, `aResizeOf...`, `aGroupSplitIsOneUndoStep`, `aRangeSplit...`, `aLockedSplit...`; GUI: `MidiClipLinkedEditTest::aRightEdgeDrag...` | green 0 (the merge already held; these pin it) |
| 1e | A peer's start offset must never come out invalid | FIXED | `f971dd288` | `peerRefusal` (`src/core/LinkedClipEdits.cpp:67-90`) refuses the whole edit when a peer would be emptied, start before the song, or be extended before its own content while the edited clip stays inside its content. Note: a NEGATIVE offset is the normal representation of a head trim (the edited clip gets one too) and is not refused; the invalid state is a peer offset above 0 | `...aTrimThatWouldPushAPeerBeforeTheSongStartIsRefused`, `...aTrimThatWouldExtendAPeerBeforeItsContentIsRefused` | red 8 (extension), green 0 |
| 1f | A refused GUI drag was silent | FIXED | `f971dd288` | the dragged clip goes back and a message says why (`ClipView.cpp`, `endLinkedTrim`) | `MidiClipLinkedEditTest::aDragAPeerCannotFollowGoesBack` (the revert; the message is not asserted) | red 8, green 0 |
| 2a | The container's gain stepped from 0.01 to 1 at the end of the offset | FIXED | `5f2532806` | A clip inside another is crossfaded at both ends: the container's fade-out (offset) is followed by a fade-in (offset) that starts at that frame, so its envelope is a V (`include/ContainedCrossfade.h`, `src/core/ContainedCrossfade.cpp`; used by `crossfade()` in `src/core/ArrangementEdits.cpp:318-345` and by `clip.crossfade`, `src/core/ControlCommandsClipEdits.cpp`, `containedSides`). Exit ramp = the contained clip's own fade-out when set, else half; `out_length` / `in_length` name the ramp into it (or out of it when the container is `in`), the other is the rest. The render puts the fade-out's end at the frame its tick falls on (`src/core/SamplePlayHandle.cpp:219-226`): a one-frame gap at unity (a 0.5 step) was found by the new render test and fixed | `ClipOffsetFadeTest::aContainerDucksUnderTheContainedClipAndComesBack` (renders, checks the closed form at both crossfades, and that no two neighbouring samples of the render differ by 0.01), `theEnvelopeIsContinuousAndEqualPowerThroughBothCrossfades`, `theCrossfadeCommandUsesTheRampsAndUndoRestoresThem`, `aContainedClipsOwnFadeOutIsTheExitRamp`; the old encoding of the step is replaced by `anOffsetFadeOutRampSitsBeforeTheClipEnd` (region facts only) | red 8 for each of: containment off, frame rounding reverted (the 0.5 step), pre-fade exclusion, handle offset; green 0 |
| 2b | Pre-fades silently disabled with an offset fade | FIXED | `5f2532806` | `prefades::engaged` no longer excludes offsets (`src/core/ClipPreFades.cpp:83`); `preFadedGainAt` has offset-aware sides (`src/core/ClipEdits.cpp`, `preFadedIn` / `preFadedOut`); the automatic edge fade stays in the pre-faded path | `ClipOffsetFadeTest::aPreFadeStillAppliesToAClipWithAnOffsetFade`, `anOldProjectsPreFadedEnvelopeIsBitForBitUnchanged` | red 8, green 0 |
| 2c | Offset fade handles were not draggable | FIXED | `5f2532806` | `fadeHandleUnder` no longer refuses; `fadeTicksAt` takes the ramp's offset and the drag is bounded by the clip, not by the other ramp, when offsets exist (`src/gui/clips/SampleClipOverlays.cpp`, `SampleClipView.cpp`) | `ClipOffsetFadeTest::theFadeHandlesOfAnOffsetFadeCanBeDragged` (geometry and drag arithmetic; the mouse drag on the view is not driven by a test) | red 8, green 0 |
| 2d | Old projects bit-identical | VERIFIED | `5f2532806` | the clamp is split out (`SamplePlayHandle::fitFades`) and equals the old one with no offsets | dense bit-for-bit checks of `clipFadeGainAt` and `preFadedGainAt` against their previous bodies; `ClipFadesRenderTest` renders hash identically to the pre-change binary (`fe7ab966...`, `3033bbc8...`, `94d35cba...`) | green 0 |
| 3 | Does the refusal reach the script as a value? | FIXED (it did not) | `76114e409` | `LuaEffectChain::removeEffect` returns `bool` (`src/core/ScriptDawEdit.cpp:395-406`, `include/ScriptDawBindings.h`): `false` when refused or the index is bad, judged by the effect count after the flush | `ScriptDawRemovalUndoTest::aScriptsRemovalThatCannotBeUndoneIsRefused` asserts the script printed `removeEffect returned false`, then `true` once capture works | red 8 (return forced true), green 0 |

Gates at the round-2 HEAD, all exit 0: complexity, file-length, fork-sources, unregistered-tests, no-tautology,
no-upstream-regression, duplication, evidence (the commands snapshot is 1047895 bytes, 681 under the 1 MiB cap),
doc-figures. 31 targeted tests exit 0 together, run with `ZENE_CONTROL_CHECK_RESULTS=1` (every reply field is in
its schema). Not run: the full suite and gates 2 and 5.

**Open after round 2.**
- The container's own edge fades (fade-in at its head, fade-out at its tail) are replaced by the V when it is crossfaded
  with a contained clip.
- A contained clip shorter than 2 ticks cannot be crossfaded at both ends: `clip.crossfade` refuses it; the arranger's
  drop leaves it uncrossfaded.
- The offsets are still not in `arrangement.get_state` / `clip.list` (snapshot size); read them from `clip.set_fade`,
  `clip.set_gain`, `clip.crossfade` replies or `clip.fade_curve`.
- A drop that trims the clip under it (overlap mode Trim) still cuts that clip only, on purpose: its peers are not under
  the drop. The destructive shift-knife on a linked clip is the non-destructive cut (round 1 note 1.2).
- Every `splitClipAt` of a linked clip takes a checkpoint over the members' tracks, so a command that slices one linked
  clip into N pieces pushes N checkpoints that the registry merges; large N costs serialising the tracks N times.
- The mouse drag of an offset fade handle and the GUI knife's undo are not driven by a test.

## Round 3 (after the round-2 re-review: ACCEPT 5, ACCEPT-NOTE 7, REWORK 1)

| # | Finding | Verdict | Commit | What changed (file:line) | Tests | Red / green |
|---|---|---|---|---|---|---|
| R1 | Overlap-mode Trim drop cuts only the clip under it | RULED LOCAL, rule documented | `cd63ef3f5` | The drop stays a positional edit at one place; the piece that keeps the original instance (left) stays in its link group, a piece cut off to its right leaves it (unlinked, as a lone right half in a group split) (`src/core/ArrangementEdits.cpp`, `overlapOne`); `docs/LINKED-CLIPS.md` states the rule next to the group rule | `LinkedClipGroupEditsTest::anOverlapTrimLeavesThePeersWholeAndTheLeftPieceInTheGroup` (peers whole, left piece and peer still one group of 2, tail unlinked) | red 8, green 0 |
| R2 | A lone `fade_out_offset` steps the gain | FIXED by REFUSING the lone offset, and repairing a drifted pair | `cd63ef3f5` | `withValidOffsetFades` (`src/core/ClipEdits.cpp`): offsets are a pair (both set, both ramps above 0); a lone one is dropped, a pair whose fade-in does not start where the fade-out ends gets its fade-in moved there. Applied in `clip.set_fade` (refuses an untouched-by-the-caller-excepted lone or mismatched offset, `ControlCommandsClipEdits.cpp`, `offsetFadesArePaired`), project load (`Clip.cpp`, `loadClipEdits`: a lone offset loads as the ordinary fades), the render (`SamplePlayHandle::snapshotClipEdits`), the gain curve (`clipGainCurveAt`) and the GUI (`SampleClipView::shownEdits`, handle hit test and drag) | `ClipOffsetFadePairTest`: `setFadeRefusesALoneOffsetAndAcceptsAPair`, `aLoneOffsetLoadsAsTheOrdinaryFades`, `theGainCurveHasNoStep` (lone, drifted after a resize, and proper pair: no tick-to-tick step above one ramp's slope), `theRenderHasNoStep` (a played handle, lone and drifted) | red 8 for each of: pairing off, load check off; the render case alone fails at 0.4999 with the repair off; green 0 |
| R3 | A contained clip under 2 ticks: `applyOverlap` reported 1 | FIXED | `cd63ef3f5` | `applyOverlap` counts only crossfades made and returns the reason through `skipped` (`ArrangementEdits.cpp`, `crossfade`, `overlapOne`, `applyOverlap`); `clip.move` replies `overlap_skipped` (`ControlCommandsClip.cpp`); the arranger shows "Crossfade not made" (`ClipView.cpp`, `announceSkippedOverlap`); `clip.crossfade` already refused it with the reason | `ClipOffsetFadePairTest::aContainedClipUnderTwoTicksReportsNoCrossfade` (0 and the reason, no edits written, command refusal, `clip.move` reply, a 2-tick clip still crossfaded) | red 8, green 0 |

Snapshot regenerated in `c851e8285`: 1048029 bytes, 547 under the 1 MiB cap. Gates at the round-3 HEAD, all exit 0: complexity,
file-length, fork-sources, unregistered-tests, no-tautology, no-upstream-regression, duplication, evidence, doc-figures.
Targeted tests (30, run with `ZENE_CONTROL_CHECK_RESULTS=1`) exit 0, `ClipFadesRenderTest` hashes are still identical to
the pre-change binary. `ArrangingCatchUpTest::queuedInstrumentHandlesAreRemovedBeforeTheirTrackCanDie` fails in a build tree
without the `tripleoscillator` module (it loads a dummy instrument) and passes once that target is built; it is unrelated to
these changes.

**Open after round 3.** A clip with a stale pair is repaired where it is used, not rewritten in the project, so
`clip.set_fade`/`clip.list` replies still show the stored offsets after a resize until the clip is crossfaded again. Frames
of the container and the contained clip are rounded per clip, so the two ramps of a crossfade can meet one sample apart
across the two clips (not measured).
