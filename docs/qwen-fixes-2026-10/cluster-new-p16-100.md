FIXED

# Cluster new-p16-100 — toFloat accepts non-finite project values

## Verdict

REAL. `LocaleHelper::toFloat` let NaN and ±infinity through to the caller unchanged, so a project
file with `nan`, `inf` or `1e999` in a float attribute could put a non-finite value into an
automatable model.

## Evidence (current file:line)

- `include/LocaleHelper.h:58-67` (before the fix) had the guard
  `!std::isinf(d) && fabs(d) > FLT_MAX`. The `!std::isinf(d)` term means infinity never enters the
  branch, and `std::fabs(NaN) > max` is false, so NaN also skipped the branch. NaN and ±inf were
  returned unchanged as `float`.
- Fix line: `include/LocaleHelper.h:61` is now
  `if (!std::isfinite(d) || std::fabs(d) > std::numeric_limits<float>::max())`, which sends NaN,
  ±inf and float overflow to the `ok = false; return 0.0f;` branch (lines 62-65).
- `src/core/Mixer.cpp:155-163` (`hasNonFiniteFloatAttribute`) used `toFloat` and tested the
  result with `std::isfinite`. Once `toFloat` returns 0.0f for non-finite input, that detector can
  never fire, so the Mixer guard would stop protecting its defaults. The fix changes this helper to
  read `LocaleHelper::toDouble` and test `std::isfinite` on the double. This is why
  `src/core/Mixer.cpp` must change: it used `toFloat`'s non-finite return as its detector.
- `tests/src/plugins/DispersionNonFiniteTest.cpp` (header comment at lines 46-47 and the test
  bodies): the old test wrote `nan` into an XML fixture and relied on the float parse to inject it.
  That path no longer produces NaN, so the test now injects NaN directly into the model.
  The guard it checks (the Nyquist clamp) is unchanged.

## Contract

- A failed float conversion returns `0.0f` with `ok = false`, the same as the existing too-large
  branch. Callers that already handle overflow now also handle NaN and infinity through that path.
- `toDouble` is deliberately unchanged. A double can represent inf and NaN, and its `ok` reports
  only whether the locale parse succeeded. Changing it would alter the Mixer detector and every
  double caller.

## Regression test

