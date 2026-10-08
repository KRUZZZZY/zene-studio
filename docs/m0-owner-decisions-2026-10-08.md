# M0 owner decisions, 2026-10-08

Three behaviour decisions the owner made on 2026-10-08, implemented on `050/m0-owner-decisions`
(from `28cc3a0b5`). Each row: what changed, the tests, the red/green exit codes, the gates, and
what is left open. Line numbers are in the tree at the commit named.

| # | Decision | What changed (file:line) | Tests | Red / green | Gates | Open |
|---|---|---|---|---|---|---|
| 1 | A trim or split of one member of a link group applies to every member, in one undo step (`2e2ab6953`, snapshot `a0b10c0b9`) | New `include/LinkedClipEdits.h`, `src/core/LinkedClipEdits.cpp` (`planEdgeEdit` :67, `applyEdgeEdit` :95, `splitGroupAt` :135, `adoptTrimmedContent` :159). `clip.trim` moves every peer's edges by the same deltas and refuses whole if a peer would be emptied (`src/core/ControlCommandsClipTrim.cpp:189,200`). `clip.split` cuts every member at the same clip-relative position; left halves keep the group, right halves get a new one (`src/core/ControlCommandsClip.cpp:325-331`). The knife cuts a linked clip through the same function (`src/gui/clips/ClipView.cpp:1596`, `src/gui/clips/MidiClipView.cpp:916`, so a shift-knife on a linked clip is the non-destructive cut). The left-edge drag captures the peers at the press and moves their edges at the release (`ClipView.cpp:125,134,742,1088`). Clear-notes-out-of-bounds writes the trimmed, re-based notes onto the peers and moves their start offsets with them (`MidiClipView.cpp:391,429`). `docs/LINKED-CLIPS.md` section 2 updated. | `LinkedClipEditsTest` (new, 8 cases: trim, undo, whole-group refusal, unlinked trim, split into two groups, undo, short member, unlinked split); `MidiClipLinkedEditTest` (+4 GUI cases: content trim, head-trim offset, knife with shift, left-edge drag with undo) | Red (product change reverted): `LinkedClipEditsTest` exit 8, `MidiClipLinkedEditTest` exit 8. Green: both 0; `ClipLinkTest`, `ClipLinkPersistenceTest` 0 | complexity 0, file-length 0, fork-sources 0, no-upstream 0 | See notes 1.1 to 1.4 |
| 2 | Crossfade into a contained clip: offset fade on the clip model (`54a4558c4`, snapshot `a0b10c0b9`) | `ClipEdits::fadeInOffsetTicks` / `fadeOutOffsetTicks` (`include/ClipEdits.h:116-117`); envelope `fadeInFactor` / `fadeOutFactor` / `clipFadeGainAt` (`src/core/ClipEdits.cpp:164-200`); saved as `fadeinoffset` / `fadeoutoffset` only when non-zero (`src/core/Clip.cpp:474-475`, read :641-642); render: `src/core/SamplePlayHandle.cpp:219-258,349` (no allocation, no lock: arithmetic on members); the drop-overlap crossfade sets the offset (`src/core/ArrangementEdits.cpp:321`); `clip.crossfade` sets both offsets and reports them, `clip.set_fade` takes `fade_in_offset` / `fade_out_offset` (`src/core/ControlCommandsClipEdits.cpp:149-150,288-292`); pre-fades are not engaged for a clip with an offset (`src/core/ClipPreFades.cpp:83`); overlay draws the ramps at their offsets (`src/gui/clips/SampleClipOverlays.cpp:47-50,109-110`). Commands snapshot regenerated from a live instance (822 ids / 76 groups, unchanged). | `ClipOffsetFadeTest` (new, 9 cases: container render, no allocation, `clip.crossfade` + undo, container as the incoming clip, envelope ends before the clip end, save/load round trip and absence when 0, old-project envelope bit for bit over 400k+ points) | Red A (crossfade writers reverted): exit 8. Red B (envelope ignores offsets): exit 8. Green: 0. Old-render check: the four `ClipFadesRenderTest` renders hash identically to the pre-change binary built at the same base (`zene-m0-train2`, `28cc3a0b5`): no-edits `fe7ab966...`, fade-in `3033bbc8...`, crossfade `94d35cba...` | complexity 0, file-length 0, fork-sources 0, no-upstream 0, evidence 0 (snapshot 1047681 of 1048576 bytes), doc-figures 0 | See notes 2.1 to 2.4 |
| 3 | A script's removal that cannot be undone is refused (`40cdcca78`) | `applyRemoveEffect` returns with an `[error] removing effect N refused: ...` log line and leaves the device when `journalEffectRemoval` fails (`src/core/ScriptDawEdit.cpp:231`). Test seam `control::setRemovalCaptureFailsForTest` (`include/ControlStructuralSupport.h:146`, `src/core/ControlStructuralSupport.cpp:510-529`). `docs/LUA-API-STABILISATION.md` notes it. | `ScriptDawRemovalUndoTest::aScriptsRemovalThatCannotBeUndoneIsRefused` (all 13 cases ran, none skipped, after building the `amplifier` module fixture) | Red (old removal restored): exit 8 (chain emptied, expected 1). Green: 0. Shared suites `DeviceMovesUndoTest`, `UndoHonestyTest`, `UndoBoundsTest`, `ReversibilityContractTest`, `ScriptDawBindingTest`: 0 (after building `bassbooster`) | complexity 0, file-length 0, fork-sources 0, no-upstream 0 | See notes 3.1 to 3.2 |

Also green after the three changes (targeted, from `build/tests`): `ControlCommandsSnapshot`,
`ControlResultSchemaTest`, `ControlEditCommandsTest`, `ArrangementModesTest`, `ClipEditsTest`,
`ClipSerialisationTest`, `AudioEditingTest`, `ControlVcaLockedEditsTest`, `ControlVerbInverseTest`,
`ClipGainEnvelopeTest`, `W3EngineTest`, `CrossfadeEditorTest`, `ClipEditsMenuTest`, `ClipTransposeTest`,
`ClipConsolidateTest`, `RangeEditsTest`, `ClipJoinRippleTest`, `ControlAudioEditing`, and the new tests
with `ZENE_CONTROL_CHECK_RESULTS=1` (every new reply field is in its result schema).

## Notes and what is left open

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
