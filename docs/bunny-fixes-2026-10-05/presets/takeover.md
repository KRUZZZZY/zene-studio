Presets lane takeover — Space Bunny, 2026-10-05

Baseline of the takeover: `9c7a95196` (the Luna WIP). The findings below are
Luna's hypotheses; each was decided against the current source and proved by a
build and a ctest run of this worktree, not by reading the handoff.

Red evidence used throughout is MY OWN: with all five WIP source repairs
un-applied in the working tree (temporarily, then restored with
`git checkout -- src/core/...`), the lane's two test targets fail exactly as
below.

  cmake --build build --target zene ControlChainPresetTest RenderPresetMenuTest  EXIT=0
  cd build/tests && ctest -R '^(ControlChainPresetTest|RenderPresetMenuTest)$' \
    --output-on-failure                                                          EXIT=8
  (ControlChainPresetTest 10 passed / 8 failed, RenderPresetMenuTest 3 passed / 2 failed)

Green, with the repairs in place:

  cmake --build build --target zene ControlChainPresetTest RenderPresetMenuTest  EXIT=0
  cd build/tests && ctest -R '^(ControlChainPresetTest|RenderPresetMenuTest)$' \
    --output-on-failure                                                          EXIT=0
  (ControlChainPresetTest 18 passed / 0 failed / 0 skipped, RenderPresetMenuTest 5 / 0 / 0)

No case skipped: the catalogue held several loadable effects, so
aCapturedChainAppliesToASecondTrackWithTheSameDevicesAndValues ran for real, and
the two permission fixtures were observable on this runner (uid 1000).

### Cluster 93 — chain.apply destroys the target before the replacement is ready

Verdict: FIXED (repair landed in `9c7a95196`; verified, not re-committed)

Evidence: `src/core/ControlChainPresetSupport.cpp:422-481` stages every device
into a scratch `EffectChain`, restores each device's state there, and only then
drops the live chain's effects and transfers ownership
(`stagedChain.removeEffect` → `effect->setParent(chain)` → `chain->appendEffect`);
`src/core/ControlCommandsChainEdit.cpp:117-131` records `addUndoStep` at line 123,
AFTER `controlApplyChainPreset` reports success at line 117. Pre-fix the drop came
first and the action was recorded first, so a device that could not be
instantiated or restored left the target empty with a stale undo step. The
scratch-chain pattern is the tree's own: `src/core/ControlCommandsDeviceTools.cpp:224`
already stacks an `EffectChain scratch(nullptr)` for the same purpose. Ownership is
sound because `EffectChain::clear()` (src/core/EffectChain.cpp) deletes exactly
what is still in `m_effects`, and a transferred effect has already been removed
from it; `appendEffect` re-points the effect at the real chain
(`_effect->setEffectChain(this)`).

Commit: 9c7a95196 (WIP; see Leftovers on why it could not be split out)

Test: ControlChainPresetTest::aFailedApplyKeepsTheOriginalDevicesAndHistory

Red/green:
  pre-fix  ctest -R '^(ControlChainPresetTest|RenderPresetMenuTest)$'  EXIT=8
    FAIL aFailedApplyKeepsTheOriginalDevicesAndHistory
    (failed apply left chain unchanged=0 and history unchanged=0) — both halves
    confirmed independently: the chain lost its device AND the journal advanced
  post-fix ctest -R '^(ControlChainPresetTest|RenderPresetMenuTest)$'  EXIT=0

### Cluster 94 — the reader ignored the writer's own `devices` attribute

Verdict: FIXED (repair landed in `9c7a95196`; its gate-compliance half is `09ca26188`)

Evidence: `src/core/ControlChainPresetSupport.cpp:241` writes
`devices="<count>"`; pre-fix `controlReadChainPreset` never read it, so a
truncated or hand-edited document was read as a shorter device list than it
claimed to be. The check now runs through the file-local helper
`checkDeclaredDeviceCount` (`src/core/ControlChainPresetSupport.cpp:76-108`, called
at line 284). The split was required, not cosmetic: with the check inline, lizard
reported `controlReadChainPreset` at CCN 13 and
`bash tests/complexity-gate.sh --check` returned EXIT=1
(`REGRESSION: new function over target`), which rule 12 forbids in a committed tree.

Commit: 09ca26188 (helper split, CCN 13 → within target); the validation itself
is in 9c7a95196

Test: ControlChainPresetTest::aPresetRefusesADamagedDeviceCount (rows 2, 0, -1, invalid, missing)

