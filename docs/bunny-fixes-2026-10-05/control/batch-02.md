# Space Bunny fixer lane `control` — batch-02 report

Worktree `zene-bunny-control`, branch `bugs/bunny-control-2026-10-05`. Six findings triaged: three fixed
with red/green regression tests, one false positive, two real but not testable in this build.

### Cluster 3022 — Failed multi-device recreation can leave a partial copy

Verdict: FALSE-POSITIVE (by design — partial success is explicitly defined and reported)

Evidence: `recreateDevices` returns the ids it did create and sets `*error` on the first failure
(`src/core/ControlDeviceMoves.cpp:99-118`). The loop `break`s at `src/core/ControlDeviceMoves.cpp:107`
without unwinding, but it also records only the successful prefix
(`recordRecreation(chainOf, made, states.mid(0, made->size()))`, `src/core/ControlDeviceMoves.cpp:117`).
Both callers then turn that into an explicit partial-success reply: `src/core/ControlCommandsRackWrap.cpp:140`
inserts `stopped` with the reason, `:142` inserts `not_wrapped` with the count of devices that were not
wrapped, and the message at `:185` documents that contract; `src/core/ControlCommandsDeviceCopy.cpp:118-156`
does the same and `:227-229` opens a single `control.undo` transaction that puts every device back. The
suggested fix ("roll back, or define and report partial success explicitly") has already been taken: the
operation is deliberately not all-or-nothing and the undo transaction makes the partial state reversible.

Commit: none

Test: none

Red/green: none

### Cluster 4984 — Hosted effect presets share a directory across plugins

Verdict: REAL-UNTESTED

Evidence: `src/core/ControlDeviceState.cpp:70` derives the preset folder from the key's `plugin`
attribute, falling back to the host module name at `:71`. The VST3 key is built from `file` + `class`
(`src/core/ControlDeviceVst3.cpp:101-102,173-174`) and the CLAP key from `file` + `id`
(`src/core/ControlDeviceClap.cpp:104-108,181-182`); neither supplies `plugin`, so every VST3 effect
gets the same `vst3effect` folder and every CLAP effect the same `clapeffect` folder — the collision the
finding describes is real. It cannot be reproduced headlessly here: this build exposes exactly one
loadable VST3 fixture class (`WANT_VST3_TEST_INSTRUMENT`), no LV2 and no CLAP modules, and
`firstLoadableEffect()` finds no built-in effects, so a two-plugin collision needs a fixture the
repository does not contain. Per the procedure, product code was left alone rather than changed without
a deterministic failing test.

Commit: none

Test: none

Red/green: none

### Cluster 4986 — Any valid XML element can be reported as a restored instrument state

Verdict: FIXED

Evidence: `controlRestoreDeviceState` parsed the document and checked only whether the looked-up content
element was null, then called `loadPreset` and reported `restored: true`
(`src/core/ControlDeviceState.cpp:98-116` at the reported revision). `DataFile`'s content lookup
(`src/core/DataFile.cpp:2700`) resolves by the track's `type`, so malformed XML and `<foo/>` were already
refused — the real gap was a well-formed document whose root `type` resolves a *different valid* content
element, e.g. a saved project document (`<song>`), which was accepted as an instrument state. The fix
mirrors the sibling check in `src/core/ControlCommandsTrackPresets.cpp:85-90`: the content `type` must
parse and equal `handle.track->type()`, otherwise `InvalidArgs`. The `typeParsed` flag is needed because
`toInt()` returns 0 for a missing/non-numeric value and 0 is the instrument track type.

Commit: `f6e7b046b`

Test: `ReversibilityUndoTest::anInstrumentStateRestoreRefusesAForeignDocument`

Red/green: RED `ctest -R '^ReversibilityUndoTest$'` EXIT=8 (restore reported success over a saved project
document); GREEN same command EXIT=0. `bash tests/complexity-gate.sh --check` and
`bash tests/file-length-gate.sh --check` both EXIT=0.

Note: `ControlChainPresetTest` (3 cases) and `ReversibilityUndoTest::firstAutomationPointIsOneUndoableStep`
fail on this branch for an unrelated pre-existing reason — the build exposes no loadable built-in
effect/instrument parameters. Confirmed by fully reverting the source change and rebuilding.

