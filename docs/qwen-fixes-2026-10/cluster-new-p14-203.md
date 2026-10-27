Verdict: FALSE-POSITIVE

new-p14-203: FALSE-POSITIVE

Evidence: the only division by a base velocity is at [Note.cpp:264](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-203/src/core/Note.cpp:264), inside `Note::releaseVelocityFromMidi()` ([Note.cpp:257](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-203/src/core/Note.cpp:257)). `midiReleaseVelocity()` ([Note.cpp:274](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-203/src/core/Note.cpp:274)) multiplies by the base velocity and divides only by the constant `DefaultVolume`, so it cannot divide by zero. The finding's "~200" is stale; the division is at Note.cpp:264.

The one production caller, [InstrumentTrack.cpp:435](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-203/src/tracks/InstrumentTrack.cpp:435), passes `midiPort()->baseVelocity()`. No caller passes a literal or computed zero (the whole tree was searched: `rg -n "releaseVelocityFromMidi|midiReleaseVelocity|baseVelocity\(\)"`; the only production caller of `releaseVelocityFromMidi` is InstrumentTrack.cpp:435; the test caller at MpeInputPathTest.cpp:311 loops over bases `{ MidiMaxVelocity / 2, DefaultVolume }`, i.e. 63 and 100, never 0 - see tests/src/core/MpeInputPathTest.cpp:300).

The base velocity cannot be 0 on any path that reaches the port's model:
- The model is constructed with range `1 .. MidiMaxVelocity` ([MidiPort.cpp:65](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-203/src/core/midi/MidiPort.cpp:65); `MidiMaxVelocity` is 127, [Midi.h:130](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-203/include/Midi.h:130)).
- The only setter is `mapPropertyFromModel`, which calls `m_baseVelocityModel.setValue()` ([MidiPort.h:68](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-203/include/MidiPort.h:68), [AutomatableModel.h:62-73](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-203/include/AutomatableModel.h:62)).
- Every write to the model value goes through `fittedValue()`, which clamps to `[m_minValue, m_maxValue]` ([AutomatableModel.cpp:604-616](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-203/src/core/AutomatableModel.cpp:604)): `setValue` → `setValueInternal` ([AutomatableModel.cpp:462-500](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-203/src/core/AutomatableModel.cpp:462)), project load via `loadSettings` → `setValue` ([AutomatableModel.cpp:328-347](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-203/src/core/AutomatableModel.cpp:328)), and automation/controller reads ([AutomatableModel.h:228](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-203/include/AutomatableModel.h:228)).
- The model's range is never changed after construction: `m_baseVelocityModel` is referenced only in MidiPort.cpp (constructor 65, load 282, the legacy-project reset 319, save 228), MidiPort.h (68, 195) and InstrumentMidiIOView.cpp:179 (a spin box binding, which writes through `setValue`).

So a base velocity of 0 (or below 1) cannot reach `releaseVelocityFromMidi()` from the DAW. The "when" in the finding — "called with a zero base velocity" — has no path in this tree, and the finding's confidence was already low.

The sibling `MidiEvent::volume(int)` ([MidiEvent.h:145](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-203/include/MidiEvent.h:145)) divides by its base velocity too; its callers ([InstrumentTrack.cpp:532](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-203/src/tracks/InstrumentTrack.cpp:532), [InstrumentTrack.cpp:579](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-203/src/tracks/InstrumentTrack.cpp:579)) pass the same clamped value, so it has the same unreachable precondition.

No product code is changed. A zero-guard in `releaseVelocityFromMidi()` would be a defensive change to a public static whose only input is already clamped; I did not add it because the procedure makes a not-real verdict a claim-only closure. If the owner wants the static to be self-defending, that is a separate hardening item and needs an explicit request.

| Evidence | Command | Result |
| --- | --- | --- |
| Every division by a base velocity | `rg -n "[Bb]aseVelocity\s*\)\|/\s*\(?\s*2\s*\*\s*midiBaseVelocity\|/\s*midiBaseVelocity" src include > logs/10-divide-by-base.log 2>&1; echo EXIT=$?` | EXIT=0; the two divisions are Note.cpp:264 and MidiEvent.h:145 (in `logs/10-divide-by-base.log`) |
| Callers of the conversion (all tree) | `rg -n "releaseVelocityFromMidi\|midiReleaseVelocity\|baseVelocity\(\)" --glob '!build/**' --glob '!docs/**' --glob '!**/upstream-modifications.txt' . > logs/11-callers.log 2>&1; echo EXIT=$?` | EXIT=0; InstrumentTrack.cpp:435 is the only production caller of releaseVelocityFromMidi (`logs/11-callers.log`, 51 lines) |
| Model floor and clamp | read MidiPort.cpp:65, AutomatableModel.cpp:604-616 | min 1, max 127; fittedValue clamps every write |
| Gate: fork sources | `bash tests/fork-sources-gate.sh > logs/02-gate-fork-sources.log 2>&1` | EXIT=0 |
| Gate: all-sources reproduce | `bash tests/all-sources-reproduce.sh > logs/03-gate-all-sources-reproduce.log 2>&1` | EXIT=0 |
| Gate: no upstream regression | `bash tests/no-upstream-regression-gate.sh > logs/04-gate-no-upstream-regression.log 2>&1` | EXIT=0 |
| Gate: check namespace | `tests/scripted/check-namespace > logs/05-gate-check-namespace.log 2>&1` | EXIT=0 |
| Gate: no tautology | `bash tests/no-tautology-gate.sh > logs/06-gate-no-tautology.log 2>&1` | EXIT=0 |
| Gate: doc figures | `python3 tests/doc-figures-gate.py > logs/07-gate-doc-figures.log 2>&1` | EXIT=0 |
| Gate: complexity (ratchet) | `bash tests/complexity-gate.sh --check > logs/08-gate-complexity.log 2>&1` | EXIT=1; the 7 listed items are in files this branch did not change (diff against base is empty), so feature_check's rule passes them |
| Gate: file length (ratchet) | `bash tests/file-length-gate.sh --check > logs/09-gate-file-length.log 2>&1` | EXIT=1; the 22 listed items are in files this branch did not change |
| Full feature_check.py | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/001-feature-check.log 2>&1` | EXIT=1 with a PermissionError: it writes its logs to `<worktree>/../<name>-feature-check`, which is the read-only programs directory. The six simple gates and two ratchets were run directly above instead, with the exit codes shown. |

No regression test was added: a test for a not-real finding would only pin a behaviour that is already correct. RED/GREEN (procedure steps 3-4b) do not apply: there is no product change to reverse.

Files committed for this verdict: `docs/qwen-fixes-2026-10/cluster-new-p14-203.md`. No product or test file changed.

## Self-check

- Regression fails with the change reverted and passes with it: not applicable, no product change; no regression test added (a not-real verdict).
- Deterministic test: not applicable.
- Fix at the cause, callers and siblings: the callers and the sibling `MidiEvent::volume` were checked above and all receive the clamped base velocity.
- No collateral damage: no code changed, so old projects and saved data are unaffected.
- Realtime/undo/commands: no code changed; no new command.
- Registration and gates: the six simple gates and two ratchets were run directly (EXIT codes above). The ratchet failures are on items in files this branch does not change.
