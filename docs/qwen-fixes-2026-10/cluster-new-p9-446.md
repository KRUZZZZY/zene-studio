FALSE-POSITIVE

# Cluster new-p9-446 — "A zero quantization value crashes step recording"

Finding: `src/core/StepRecorder.cpp:59` (historical), `start()` divides by `m_pianoRoll.quantization()` without a zero check. The current division is at `src/core/StepRecorder.cpp:69-71`:

```cpp
const int q = m_pianoRoll.quantization();          // :69
const int curPosTicks = currentPosition.getTicks();
const int QuantizedPosTicks = (curPosTicks / q) * q; // :71
```

The division is unguarded, but the divisor cannot be 0 for any value the piano roll's models can hold. The claimed trigger ("the no-quantization setting returns 0") does not happen.

## Evidence

1. `quantization()` returns one of three expressions, and none is 0 (`src/gui/editors/PianoRoll.cpp:5118-5132`):
   - quantize model value `0` ("Note lock") and note-length model value `> 0`: returns `newNoteLen()`. That is `DefaultTicksPerBar / N` for the item text `1/N` (`PianoRoll.cpp:5199-5207`).
   - quantize model value `0` and note-length value `0` ("Last note"): returns `DefaultTicksPerBar / 16` = `192 / 16` = 12 (`PianoRoll.cpp:5120-5127`).
   - quantize value `v > 0`: returns `DefaultTicksPerBar / Quantizations[v - 1]` (`PianoRoll.cpp:5132`).
2. `DefaultTicksPerBar` is 192 (`include/TimePos.h:39`). `Quantizations` = `{1, 2, 4, 8, 16, 32, 64, 3, 6, 12, 24, 48, 96, 192}` (`include/Editor.h:36-38`). So `192 / Quantizations[i]` ranges over 192 … 1; every entry is at least 1 and the division is nonzero.
3. The note-length items are the "Last note" entry plus `NUM_EVEN_LENGTHS = 6` (`1/1 … 1/32`) and `NUM_TRIPLET_LENGTHS = 5` (`1/3 … 1/48`) (`src/gui/editors/PianoRoll.cpp:126-127`, `:359-375`). So for `noteLen > 0`, `192 / N` ranges over 192, 96, 48, 24, 12, 6, 64, 32, 16, 8, 4, all nonzero.
4. The quantize model's range is `0 … size - 1` (`src/core/ComboBoxModel.cpp:35`, set by `addItem`), and `AutomatableModel` clamps every value to that range (`src/core/AutomatableModel.cpp:606`, `:616`). So `quantization()` only ever sees indices 0 … 14, and the zero-divisor path is not reachable through the models.
5. The only caller of `StepRecorder::start()` is `PianoRoll::toggleStepRecording()` (`src/gui/editors/PianoRoll.cpp:4462-4485`, calls at `:4474` and `:4478`). Both pass `newNoteLen()` as the step length; `m_pianoRoll.quantization()` is read inside `start()` at `StepRecorder.cpp:69`. A search for `m_stepRecorder.start` and `StepRecorder::start` over `src`, `include` and `tests` finds no other caller.

The finding's "no quantization returns 0" is not a value the code produces. "Note lock" maps to the note length or 12, never 0.

## Adjacent hazard (not fixed here, separate cause)

`src/core/StepRecorder.cpp:178` divides by `m_stepsLength`: `m_curStepLength = (m_curStepLength / m_stepsLength) * newLength;`. `m_stepsLength` is set from `setStepsLength()` (`StepRecorder.cpp:183`) and can be 0 when "Last note" is selected and the last note had zero length: `newNoteLen()` returns `m_lenOfNewNotes` in that case (`PianoRoll.cpp:5199-5204`), and `m_lenOfNewNotes` is copied from an existing note's `length()` at `PianoRoll.cpp:1905`. `Note`'s default length is `TimePos(0)` (`include/Note.h:106`). If a step is in progress (`StepRecorder.cpp:94-95`, `notePressed`) and the note length changes (`PianoRoll.cpp:5108-5110`, `noteLengthChanged` → `setStepsLength(newNoteLen())`), line 178 divides by that 0.

This is a zero step length, not a zero quantization, and it needs a zero-length note to reach `m_lenOfNewNotes`. I did not reproduce it at runtime and did not change it: it is out of scope for this finding and needs its own repro and regression.