`tests/src/core/LocaleHelperTest.cpp` has these slots (line numbers are the slot's function start):
- `nonFiniteTextIsRejected()` (line 99): `nan`, `NaN`, `inf`, `-inf`, `+inf`. Each must return
  `0.0f` with `ok == false`.
- `overflowLiteralsAreRejected()` (line 113): `1e999`, `-1e999`, `3.5e38`. Each must return
  `0.0f` with `ok == false`.
- `nullOkStillRejects()` (line 126): `toFloat("nan")` with a null `ok` must return a finite `0.0f`.
- `floatMaxBoundaryStillLoads()` (line 134): `1e38` and `FLT_MAX` load with `ok == true`; a value
  just above FLT_MAX is rejected.
- `projectLoadKeepsNonFiniteOutOfTheModel()` (line 152) and
  `projectLoadKeepsNonFiniteOutOfTheAttribute()` (line 160): a child `<volume value="nan"/>` and an
  attribute `volume="nan"` must not load a non-finite model value.

In the p7 RED run, five slots fail: `nonFiniteTextIsRejected`, `overflowLiteralsAreRejected`,
`nullOkStillRejects`, `projectLoadKeepsNonFiniteOutOfTheModel` and
`projectLoadKeepsNonFiniteOutOfTheAttribute`. `floatMaxBoundaryStillLoads` passes in RED.

The test is registered in `tests/CMakeLists.txt` (line 117), `tests/fork-sources.txt` (line 3979)
and `tests/all-sources.txt` (line 3040). It is not listed in `tests/upstream-modifications.txt`.

## Commands and exit codes

Measured post-commit in p7. The exit codes are written into each log by the command itself
(`APPLY_EXIT`, `BUILD_EXIT`, `TEST_EXIT`, `EXIT`):

| Step | Command (worktree root) | Measured exit | Log |
|---|---|---|---|
| RED | `git apply -R logs/fix.patch`; `cmake --build build --target LocaleHelperTest`; `ctest -R '^LocaleHelperTest$'` in `build/tests` | TEST_EXIT=8 (APPLY_EXIT=0, BUILD_EXIT=0) | `logs/p7-red.log` |
| GREEN | `git apply logs/fix.patch`; `cmake --build build --target LocaleHelperTest DispersionNonFiniteTest MixerStripTest MixerToolsTest ProjectStateTest AutomatableModelTest`; `ctest -R '^(LocaleHelperTest\|DispersionNonFiniteTest\|MixerStripTest\|MixerToolsTest\|ProjectStateTest\|AutomatableModelTest)$'` | TEST_EXIT=0, 6/6 passed (APPLY_EXIT=0, BUILD_EXIT=0) | `logs/p7-green.log` |
| Feature check | `python3 feature_check.py <worktree>` | EXIT=0, `RESULT: PASS` | `logs/p7-feature-check.log` |

Before the RED run, `git apply --check -R logs/fix.patch` exited 0. `grep '^diff --git'` on the patch
names only `include/LocaleHelper.h` and `src/core/Mixer.cpp`.

Earlier history, kept as log-summary facts and not re-run here (the earlier logs carry no exit lines):
baseline `logs/test-1.log` 5/5 passed; the Mixer mutation (guard reverted to `toFloat`) in
`logs/test-4.log` shows three `FAIL!` assertions in MixerStripTest; `logs/test-4b.log` shows
MixerStripTest passing after the restore.

## Scope and collateral

- Deterministic: the test uses a direct string parse and direct model injection. It has no timing,
  sleeps or threads.
- No audio-thread, undo or control changes.
- No collateral: finite values, including `FLT_MAX`, still parse. The Mixer non-finite defaults are
  preserved (proven by MixerStripTest). The Dispersion guard is preserved.
- `src/core/DataFile.cpp:1240` (the load-time upgrade of `vol`) now writes `0` instead of `nan` for
  a malformed `vol`. This is the intended rejection.

## Other findings

- The pinned zynaddsubfx submodule copy still accepts non-finite values. It was not changed; it is
  outside this cluster.

## Self-check

1. RED/GREEN exits, measured in p7: RED is `TEST_EXIT=8` (`logs/p7-red.log`, fix reversed); GREEN is
   `TEST_EXIT=0`, 6/6 (`logs/p7-green.log`, fix restored); feature check `EXIT=0` with
   `RESULT: PASS` (`logs/p7-feature-check.log`). Earlier history, log summaries only: baseline 5/5
   (`test-1.log`); the Mixer mutation shows three MixerStripTest `FAIL!` assertions (`test-4.log`);
   MixerStripTest passes after the restore (`test-4b.log`).
2. Determinism: direct parse and direct model injection. No timing, sleeps or threads.
3. Cause and completeness (caller audit done): the cause is the `toFloat` guard. Checked: the sibling
   that used `toFloat` as a detector (`src/core/Mixer.cpp:155-163`, now reads `toDouble`), the
   Dispersion test injection, and `DataFile.cpp:1240`. Caller audit:
   `rg -n 'LocaleHelper::toFloat' src include plugins --glob '!build/**'` finds 14 lines. 13 are
   tracked call sites in six files, classified as:
   - Loaders: `AutomatableModel.cpp:430,451` (value and initValue on load); `DataFile.cpp:1367`
     (`vol`) and `DataFile.cpp:2283` (`value`); `AutomationClip.cpp:1306-1307,1315-1316`
     (time-map values and tangents on load).
   - Upgrade writer: `DataFile.cpp:1240` (legacy `vol` upgrade on load, written back).
   - Control setter: `AutomationClip.cpp:275` (`setTension`, `_new_tension`).
   - GUI editor: `FloatModelEditorBase.cpp:205` (text entry into a model).
   - Preset parsers: `VstPlugin.h:72`, `CarlaVst2Legacy.cpp:57` (parameter text in presets).

   None is a render or audio-thread path. The 14th line, `plugins/ZynAddSubFx/zynaddsubfx/src/Misc/QtXmlWrapper.cpp:512`,
   is in the pinned submodule. The packet records that it uses the submodule's own copy; I did not
   re-verify that. It is not changed here (see Other findings). No tracked caller relies on a
   non-finite return any more: the one that did (Mixer) now reads `toDouble`. `toDouble` is unchanged
   on purpose (see Contract).
4. No collateral and no limit rejection: finite values still parse, including the FLT_MAX boundary
   (`floatMaxBoundaryStillLoads`, line 134). The Mixer defaults are preserved (MixerStripTest passes). The
   `DataFile.cpp:1240` change is the intended rejection of a malformed `vol`: it writes `0`, not
   `nan`. No new limit was added, so no caller is rejected for exceeding one.
5. Not applicable: the verdict is REAL, not ALREADY-FIXED or FALSE-POSITIVE. The REAL evidence is
   `include/LocaleHelper.h:58-67` (old condition; fix at line 61), `src/core/Mixer.cpp:155-163`,
   and the Dispersion test change, as cited above.
