# BUG-CTL-1 — the undo of `plugin.unload` reported success over a default device

Lane `bugs/hunt-2026-09-24` (worktree `zene-bugs`), 2026-09-24. Raw logs are not landed
as `.log` (Gate 11 refuses them anywhere in the tree, and this run's transcripts exceed
its 1 MiB cap); the commands and their unpiped exit codes are recorded here instead,
the shape `docs/706-logs/` and the sibling `docs/bug-fix-2026-09-24-logs/EVIDENCE.md`
already use.

## The defect

`control.undo` of `plugin.unload` answered `undone: true` while the device it had
re-created sat at its DEFAULTS. `recreateEffectFromState()`
(src/core/ControlStructuralSupport.cpp) discarded the result of
`controlRestoreEffectState()` (src/core/ControlDeviceSupport.cpp), which returns a typed
`Refused`/`InvalidArgs` when the device will not accept the document. An inverse that
silently half-works is the SPEC A16 contract violation: the registry still claims
`plugin.unload` is reversible.

The obstacle a previous session traced correctly: `ProjectJournal`'s structural
callbacks are `void` and `undo()` returns `void`, so a failure inside the undo callback
has no path back to `control.undo`'s response.

## The fix — shape (a), a side channel

* `recreateEffectFromState()` records the failure of every one of its failure exits in a
  file-local `std::optional<ControlResult>`, and on a REFUSED restore removes the
  freshly created default-parameters device again (`EffectChain::removeEffect` +
  `deleteLater` — the same lifetime as the removal path beside it), so no half-restored
  instance is left for a later save to persist.
* `takeStructuralRestoreFailure()` / `clearStructuralRestoreFailure()`
  (include/ControlStructuralSupport.h) expose the record. The read CONSUMES it, so one
  failure is reported once and cannot leak into a later undo.
* `undoThroughJournal()` (src/core/ControlCommandsControl.cpp) clears the record before
  it unwinds a step and, when it is set afterwards, answers the typed failure instead of
  success — naming the command and the `before.state_xml` fallback.

Shape (b) — an add-only checked callback in `ProjectJournal` — was rejected as the
larger change: it touches the checkpoint copy/merge/redo machinery, the blast radius the
previous session correctly refused to enter.

## Proof

`tests/src/core/ReversibilityUndoTest.cpp::unloadUndoRefusesWhenTheDeviceCannotBeRestored`
(the SPEC A16 undo-acceptance file). Run from `build/tests` with
`QT_QPA_PLATFORM=offscreen CTEST_JOBS=1`:

| tree | command | EXIT |
| --- | --- | --- |
| fix in place | `ctest -R '^ReversibilityUndoTest$'` | 0 |
| the three production hunks reverted (`git checkout --` the two `.cpp` and the header) | same | 8 |
| restored (`git apply`) | same | 0 |

Reverted, the new case fails with exactly the defect:

```
FAIL!  : ReversibilityUndoTest::unloadUndoRefusesWhenTheDeviceCannotBeRestored()
'!undo.ok' returned FALSE. (control.undo reported SUCCESS although the device was not
restored (BUG-CTL-1): the recreated instance sat at its defaults)
```

The case asserts both directions: the happy path (a document the device accepts →
`control.undo` reports success and the device is back), and the refused document →
a typed failure, no device left in the chain, a redo that resurrects nothing, a second
undo that refuses again, and the instance still serving.

## Battery and gates (same tree)

From `build/tests`, `CTEST_JOBS=1`: **17/17 passed, EXIT=0** —
ControlAutomationScriptTest, ControlAutomationModesTest, ControlVerbInverseTest,
ReversibilityContractTest, ReversibilityUndoTest, UndoBoundsTest,
ControlFreezeCommandsTranscript, ControlTrackFolderTranscript, ControlRoutingCommands,
ControlFeedbackCommands, ControlPortsCommands, ControlMeterCommands,
ControlNegativeControl, ControlCommandsSnapshot, ControlMcpGroupCoverage,
ControlSurfaceHardening and `ControlUndoStructuralTranscript` (the existing
`plugin.unload` → undo → device-and-settings happy-path transcript).

`bash tests/file-length-gate.sh` 0 · `bash tests/complexity-gate.sh` 0 ·
`bash tests/fork-sources-gate.sh` 0 · `bash tests/no-upstream-regression-gate.sh` 0 ·
`bash tests/duplication-gate.sh` 0.

## Observed, not fixed here

A failed recreate's own chain writes (`EffectChain::appendEffect`/`removeEffect` set the
chain's `enabled` model, and `AutomatableModel::setValue` journals a checkpoint per
write) leave chain-flag steps on the undo stack, so the NEXT undo unwinds one of those
rather than the unload step. The happy path has the same pre-existing behaviour
(undoing a device removal also leaves the chain's flag step), which is why no depth
delta is asserted anywhere in the new case.
