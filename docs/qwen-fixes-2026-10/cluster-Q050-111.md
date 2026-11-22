IMPLEMENTED

# Q050-111 Add per-clip realtime effects

Branch `feat/050-Q050-111-oc`, cut from `050/m0-train-21` (`37eaf0755`). Feature record:
`docs/050-features/Q050-111.md`. Logs: `~/.local/share/zene-bunny-fix/qwen/runs/Q050-111.r1/logs/` (numbered).

## What changed

- `SampleClip` owns an `EffectChain` (`clipEffects()`); `SampleTrack::play` -> `SamplePlayHandle::setClipEffects` ->
  `SamplePlayHandle::play` calls `SampleClip::processClipEffects` on the clip's own buffer before
  `AudioBusHandle` sums the track's clips (src/core/SampleClipEffects.cpp).
- Agent: `resolveControlTarget` accepts `clip-<n>` (audio clips; others are `refused`), so `plugin.load`,
  `plugin.move`, `plugin.bypass`, `plugin.param_set`, `plugin.unload` and `dsp.get_state` work on a clip's chain;
  `chainArgs` lets their undo steps find the chain by the clip's persistent id. No new command ids.
- GUI: the audio clip menu's **Clip effects...** opens `ClipEffectsDialog`; each button and a double-clicked
  parameter row run those same registered commands with the clip as target. FX badge on the clip view.
- Format: additive `<fxchain>` child, written only when a clip has effects, read reset-on-absence.

## Acceptance

All from `build/tests`, `ctest -R '^<name>$' --output-on-failure`.

| Criterion | Test | Exit with change | Exit, product reverted |
| --- | --- | --- | --- |
| A render regression applies an effect to one of two clips on the same track and verifies only that clip's output changes | `ClipEffectsTest` - `anEffectOnOneClipChangesOnlyThatClip` (overlapping sibling; first clip alone 0.25 instead of 0.5, overlap 0.5 instead of 0.75, sibling-alone span byte-identical to the dry render), `aBypassedOrRemovedEffectRendersTheDryClip` | 0 (logs test-8, test-15) | 8 with only the processing hook removed (test-14: "the processed clip alone measured 0.499969, expected 0.25"); 8 with every behaviour hunk removed (test-12) |
| Per-clip effect state survives save/load | `ClipEffectsTest` - `theChainSurvivesSaveAndReopen` (plug-in, order, settings, bypass after a project file save + reopen; the reopened clip renders byte-identical), `aClipWithoutAChainLoadsNone` | 0 (test-15) | 8 (test-12) |
| ... appears in the clip UI | `ClipEffectsDialogTest` - the menu item triggered, Add effect... / Move up / Bypass / Remove clicked, a parameter row double-clicked; list, menu count and FX badge checked; each edit undone by one `control.undo` | 0 (test-16, and test-23 after the rename) | 8 (test-13) |
| ... and can be edited through the agent control surface | `ClipEffectsTest` - `theAgentEditsTheChainOneUndoStepEach` (`plugin.load/move/bypass/unload` with `target: clip-<n>`, read back with `dsp.get_state`, one `control.undo` each; MIDI clip `refused`) | 0 (test-15) | 8 (test-12) |

Revert method: the non-test hunks that carry the behaviour were reversed in the tree (processing call in
`SamplePlayHandle::play`, `setClipEffects` in `SampleTrack::play`, the `clip-` branch of `resolveControlTarget`,
and the save/load/copy calls in `SampleClip.cpp`), rebuilt (exit 0, logs build-18, build-19), run, and restored
from a copy. Reversing *every* non-test hunk (the new API included) makes the tests fail to compile, so the
behaviour-only revert is the meaningful one; both new test executables fail under it.

Neighbours, with the change (exit 0 each): ClipSerialisationTest, ClipFadesRenderTest, ClipEditsMenuTest,
DeviceToolsChainTest, ReversibilityContractTest, ControlCommandsSnapshot, agent_surface.

## Self-check

`submit_check.py` and `feature_check.py` write their logs next to the worktree, which this sandbox cannot do,
so I ran copies in /tmp that differ only in the log directory (written to the run folder instead); the gates
and the baseline cache are the originals'.

