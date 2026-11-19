FIXED
new-p16-25: FIXED

# Cluster new-p16-25: a flam grace note rounds to silence

## Verdict

FIXED. A flam grace note from an audible source could round to volume 0 and sound silent. The fix keeps it at volume >= 1.

## Cause (evidence)

- `src/core/ControlCommandsNoteTools.cpp:236` (after the fix): `const long scaled = std::lround(sourceVolume * ratio);`. The old line was 235: `grace.setVolume(static_cast<volume_t>(std::lround(note->getVolume() * ratio)));`. With the 0.05 floor, any source volume below 10 gives `lround(< 0.5) = 0`.
- `src/core/ControlCommandsNoteTools.cpp:216`: the ratio is refused unless it is in 0.05..1.0, so the ratio can never be 0.
- `src/core/ControlCommandsNoteTools.cpp:490-491`: the command contract reads "(0.05..1 - a grace note always sounds -, 0.6 when left out)".
- `src/core/Note.cpp:365-367`: `Note::setVolume` clamps to `MinVolume..MaxVolume`.
- `include/volume.h:35-37`: `MinVolume = 0`, `MaxVolume = 200`, `DefaultVolume = 100`. Volume 0 is legal, so the clamp accepted the rounded 0 without complaint.

## Fix

`src/core/ControlCommandsNoteTools.cpp:233-238`: `sourceVolume > 0 ? max(1, scaled) : scaled`.

## Contract

- A grace note from an audible source (volume > 0) has volume >= 1.
- A silent source (volume 0) gives a silent grace note (volume 0).
- Normal ratios are unchanged: whenever `lround` gives >= 1 the value is identical. The test asserts a normal-path grace note of 60.
- Command surface: the test calls the registry command `note.flam`. The GUI Flam menu uses the same command id at `src/gui/noteeditor/NoteEditorMenus.cpp:91`: `run("note.flam", scoped, false)`.

## Test

`tests/src/core/NoteToolsValidationTest.cpp`, function `aFlamGraceNoteAtTheMinimumRatioStaysAudible` (+60 lines). It checks:
- (a) ratio 0.05 is accepted;
- (b) the grace note sits at tick 21, length 3, volume >= 1;
- (c) a silent source gives a grace note of volume 0;
- (d) the normal path gives volume 60;
- (e) one `control.undo` removes the grace note (2 notes back to 1).

## Evidence

Logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p16-25.r1/logs/`. These logs record EXIT lines but not the command text. Where a log has no EXIT line, this is stated.

| Log | Result |
|---|---|
| test-20-red.log | before the fix: `FAIL! : ...aFlamGraceNoteAtTheMinimumRatioStaysAudible() 'grace->getVolume() >= 1' returned FALSE`. No EXIT line. |
| test-24-p1b-red.log | `EXIT=8` |
| test-25b-mutant.log | `EXIT=8`. Actual `silentGrace->getVolume()` = 1, expected 0 (test line 186). |
| build-26-reversed.log | `EXIT=0` (product change reverted, rebuilt) |
| test-26-reversed-red.log | `EXIT=8`. `'grace->getVolume() >= 1' returned FALSE` |
| build-27-restored.log | `EXIT=0` (fix restored, rebuilt) |
| test-27-restored-green.log | `EXIT=0`. 100% tests passed, 2 of 2 |
| build-25a-green.log / test-25a-green.log | `EXIT=0` / `EXIT=0`. 100% tests passed, 2 of 2 |
| test-25c-restored.log | `EXIT=0`. 100% tests passed, 2 of 2 |
| test-21-green.log | 100% tests passed, 2 of 2. No EXIT line. |
| apply-reverse-4.log / apply-forward-4.log | `EXIT=0` / `EXIT=0` |
| patch-compare.log | `EXIT=0` |

Feature check PASS table, copied from `feature-check-3.log`:

```
feature-check kads-new-p16-25 (base fa4a5f2e7, 2 changed files; logs in .../build/feature-check)
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

## Realtime

`flam` is a control-command handler (`src/core/ControlCommandsNoteTools.cpp:198`). It runs on the control thread through the command registry, not in the audio callback. The change adds two `const` locals and a `std::max`. A grep of the added lines finds no new allocation, lock, or log call. No audio-thread code is touched.

## Undo

The diff adds and removes no undo checkpoint. The flam edit is still one clip step. Test (e) proves it: one `control.undo` takes the clip from 2 notes back to 1.

## Self-check

1. **Fails with the change reverted, passes with it.** Reverted: `build-26-reversed.log` EXIT=0, then `test-26-reversed-red.log` EXIT=8. Restored: `build-27-restored.log` EXIT=0, then `test-27-restored-green.log` EXIT=0.
2. **Deterministic.** The test uses fixed JSON arguments and clips built in the test itself. A grep of the added test lines for sleep, wait, clock, timer, chrono, rand finds 0 matches. Seam: `run(QStringLiteral("note.flam"), args)`.
3. **Fix at the cause, complete.** The only `lround` in `ControlCommandsNoteTools.cpp` is line 236. The ratio gate at line 216 keeps the ratio >= 0.05. The GUI caller (`NoteEditorMenus.cpp:91`) uses the same command id. I did not check other rounding sites outside this file.
4. **No collateral damage.** No change to serialization or loading, and no new limit or check. The only behavioural change: a grace note whose source volume is > 0 and would round to 0 is now 1. Normal values are asserted unchanged (60).
5. **Verdict.** FIXED, not ALREADY-FIXED or FALSE-POSITIVE. The cause is at `ControlCommandsNoteTools.cpp:236`. The red run `test-20-red.log` shows the failure before the fix.