Red/green:
  red    ctest -R '^ControlChainPresetTest$'            EXIT=8  (5 rows FAIL `'!read' returned FALSE`)
         bash tests/complexity-gate.sh --check         EXIT=1  (CCN 13 regression)
  green ctest -R '^ControlChainPresetTest$'            EXIT=0
         bash tests/complexity-gate.sh --check         EXIT=0
         bash tests/file-length-gate.sh --check        EXIT=0

### Cluster 104 — chain.save overwrote a preset it could not read back

Verdict: FIXED (repair landed in `9c7a95196`)

Evidence: `src/core/ControlCommandsChain.cpp:160` —
`if (*existed && !controlReadFileBytes(path, &previous, error)) { return false; }`.
Pre-fix the read's result was discarded, so a revision that could not be captured
was still overwritten, and the recorded inverse (`recordStoreWrite`, line 124,
called at line 170) would then have restored empty bytes. Capture-before-overwrite
is also the rule the function's own doc comment states (lines 139-147).

Commit: 9c7a95196

Test: ControlChainPresetTest::anUnreadablePresetCannotBeOverwritten(chain)

Red/green:
  pre-fix  ctest -R '^ControlChainPresetTest$'  EXIT=8
    FAIL (overwrite refused=0, file unchanged=1, history unchanged=0)
  post-fix ctest -R '^ControlChainPresetTest$'  EXIT=0

### Cluster 159 — export.preset_add overwrote a preset it could not read back

Verdict: FIXED (repair landed in `9c7a95196`); the same defect as 104 on the export side

Evidence: `src/core/ControlCommandsExportPresets.cpp:146` — the identical guard
in `writePresetDocument`, with the same discarded-result bug pre-fix.

Commit: 9c7a95196

Test: ControlChainPresetTest::anUnreadablePresetCannotBeOverwritten(export)

Red/green:
  pre-fix  ctest -R '^ControlChainPresetTest$'  EXIT=8
    FAIL (overwrite refused=0, file unchanged=1, history unchanged=0)
  post-fix ctest -R '^ControlChainPresetTest$'  EXIT=0

### Cluster 160 / 1074 — export.preset_remove deleted the file before parsing it

Verdict: FIXED (duplicate sighting of one defect; repair landed in `9c7a95196`)

Evidence: `src/core/ControlCommandsExportPresets.cpp:445` parses the bytes into a
`ControlExportPreset` and refuses (typed InvalidArgs) BEFORE the unlink at line 450.
Pre-fix the unlink ran first and the parse refusal came after, so a corrupt
document was deleted and nothing reported it. `chain.remove` was already correct
pre-fix and is the reference implementation
(`src/core/ControlCommandsChainEdit.cpp:299-312`: read → unlink → record).

Commit: 9c7a95196

Test: RenderPresetMenuTest::aCorruptRenderPresetIsNotDeleted

Red/green:
  pre-fix  ctest -R '^RenderPresetMenuTest$'  EXIT=8
    FAIL (corrupt removal refused=1, file unchanged=0, history unchanged=0) — the
    refusal was reported AND the file was gone: exactly the delete-then-parse order
  post-fix ctest -R '^RenderPresetMenuTest$'  EXIT=0

### Cluster 1073 — export.preset_remove recorded an undo step for an unlink that never happened

Verdict: FIXED (repair landed in `9c7a95196`)

Evidence: `src/core/ControlCommandsExportPresets.cpp:450-463` records `addUndoStep`
(line 458) only after `QFile::remove` has returned true. Pre-fix the step went in
before the unlink, so a failed delete left history claiming a removal that never
occurred, and the store no longer matched its own recorded snapshot.

Commit: 9c7a95196

Test: RenderPresetMenuTest::aFailedRenderPresetDeleteLeavesNoHistory

Red/green:
  pre-fix  ctest -R '^RenderPresetMenuTest$'  EXIT=8
    FAIL (failed deletion refused=1, file unchanged=1, history unchanged=0)
  post-fix ctest -R '^RenderPresetMenuTest$'  EXIT=0

Leftovers: the five repairs verified above live in the single WIP commit
`9c7a95196` rather than in one commit each — the lane guard refuses
`git revert` ("not allowed in a Bunny lane"), so the source could not be split
back out without rewriting history, which rule 1 forbids. Only the
gate-compliance repair (`09ca26188`) is a separate commit. Non-blocking
observations, not fixed: `controlChainPresetDocument`
(`src/core/ControlChainPresetSupport.cpp:251`) still `continue`s past a device
whose state does not parse, which now yields a document its own reader refuses on
the count check — unreachable in practice, because a captured device's state is
always the XML `controlEffectStateXml` wrote. Also unverified by this lane:
non-Linux permission behaviour (the two permission fixtures SKIP loudly on a
privileged or read-observing-impossible runner; this runner is uid 1000 and they
did not skip), and the full test suite, which is parent integration work.