# Cluster new-p9-29-port — FIXED

## Verdict

**FIXED** (port of `94fef0f80` "Fix redundant clip record undo step (cluster new-p9-29)" onto
`050/m0-train-5`). The bug was still present on this line; the fix was re-applied by hand
against the current code (no merge, no cherry-pick, no rebase).

The command has MOVED since the original branch: on the newest line the `clip.set_record`
handler lives in its own translation unit, `src/core/ControlCommandsClipRecord.cpp`, not in
`src/core/ControlCommandsClipEdits.cpp`. The change was written there.

## The bug, in the current tree

`src/core/ControlCommandsClipRecord.cpp:73-97` (before the fix): the handler read the clip's
current flag into `before`, read the requested one into `wanted`, and then ALWAYS pushed an
undo action:

```cpp
const bool before = clip->isRecord();
const bool wanted = args.value(QStringLiteral("record")).toBool();
...
addUndoStep([apply, before] { apply(before); }, [apply, wanted] { apply(wanted); });
clip->setRecord(wanted);
```

So `clip.set_record` with `record` equal to the flag the clip already had recorded an undo
step that changes nothing. The next `control.undo` consumed that step and left the clip
alone, so the real edit before it stayed.

## The change

`src/core/ControlCommandsClipRecord.cpp:76-89` — the handler returns the unchanged state
BEFORE it records anything, so a request for the flag the clip already has leaves neither a
journal step nor a transaction record:

```cpp
if (wanted == before)
{
    return ControlResult::success(QJsonObject{
        {QStringLiteral("clip"), id},
        {QStringLiteral("record"), before},
        {QStringLiteral("record_before"), before},
        {QStringLiteral("song_recording"), Engine::getSong()->isRecording()}});
}
```

The reply carries the same four keys the change path answers with (the declared
`cmd.resultSchema` at `src/core/ControlCommandsClipRecord.cpp:55-60`), so a client cannot
tell the two paths apart by the shape of the answer. The no-op path deliberately carries NO
`__transaction` key: `ControlRegistry::changedNothing`
(`src/core/ControlRegistry.cpp:298-303`) then treats the call as "changed nothing", so the
registry records no transaction for it and the previous real edit stays the next
`control.undo` target. This is the house idiom for a no-op mutating command (see
`src/core/ControlCommandsAutomationTools.cpp:109`, "nothing changed: no undo step", and
`src/core/ControlCommandsFeedback.cpp:349`, "A no-op (already off) records no undo step").

A real change is untouched: it still records exactly one action step whose inverse sets the
flag back (`src/core/ControlCommandsClipRecord.cpp:96`).

## The regression test

`tests/control-audio-editing.py:37-67` — `check_redundant_record_arm_does_not_consume_undo`
arms a sample clip, repeats the SAME arm, undoes once, and reads the flag back:

* `armed` must report `record_before: false` → `record: true` (the first arm changed the clip);
* `repeated` must report `record_before: true` → `record: true` (the repeat reports the state);
* after ONE `control.undo`, the next read must show `record_before: false` — the undo reached
  the FIRST (real) arm instead of being eaten by the repeat.

### Why the ported test differs from the one in `94fef0f80`

The original test set `control.set_undo_coalescing {window_ms: 0}` and never put it back,
and it left the clip ARMED. On this newest line that breaks the LATER `clip.split_at` check
in the same file, which is what stopped the original commit from merging. The cause is NOT
the coalescing window (see the proof below): it is the leftover ARMED clip.

* An armed clip does not journal its own state — `SampleClip::armRecordTake` turns the
  clip's journalling off while it records (`src/core/SampleClip.cpp:229`:
  `setJournalling(false)`), and it comes back only when the clip is disarmed
  (`src/core/SampleClip.cpp:215`).
* A non-journalling object saves as an EMPTY element
  (`src/core/JournallingObject.cpp:88-97`), so the track checkpoint the next
  `range.fade` takes no longer contains the armed clip, and the undo of that step rebuilds
  the track WITHOUT it. The first clip then vanished and the later split check failed.

Evidence that the coalescing line is innocent: with the `control.set_undo_coalescing` line
REMOVED from the test and everything else unchanged, the `clip.split_at` check still failed
the same way (log `test-3.log`, EXIT=8). `clip.set_record` is not a coalescing command
either - the coalescing rule's command list is reported by the engine itself as
`arrangement.nudge, chord.set, clip.move, clip.resize, mixer.set_pan, mixer.set_volume,
plugin.param_set, rack.macro_set, rack.macro_set_pair` (log `test-2.log`), and
`clip.set_record` is not in it.