### Cluster 4983 — Effect state restore accepts state from a different hosted plugin

Verdict: REAL-UNTESTED

Evidence: `controlEffectStateXml` writes `hosted_file` (`src/core/ControlDeviceSupport.cpp:475`),
`hosted_id` from the `plugin` attribute (`:476`) and `hosted_uri` from `uri` (`:477`). Restore compares
`hosted_id` against `key.attributes["plugin"]` (`:514-520`) and `hosted_uri` against `uri` (`:525-530`);
because the VST3 key carries `file`+`class` (`src/core/ControlDeviceVst3.cpp:101-102,173-174`) and the CLAP
key carries `file`+`id` (`src/core/ControlDeviceClap.cpp:104-108,181-182`), both compared values are empty
and the check at `:516` is skipped (`!hostedId.isEmpty()` is false), so A's state loads into B — the gap is
real and `hosted_file` is saved but never compared. Proving it needs two distinct hosted plugins in one
chain: this build has a single VST3 fixture class, no LV2 and no CLAP modules, and no loadable built-in
effects, so no deterministic headless failing test can be written. Per the procedure, no product code was
changed.

Commit: none

Test: none

Red/green: none

### Cluster 4045 — Note selection can shift to a different note after edits

Verdict: FIXED (scoped to the removal case the finding's own "when" describes)

Evidence: selection is stored as positions in the note list (`s_selectedNotes` /
`selectNotes` / `selectedNoteIndices`, `src/core/ControlEditSupport.cpp:250-285`, documented at
`include/ControlEdit.h:133`) and `rollState` re-reads them verbatim
(`src/core/ControlEditSupport.cpp:345-382`). `note.remove` deleted from the note list without remapping
the stored positions (`src/core/ControlCommandsNotes.cpp:163-216`), so every surviving selection index
above the removed position shifted onto the note that took its place. The fix remaps the positions inside
the command that moves them — drop the removed position, decrement higher positions — and writes back
through `control::selectNotes` (same module, `:480-515`). Switching to note IDs, as the finding suggests
first, would be a design change and is out of scope.

Commit: `668e80222`

Test: `ControlEditCommandsTest::aNoteRemovalDoesNotShiftTheSelectionOntoAnotherNote`

Red/green: RED `ctest -R '^ControlEditCommandsTest$'` EXIT=8
(`Actual (selectedNotes.size()): 0  Expected (1)`); GREEN same command EXIT=0, and all four note suites
`NoteEdit|NoteTools|NoteShape|ControlEditCommands` EXIT=0. Both gates EXIT=0.

### Cluster 4990 — Detailed render preset JSON omits its promised hash

Verdict: FIXED

Evidence: the header promises size *and* hash for detailed preset JSON
(`include/ControlExportPresetSupport.h:130-132`), but `controlExportPresetJson` inserted only `bytes`
(`src/core/ControlExportPresetSupport.cpp:297-313`). The fix reads the file and inserts `sha256` via the
existing `controlSha256OfBytes` helper, over the same bytes the `bytes` count is measured from, so
`export.preset_list name=…` entries are now comparable across an undo. The per-call `sha256` inserts in
`src/core/ControlCommandsExportPresets.cpp:204,298-299` are unaffected — they compute the same value.

Commit: `6215d0230`

Test: `RenderPresetMenuTest::aDetailedPresetListingCarriesTheDocumentHash`

Red/green: RED `ctest -R '^RenderPresetMenuTest$'` EXIT=8 (hash was `""`); GREEN same command EXIT=0,
plus `ControlRenderPresets` and `ControlCommandsSnapshot` EXIT=0. Both gates EXIT=0.

Leftovers: `src/core/ControlChainPresetSupport.cpp:503-522` has the same missing-`sha256` shape as cluster
4990 (its header at `include/ControlChainPresetSupport.h:186-187` promises size and hash) — outside this
batch's finding list, left untouched. Clusters 4983 and 4984 remain unfixed in product code and need a
two-plugin fixture (a second VST3 class, or a CLAP module) before they can be regression-tested.