- **Completion check** (`submit_check.py` copy, log submit-check-1): RESULT PASS, EXIT=0. Its one WARN ("canary
  figures (README) updated - new command ids: control.redo, control.undo, dsp.get_state, ...") is the regex reading
  existing ids out of my tests' string literals: no command id was added (871 ids / 76 groups before and after, the
  doc-figures gate passes), so README's canary needs no change.
- **Gates** (`feature_check.py` copy, log feature-check-2): PASS on all of fork-sources, all-sources-reproduce,
  no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity (no new item in my files),
  file-length (no new item in my files). EXIT=0.
- **Each new test FAILS with the product change reverted, PASSES restored:** see the table. ClipEffectsTest
  exit 8 (test-12 and test-14) -> 0 (test-15); ClipEffectsDialogTest exit 8 (test-13) -> 0 (test-16, test-23).
- **Every acceptance criterion proven by a test I ran:** see the table; every exit code above is one I saw.
- **One undo step per edit, honest classes:** no new command. A clip target uses the existing rows:
  `plugin.load` snapshot (the recorded step unloads it), `plugin.unload` true_inverse, `plugin.bypass`
  true_inverse, `plugin.param_set` true_inverse, `plugin.move` true_inverse, `dsp.get_state` not_mutating.
  Each edit was taken back by exactly one `control.undo` in both tests (and redo of the load re-adds it).
  ReversibilityContractTest exit 0, so the A16 histogram line did not change.
- **Realtime:** the audio-path code I added is `SampleClip::processClipEffects` and the one call to it in
  `SamplePlayHandle::play`. It reads the chain's switch and list size (both stable for the period, because edits
  take the engine's model-change guard, which excludes render periods), does one atomic exchange/store on a busy
  flag (non-blocking; a loser skips the effects and never waits), builds an `AudioBus` over a one-element stack
  array, and calls `EffectChain::processAudioBuffer`, the same call a track's chain makes every period. Nothing
  allocates, frees, locks, waits, does I/O or emits a signal. `theClipEffectsAllocateNothingPerPeriod` counted
  0 allocations over 32 periods with an amplifier running. `setClipEffects` in `SampleTrack::play` is one
  pointer store. TSan does not run here; the race argument is in the feature record (chain pointer fixed for the
  clip's lifetime; handles removed under the change guard before the clip is destroyed).
- **Old projects load and render as before; new data round-trips:** a clip without effects writes no
  `<fxchain>` (asserted in `aClipWithoutAChainLoadsNone`), so existing files serialise byte for byte as before; a
  missing child loads as no effects and takes no lock. A clip without effects returns from
  `processClipEffects` before touching anything, so its render is unchanged: ClipFadesRenderTest (whose
  untouched-clip hash claim covers this path) and ClipSerialisationTest exit 0. The round trip is
  `theChainSurvivesSaveAndReopen`.
- **Portability:** no Qt-6-only API (QTreeWidget, QInputDialog::getDouble, QFontMetrics::horizontalAdvance are
  all in Qt 5.11+), no `min`/`max`/`near`/`far`/`small`/`interface` identifiers (two `min`/`max` parameters were
  renamed `lowest`/`highest` in the second commit), no std::filesystem, no POSIX-only calls. Not compiled on the
  hosted matrix from here.
- **Snapshot:** regenerated live with `snapshot_live.py` (871 ids / 76 groups; only the `plugin.load`,
  `plugin.unload` and `dsp.get_state` descriptions changed), `doc-figures-gate.py` PASS, ControlCommandsSnapshot 0.
- **Docs:** manual `data/manual/index.html` (Arranging section, "Clip effects..." paragraph, with its limits);
  `docs/050-features/Q050-111.md` (one row per criterion, commands, format, known limits).

## Known limits (also in the record and the manual)

Audio clips only; an effect tail stops when the clip's handle ends; no PDC for clip effects; `clip.consolidate`
renders sources without clip effects and the consolidated clip has none (undo restores the originals); not
tested: automation of a clip effect's parameters.