The ported test therefore puts the session back the way it found it: the coalescing window is
read before the check and restored after it, and the clip is disarmed again at the end (which
also re-enables the clip's journalling). With that, the whole file passes and the later
`clip.split_at` one-undo check passes again.

## Evidence

| Check | Exact command | Exit | Result |
| --- | --- | ---: | --- |
| Baseline (no test change, no fix) | `cd build/tests && ctest -R ControlAudioEditing --output-on-failure` | 0 | Passed (1/1). |
| RED — the ported test on the UNFIXED handler | `cd build/tests && ctest -R ControlAudioEditing --output-on-failure` (log `test-4.log`) | 8 | "one undo reaches the first real arm after the repeated request" FAILED: `state_after_undo={'record': True, 'record_before': True}` — the repeat ate the undo. All the other checks in the file passed. |
| GREEN — with the fix | `cd build/tests && ctest -R ControlAudioEditing --output-on-failure` (log `test-5.log`) | 0 | Passed (1/1). |
| Reverse-patch proof, fix removed | `git apply -R logs/fix.patch && cmake --build build --target zene && cd build/tests && ctest -R ControlAudioEditing --output-on-failure` (logs `build-8.log`, `test-12.log`) | 8 | Same check FAILED again. |
| Reverse-patch proof, fix restored | `git apply logs/fix.patch && cmake --build build --target zene && cd build/tests && ctest -R ControlAudioEditing --output-on-failure` (logs `build-9.log`, `test-13.log`) | 0 | Passed (1/1). |
| Affected family | `cd build/tests && ctest -R "^(ControlAudioEditing|ControlCommandsSnapshot|ControlResultSchemaTest|ControlReversibilityTranscript|ControlVerbInverseTest|ControlEditCommandsTest|ControlRegistryTest|ControlRegistryNestingTest|ControlRecordingRecovery|ControlRecordInputs|ControlPunchTranscript|ClipEditsMenuTest|ClipEditsTest|ControlSurfaceReferenceTest)$" --output-on-failure` (log `test-9.log`) | 8 | 13 of 14 ran and passed; `ControlReversibilityTranscript` failed - see below. 1 test in the `ControlAudioEditing` family itself, zero tests would be an error. |
| Pre-existing failure, proven | the same `ControlReversibilityTranscript` run with the product patch REVERTED (log `test-10.log`) | 8 | Fails identically without my change: `the LADSPA host plugin 'ladspaeffect' is not in this build`. Environmental, not mine. |
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (log `gate-2.log`) | 0 | RESULT: PASS (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length). |

All `cmake --build build --target zene` runs exited 0 (logs `build-1.log` .. `build-9.log`).
The reverse-patch sequence was run twice and both `git apply` invocations exited 0
(`APPLY_REVERSE_EXIT=0`, `APPLY_EXIT=0`).

### The exact commands, as run

```sh
cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-29-port
LOGS=/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-29-port.r1/logs

cmake --build build --target zene > $LOGS/build-1.log 2>&1; echo EXIT=$?          # 0
cd build/tests && ctest -R ControlAudioEditing --output-on-failure > $LOGS/test-1.log 2>&1; echo EXIT=$?   # 0 (baseline)
cd .. && cd ..   # back to the worktree

# RED: the ported test, the UNFIXED handler
cd build/tests && ctest -R ControlAudioEditing --output-on-failure > $LOGS/test-4.log 2>&1; echo EXIT=$?   # 8

# GREEN: the fix applied
cmake --build build --target zene > $LOGS/build-2.log 2>&1; echo EXIT=$?          # 0
cd build/tests && ctest -R ControlAudioEditing --output-on-failure > $LOGS/test-5.log 2>&1; echo EXIT=$?   # 0

# reverse-patch proof
git diff -- src/core/ControlCommandsClipRecord.cpp > $LOGS/fix.patch && git apply -R $LOGS/fix.patch   # 0
cmake --build build --target zene > $LOGS/build-8.log 2>&1; echo EXIT=$?          # 0
cd build/tests && ctest -R ControlAudioEditing --output-on-failure > $LOGS/test-12.log 2>&1; echo EXIT=$?  # 8
git apply $LOGS/fix.patch                                                          # 0
cmake --build build --target zene > $LOGS/build-9.log 2>&1; echo EXIT=$?          # 0
cd build/tests && ctest -R ControlAudioEditing --output-on-failure > $LOGS/test-13.log 2>&1; echo EXIT=$?  # 0

# the wider control family
cmake --build build --target ControlResultSchemaTest ControlVerbInverseTest ControlRegistryTest \
    ControlRegistryNestingTest ControlEditCommandsTest ClipEditsTest ClipEditsMenuTest \
    ControlSurfaceReferenceTest > $LOGS/build-5.log 2>&1; echo EXIT=$?           # 0
cd build/tests && ctest -R "^(ControlAudioEditing|ControlCommandsSnapshot|ControlResultSchemaTest|ControlReversibilityTranscript|ControlVerbInverseTest|ControlEditCommandsTest|ControlRegistryTest|ControlRegistryNestingTest|ControlRecordingRecovery|ControlRecordInputs|ControlPunchTranscript|ClipEditsMenuTest|ClipEditsTest|ControlSurfaceReferenceTest)$" --output-on-failure > $LOGS/test-9.log 2>&1; echo EXIT=$?   # 8 (one pre-existing failure)

# gates
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py \
    /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-29-port \
    > $LOGS/gate-2.log 2>&1; echo EXIT=$?                                         # 0
```

## Self-check

* **The regression test FAILS with the product change reverted and PASSES with it.** RED:
  `ctest -R ControlAudioEditing --output-on-failure` → EXIT=8, the check "one undo reaches
  the first real arm after the repeated request" FAILED with
  `state_after_undo={'record': True, 'record_before': True}` (log `test-12.log`, and the
  earlier identical RED in `test-4.log`). GREEN: the same command → EXIT=0, "1/1 Test #520:
  ControlAudioEditing ... Passed" (log `test-13.log`). Reverse-patch proof run in both
  directions with `git apply -R` / `git apply` on `logs/fix.patch` (both EXIT=0).
* **The test is deterministic.** No wall-clock timing, no sleeps, no races. The seam is the
  undo coalescing window, which the test READS from the engine
  (`control.undo_depth` → `coalescing.window_ms`) and PINNS to 0 for the duration of the
  check, so "one call, one step" cannot depend on how fast the calls arrive. The window is
  restored afterwards. Nothing else in the test reads a clock, and the assertions are on
  state read back over the socket.
* **The fix is at the cause and complete.** The cause is the unconditional
  `addUndoStep(...)` in the `clip.set_record` handler
  (`src/core/ControlCommandsClipRecord.cpp:96` before the change) - the fix guards the
  handler itself, so every caller of the command is covered. Callers and siblings checked:
  the GUI action `src/gui/ClipEditsMenu.cpp:105-118` (a CHECKABLE `QAction`, so a click
  always sends the opposite of the current value and still takes the change path - its
  behaviour is unchanged); the other writer of the flag,
  `SampleClip::acceptRecordedTake` → `setRecord(false)` (`src/core/SampleClip.cpp:308`,
  not journalled by design, untouched); the flag's model binding
  `mapPropertyFromModel(bool,isRecord,setRecord,m_recordModel)`
  (`include/SampleClip.h:62`, untouched - the no-op path never reaches it); the
  `clip.set_record` row of the contract table
  (`src/core/ControlReversibilityTableLive.cpp:176-180`, untouched - the class stays
  true_inverse, and the no-op path records no step at all, which the registry's own
  `changedNothing` rule already expects); the sibling no-op mutating commands
  (`feedback.enable` / `feedback.disable`, `arrangement.*`) which already follow the same
  "a no-op records no undo step" idiom, so the clip command now matches them. No new
  control command was added (`cmd.id`, the schema and the description are unchanged, so the
  `ControlCommandsSnapshot` canary does not move - and that test passes, log `test-9.log`).
* **No collateral damage.** The record flag is NOT serialized
  (`src/core/ControlReversibilityTableLive.cpp:177-178`: "the record flag is not in the
  clip's serialized state"), so no saved project or old file changes meaning; the change
  touches only the in-memory flag and the journal. No limit, threshold or check was added,
  so no other caller can be hit by it. A real arm/disarm still records exactly one undo step
  with the same inverse as before, and the reply of the change path is byte-for-byte the
  same as before. The wider control family (14 tests, log `test-9.log`) passes except
  `ControlReversibilityTranscript`, which fails identically WITHOUT the change (log
  `test-10.log`: the LADSPA host plugin `ladspaeffect` is not in this build) and is
  therefore pre-existing and environmental. `ControlMcpGroupCoverage` also fails in this
  environment (the MCP bridge's Python `Server.__init__()` does not accept `on_list_tools` in
  the installed MCP SDK) - it fails before the bridge even talks to `zene`, so it cannot be
  about this change.
* **Not ALREADY-FIXED, not a false positive.** The bug was present on this line: the
  handler at `src/core/ControlCommandsClipRecord.cpp:73-97` recorded the undo step
  unconditionally, and the RED run above shows the undo being eaten by the repeat
  (`record_before: True` after the undo). The verdict is FIXED.

## Files committed

- `src/core/ControlCommandsClipRecord.cpp` — the fix (the handler returns the unchanged
  state instead of recording an undo step).
- `tests/control-audio-editing.py` — the regression check
  `check_redundant_record_arm_does_not_consume_undo` and the file's docstring.
- `docs/qwen-fixes-2026-10/cluster-new-p9-29-port.md` — this report.

No new file was created, so no entry in `tests/fork-sources.txt` was needed:
`src/core/ControlCommandsClipRecord.cpp` is already registered there (line 1337) and
`tests/control-audio-editing.py` is an inherited file of this fork, already covered by the
`fork-sources` gate, which passes (log `gate-2.log`).
