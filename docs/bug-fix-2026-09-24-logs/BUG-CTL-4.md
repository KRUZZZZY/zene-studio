# BUG-CTL-4 — `control.undo` claimed a single-command inversion while reverting a coalesced span

Lane `bugs/hunt-2026-09-24` (worktree `zene-bugs`), 2026-09-24. Raw run logs are not landed as `.log`
(Gate 11 refuses them anywhere in the tree); the commands, their unpiped exit codes and the exact
printed lines are recorded here, the shape `docs/bug-fix-2026-09-24-logs/BUG-CTL-1.md` already uses.

## The defect (a QA sweep's DEFECT-D4a, mechanism established at the wire)

A sweep of undo windows against the control socket (seed 5201, `/tmp/zene-cert-w1r-flow/`) found one
window out of seven where `control.undo` over-reverted:

```
@59  plugin.param_set {name: 'Wet/Dry mix', value: 0.5}      -> value -0.34 -> 0.5
@64  dsp.get_state                                            -> 0.5
@66  plugin.param_set {name: 'Wet/Dry mix', value: -0.16}
@67  control.undo   -> {"undone": true, "undone_command": "plugin.param_set"}
@72  dsp.get_state  -> -0.34      <-- the value from BEFORE @59: TWO writes reverted
```

The neighbouring window in the same run is correct — `param_set 0.6` → `automation.add_point` →
`param_set -0.16` → `control.undo` → `0.6`, a single inversion — and the difference is the only thing
that matters here: in the failing window the calls in between (`dsp.get_state`, a read) do **not**
produce a journal step.

## Root cause: the coalescing rule is working; the REPLY was silent

Not a lost checkpoint, not dedup, not a stale step. The two writes were merged into ONE undo step by
the **declared** coalescing rule:

* `plugin.param_set` is declared coalescing with target `target,plugin,name,index`
  (`src/core/ControlReversibilityTableLive.cpp`: `RC("plugin.param_set", RC::TrueInverse, true, …)`),
  and the window is a declared constant — `control::UndoCoalesceWindowMs = 400`
  (`include/ControlReversibility.h`), settable through `control.set_undo_coalescing`.
* `ControlUndoCoalescing.cpp` (`coalesceOrOpenStep`) deliberately leaves the run ALONE when a call
  produced no step: a client that reads state between two moves of the same thing is still making one
  gesture. An intervening command that DOES push a step ends the run (`stepOpened("")`), which is
  exactly why the `automation.add_point` window inverts one command.
* The merge keeps the EARLIEST capture (`ProjectJournal::coalesceTopStepIntoPrevious()`), so the
  surviving step's `before` is the pre-gesture state — one undo returns the parameter to `-0.34`. The
  record side counts it: `Transaction::commands` (`include/ControlRegistry.h`).

So the *state* follows the documented rule. What did not was the reply: `undoThroughJournal()`
(`src/core/ControlCommandsControl.cpp`) answered `undone_command` and nothing else, so a step covering
TWO commands was indistinguishable on the wire from a single-command inversion. That is the SPEC A16
honesty failure — the same "never pretend" rule §6/§10 of `docs/A16-REVERSIBILITY.md` already hold the
eviction case to.

## The semantics the product promises: (b) deliberate coalescing, made honest

Decided from the tests and the spec, not from taste:

* `UndoBoundsTest::aTwoHundredCallDragIsOneUndoStep` asserts a 200-call `clip.move` run is ONE step
  with the record's `commands == 200`, and that ONE `control.undo` reverts the whole drag.
  `coalescingOffReproducesTheOldBehaviour` asserts window 0 restores per-call granularity. (a) —
  "every command gets its own checkpoint" — would contradict both tests.
* `tests/control-socket-integration.py:783-793` already states the rule and works around it:
  *"The undo below asserts PER-CALL granularity: one param_set, one step. Under the control surface's
  coalescing rule (docs/UNDO-BOUNDS.md) two consecutive param_set calls on the SAME parameter inside
  the window are ONE gesture … Window 0 is exactly 'one step per call'."*
* SPEC §7 A16: *"Every command is reversible. … The registry records a transaction per mutating
  command — before-state plus the inverse operation — so `control.undo`/`control.redo` reverse it"* —
  reconciled as: "reverse it" is per undo STEP under the declared window, per COMMAND at window 0 or
  when any other step-producing command intervenes, and the reply now says which of the two it was.