## Commands run

No production code changed, so no RED/GREEN regression was run. The verdict rests on the read-only searches and file reads above, all done with `rg` and `Read` on the worktree.

| Step | Command | Result |
| --- | --- | --- |
| Read quantization / step code | `rg -n "quantization\(\)|m_quantizeModel|quantization\b"` over `src` and `include`; read `PianoRoll.cpp:5118-5132`, `:5199-5207`, `StepRecorder.cpp:29-95`, `:173-240` | read, citations above |
| Constants and models | `rg -n "DefaultTicksPerBar"`, `rg -n "NUM_EVEN_LENGTHS|NUM_TRIPLET_LENGTHS"`, read `include/Editor.h:36-38`, `rg -n "setRange" src/core/ComboBoxModel.cpp`, read `src/core/AutomatableModel.cpp:606,616` | read, citations above |
| Caller search | `rg -n "stepRecorder|m_stepRecorder\.start|StepRecorder::start" src include tests` | only `PianoRoll.cpp:4474,4478` call `start()` |
| Enclosing function | `awk` / `rg -n "^bool PianoRoll::"` before line 4474 | `PianoRoll::toggleStepRecording()` at `:4462` |

## Gates

`feature_check.py` could not write its logs. It creates `<worktree>/../<name>-feature-check` (`qwen/feature_check.py:71`), a sibling of the worktree outside the write-jail, and failed with `PermissionError` (`logs/feature-check-1.log`, exit 1). I did not edit the shared script. I ran the same eight commands from its `SIMPLE` and `RATCHET` lists (`feature_check.py:22-30`) in the worktree and logged each one to the LOG FOLDER:

| Gate | Command (cwd = worktree) | Exit | Log |
| --- | --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 | `gate-fork-sources-2.log` |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 | `gate-all-sources-reproduce-2.log` |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 | `gate-no-upstream-regression-2.log` |
| check-namespace | `tests/scripted/check-namespace` | 0 | `gate-check-namespace-2.log` |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 | `gate-no-tautology-2.log` |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 | `gate-doc-figures-2.log` |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 | `gate-complexity-2.log` |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 | `gate-file-length-2.log` |

The two ratchet gates exit 1 on the base as well: 7 complexity and 19 file-length regression lines. The gate passes only when none of those lines is new and in a changed file. I applied that rule by hand (`ratchet-analysis-2.log`, base `cf3618e61`, the cached listing): changed files = `docs/qwen-fixes-2026-10/cluster-new-p9-446.md` only, and new items in changed files = 0 for both gates. So both count as PASS under the script's own rule.

Result by the script's rule: all six simple gates exit 0; both ratchets have 0 new items in changed files. The script itself was not run to completion, so this is my reproduction of its logic, not its output.

## Self-check

- **Regression test fails with the product change reverted and passes with it (exit codes).** Not applicable: there is no product change and no regression test, because the finding is not real (verdict above). No RED or GREEN exit codes exist to report.
- **Test is deterministic.** Not applicable: no test was added.
- **Fix at the cause; every caller and sibling checked.** No fix. The callers of `quantization()` and of `StepRecorder::start()` were checked (`PianoRoll.cpp:4474,4478`; the search in the command table). The adjacent zero divisor at `StepRecorder.cpp:178` was found and is reported above as a separate cause, not changed.
- **No collateral damage.** Only `docs/qwen-fixes-2026-10/cluster-new-p9-446.md` is changed. No project file, save format or limit changes.
- **FALSE-POSITIVE evidence (file:line).** `PianoRoll.cpp:5118-5132` (no branch returns 0); `include/TimePos.h:39` (192); `include/Editor.h:36-38` (`Quantizations` all ≥ 1); `PianoRoll.cpp:126-127` (note-length counts); `src/core/ComboBoxModel.cpp:35` and `src/core/AutomatableModel.cpp:606,616` (range clamp).
- **submit_check.py.** Its first run failed on: no commit yet (the commit follows this report), the untracked report (cleared by the commit), the missing Self-check section (this section), and the gates item (the `PermissionError` above). The gates item stays red for that reason until the script can write its logs; this is a sandbox limit, not a gate result.
