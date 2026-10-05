# Presets batch-02 — Space Bunny fixer lane `presets`

Branch `bugs/luna-presets-2026-10-05`. All five findings were checked against the
current tree at `ec7541125`. Nothing in this batch needed a product change:
three findings were already repaired in the branch's WIP commit `9c7a95196`, and
two are false positives that rest on a wrong reading of the SPEC A16 transaction
payload.

### Cluster 5656 — Overwriting an unreadable preset loses its previous contents on undo

Verdict: ALREADY-FIXED

Evidence: `src/core/ControlCommandsExportPresets.cpp:146` now reads
`if (*existed && !controlReadFileBytes(path, &previous, error)) { return false; }`
inside `writePresetDocument`, so an unreadable existing preset aborts the add
before the write at `:155` and before `recordPresetWrite` at `:156` — no
document is written and no undo step is recorded. `git show 9c7a95196 --
src/core/ControlCommandsExportPresets.cpp` shows this exact line replacing the
old unchecked `if (*existed) { controlReadFileBytes(path, &previous, error); }`.

Commit: 9c7a95196 (not mine)

Test: none added — the defect is absent in the current tree

Red/green: none (no change made)

### Cluster 5657 — Removing a corrupt preset deletes it before returning an error

Verdict: ALREADY-FIXED

Evidence: in `registerExportPresetRemove` the bytes are read at
`src/core/ControlCommandsExportPresets.cpp:443` and parsed at `:445`, both before
`QFile::remove` at `:450`; a malformed preset returns the typed parse error with
the file still on disk. Commit `9c7a95196` moved the
`controlExportPresetFromBytes` call from after the unlink to before it.

Commit: 9c7a95196 (not mine)

Test: none added — the defect is absent in the current tree

Red/green: none (no change made)

### Cluster 1072 — The overwrite transaction inverse deletes the replaced preset

Verdict: FALSE-POSITIVE

Evidence: the `__transaction` payload built at
`src/core/ControlCommandsExportPresets.cpp:306-312` is descriptive metadata, not
a dispatchable inverse: `transactionPayload`
(`src/core/ControlEditSupport.cpp:481-493`) never sets `inverse.applies`, and
`undoByCommand` (`src/core/ControlCommandsControl.cpp:334-335`) only dispatches
when `applies == "command"`, falling through to the journal/action path
otherwise. So `export.preset_remove` in that payload is never invoked by
`control.undo`; the real inverse is the recorded action step from
`recordPresetWrite` (`:112-125`), which writes the captured `previous` bytes back
when `replaced` is true and only removes the file otherwise — exactly the
behaviour the finding asks for. The mechanism string at `:310-312` says so.

Commit: none

Test: none

Red/green: none (no change made)

### Cluster 1993 — The preset-apply transaction inverse can fail after its prior preset is removed

Verdict: FALSE-POSITIVE

Evidence: same mechanism as cluster 1072. The payload at
`src/core/ControlCommandsExportPresets.cpp:386-396` carries no
`inverse.applies: "command"`, so `undoByCommand` never dispatches
`export.preset_apply` with the prior name and can never return `NotFound`. The
actual inverse is the action step registered at `:371-379`, which captures the
previous settings **by value** (`previousSettings`) and restores them with
`ControlExportPresetSettings::setActive` without a store lookup — a preset removed
from the store between the apply and the undo still comes back.

Commit: none

Test: none

Red/green: none (no change made)

### Cluster 5658 — Failed preset removal still adds an undo step

Verdict: ALREADY-FIXED

Evidence: `addUndoStep` is now called at
`src/core/ControlCommandsExportPresets.cpp:458`, after the `QFile::remove` check
at `:450-454` returns its typed `Refused` on failure; the comment at `:456-457`
states the invariant. `git show 9c7a95196` shows the step being moved from before
the unlink to after it.

Commit: 9c7a95196 (not mine)

Test: none added — the defect is absent in the current tree

Red/green: none (no change made)

Leftovers: none — no code changed in this batch, so the complexity and
file-length gates and a `zene` rebuild were not required before this report
commit. The two false positives both rest on reading a `transactionPayload`
`inverse.op` as a dispatched command; a reviewer who wants machine evidence
rather than the `undoByCommand` argument could add a
`ControlExportPreset` test asserting `control.undo` after an overwriting
`export.preset_add` restores the replaced revision, but that would be new
coverage rather than a regression for a defect.