## The fix

* `src/core/ControlCommandsControl.cpp` — `undoThroughJournal()` takes `commands` and inserts
  `undone_commands` beside `undone_command`; `undoLastCommand()` passes `record.commands`; the
  command-inverse branch reports `record.commands` too; `control.undo`'s `resultSchema` declares the
  integer field and its description states the granularity rule and the window-0 escape hatch.
* `docs/UNDO-BOUNDS.md` — Decision 2 gains "the reply says which of the two it was"; honest limit 5
  gains the field.
* `docs/A16-REVERSIBILITY.md` — §4.3 (the reconciliation with the A16 row) and §10 (three behaviours,
  not two).

## Proof — red/green, both windows in ONE flow

`tests/control-reversibility-transcript.py` section D (the registered A16 socket transcript,
ctest `ControlReversibilityTranscript`), run from the worktree root with
`QT_QPA_PLATFORM=offscreen python3 tests/control-reversibility-transcript.py <build>/zene`:

| tree | observed | EXIT |
| --- | --- | --- |
| production hunks absent (`ControlCommandsControl.cpp` reverted, binary rebuilt) | `FAIL: A16 reversibility transcript` — *the ONE step control.undo unwound covered TWO commands and the reply did not say so (BUG-CTL-4): it answered undone_commands=None* and *an intervening journaled step did not leave each write its own undo step (BUG-CTL-4): control.undo reported undone_commands=None* | 1 |
| fix in place (binary rebuilt) | `PASS` — coalesced leg prints `undone_command=plugin.param_set undone_commands=2 \| -0.6 -> -0.1 -> undo -> 1 (pre-gesture 1)`; intervening leg prints `undone_commands=1 \| -0.6 -> 0.5 -> undo -> -0.6` | 0 |

Both windows are asserted in the same section: the merged span must report `2` and restore the
pre-gesture value, and the intervening-journaled-step window must report `1` and restore the FIRST
write's value (not the pre-first one). Only the two BUG-CTL-4 assertions failed before the fix; the
sections A/B/C assertions and the two "the state follows the declared rule" assertions already held.
Both exits were measured unpiped (`cmd > log 2>&1; echo EXIT=$?`).

The first shape of section D tripped the complexity ratchet — `REGRESSION: new function over target:
the_coalescing_rule_reports_what_it_covered@204-331@tests/control-reversibility-transcript.py
(CCN 11)`, `FAIL: complexity ratchet regressed` — so the section is split into the fixture probe and
one helper per window (`coalescing_fixture`, `check_coalesced_span`, `check_intervening_step`), the
shape `control-undo-structural.py` uses for the same reason. The gate is green with the split.

## Observed in this run, NOT fixed here

**`plugin.param_get` reports a range the setter refuses, for the effect's own enable flag.** Probing
index 0 of the loaded Amplifier reported `'Effect enabled': min=-1 max=1`, and `plugin.param_set`
with a value from that range was refused:
`value -0.6 is outside the range 0..1 of parameter 'Effect enabled'` (reply id 510). The transcript's
probe now picks a BIPOLAR numeric parameter instead, so section D is not blocked by it; the mismatch
between `plugin.param_get`'s reported range and the model range it advertises is a separate defect
(a `boolean`-typed model whose reported min/max are the schema's, not the model's) that this lane did
not take on.

**The secondary lead does not reproduce.** The sweep's `plugin.unload` → `control.undo` semantic check
on the stored run reports `same: False, semantic_same: True`
(`/tmp/zene-cert-w1r-flow/seed-5201/steps.json`, check `A16-plugin-unload-undo`): the only difference
between the pre-unload and post-undo `dsp.get_state` is the instance id (`fx-11` → `fx-36`), which the
checker's `semantic_plugin_state()` strips by design; the device class, index and every parameter value
match. Seed 5206's directory does not exist in the evidence tree (`/tmp/zene-cert-w1r-flow/` holds only
`seed-5201/`), and the `HARNESS-ERROR: plugin not restored after unload undo` in `pilot3.log` is an
earlier pilot's verdict, not the stored run's. The registered happy-path transcript
(`ControlUndoStructuralTranscript`) covers the same leg and stays green in the battery below.
