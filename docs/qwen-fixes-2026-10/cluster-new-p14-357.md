# Cluster new-p14-357

Verdict: FIXED

The note-range upgrade wrote base notes back as `12` when the saved value was not a number, and it overflowed `int` for a base note of `INT_MAX - 11` and above. The fix shifts a base note only when it reads as a number and the shift stays inside `int`. Anything else is left exactly as saved. Fractional base notes keep their fraction.

## Evidence

Each claim below was read from the code on this branch.

- `src/core/UpgradeExtendedNoteRange.cpp:111-113` adds `canRaiseBaseNote`, the single range check: `value >= INT_MIN && value <= INT_MAX - semitones`. NaN fails both comparisons and is left alone.
- `src/core/UpgradeExtendedNoteRange.cpp:124-136` is the non-automated `basenote` attribute. The old code was `toInt()` then `+ 12`, which returned `0` for `"bad"` and `"57.5"` (so both became `12`) and overflowed for `"2147483636"`. It now uses `toDouble(&converted)` and only writes when `converted && canRaiseBaseNote(...)`.
- `src/core/UpgradeExtendedNoteRange.cpp:155-161` is the automated base-note element `value`. It now uses `toFloat(&converted)` with the same range check. The base note id is still collected when the value is unreadable, so pattern analysis is unchanged.
- `src/core/UpgradeExtendedNoteRange.cpp:176-202` is `fixAutomationPattern`, which shifts automation `time` nodes. A node whose `value` does not read is left whole, `outValue` included, because `upgrade_automationNodes` derives `outValue` from `value` (`src/core/DataFile.cpp:2230-2234`). A missing or unreadable `outValue` is no longer given `12`.
- `src/core/UpgradeExtendedNoteRange.cpp:466-485` is the preset path, a sibling of the same bug. The old code shifted twice by re-reading the attribute with `toInt()`. It now reads once, adds the Zyn/VeSTige/LV2/Carla correction into a single `shift` (12 or 24), and writes only when the value is valid and fits.
- Loader check: `src/core/AutomatableModel.cpp:440` reads a saved `basenote` with `LocaleHelper::toFloat`, so a fractional value such as `69.5` written by the upgrade loads as a float. How `IntModel` converts that float to its int value is not tested here.
- Callers: `DataFile::upgrade_extendedNoteRange` (`src/core/DataFile.cpp:2301-2307`) is the only caller of `UpgradeExtendedNoteRange`. No other test in `tests/src` references `basenote` or this upgrade.
- Older projects: the `time`-node change only affects `value`/`outValue` attributes that cannot be read. `AutomationClip::saveSettings` always writes `outValue` (`src/core/AutomationClip.cpp:1241`), and `upgrade_automationNodes` runs before this step (`src/core/DataFile.cpp:99`). For readable data the arithmetic is unchanged: `float + 12.` for `value` and `outValue`, `float + 12` for the element, `int + 12` for the integer presets.

## Regression test

`tests/src/core/UpgradeExtendedNoteRangeTest.cpp` gains 14 test functions, four helpers (`upgradeBaseNoteAttribute`, `upgradeBaseNoteElement`, `upgradeAutomationTime`, `upgradePresetBaseNote`) and a small result struct (`UpgradedTime`). No timing, sleep or race is involved: each helper builds an in-memory `QDomDocument` with fixed values and checks the attributes after `UpgradeExtendedNoteRange::upgrade()`.

Test matrix:

| Path | Case | Expected |
|---|---|---|
| Normal | `basenote="57"` | `69` (`plainBaseNoteAttributeStillShifts`) |
| Boundary | `basenote="2147483635"` | `2147483647` (`highestShiftableBaseNoteAttributeStillShifts`) |
| Overflow | `basenote="2147483636"` | unchanged (`overflowingBaseNoteAttributeIsPreserved`) |
| Malformed | `basenote="bad"` | unchanged (`malformedBaseNoteAttributeIsPreserved`) |
| Fraction | `basenote="57.5"` | `69.5` (`fractionalBaseNoteAttributeKeepsFraction`) |
| Automated element | `value="bad"` | unchanged (`malformedAutomatedBaseNoteIsPreserved`) |
| Automation time | `value="10" outValue="10"` | `22` / `22` (`automationTimeStillShiftsBothValues`) |
| Automation time | `value="bad" outValue="0"` | unchanged (`malformedAutomationTimeIsPreserved`) |
| Automation time | `value="10"`, no `outValue` | `22`, no `outValue` added (`missingAutomationOutValueIsNotInvented`) |
| Preset | no `basenote` | `69` (`absentPresetBaseNoteDefaultsTo57`) |
| Preset | `basenote="bad"` | unchanged (`malformedPresetBaseNoteIsPreserved`) |
| Preset | `basenote="57.5"` | `69.5` (`fractionalPresetBaseNoteKeepsFraction`) |
| Preset | `basenote="2147483636"` | unchanged (`overflowingPresetBaseNoteIsPreserved`) |
| Preset, affected instrument | `basenote="57"`, `zynaddsubfx` | `81` (`affectedPresetBaseNoteShiftsTwice`) |

The six pre-existing test functions are unchanged.

Audio thread, undo and commands: the change is in the project-load upgrade only. It allocates nothing on any audio path, adds no control command and does not touch undo. No header changed.

## Commands and exit codes

Working directory is the worktree unless marked. `ccache`'s configured temp directory (`/home/kruzzzzy/.local/share/zene-bunny-fix/dsh/home/ccache-tmp-r6`) is outside the write jail, so every compile here sets `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp/ccache-tmp-new-p14-357`. The first attempt without it failed on `Permission denied` (`build-1.log`, EXIT 2). That was a setup failure and is not a test result.

| Stage | Command | EXIT | Log |
|---|---|---:|---|
| RED build (new test, original source) | `CCACHE_TEMPDIR=… cmake --build build --target zene UpgradeExtendedNoteRangeTest -j4` | 0 | `build-2.log` |
| RED test | `cd build/tests && ctest -R '^UpgradeExtendedNoteRangeTest$' --output-on-failure` | **8** | `test-1.log`: 9 test functions fail; Totals 13 passed, 9 failed (includes `initTestCase`/`cleanupTestCase`) |
| Sensitivity: save diff | `git diff -- src/core/UpgradeExtendedNoteRange.cpp > …/logs/fix.patch` | 0 | `fix.patch` |
| Sensitivity: revert fix | `git apply -R …/logs/fix.patch` | 0 | — |
| Sensitivity build (reverted) | `CCACHE_TEMPDIR=… cmake --build build --target zene UpgradeExtendedNoteRangeTest -j4` | 0 | `build-4.log` |
| Sensitivity test (reverted) | `cd build/tests && ctest -R '^UpgradeExtendedNoteRangeTest$' --output-on-failure` | **8** | `test-4.log`: the same 9 test functions fail; Totals 13 passed, 9 failed |
| Sensitivity: restore fix | `git apply …/logs/fix.patch` | 0 | — |
| Sensitivity build (restored) | `CCACHE_TEMPDIR=… cmake --build build --target zene UpgradeExtendedNoteRangeTest -j4` | 0 | `build-5.log` |
| GREEN test (restored) | `cd build/tests && ctest -R '^UpgradeExtendedNoteRangeTest$' --output-on-failure` | **0** | `test-5.log` |
| GREEN test, verbose, final source | `cd build/tests && ctest -R '^UpgradeExtendedNoteRangeTest$' -V` | **0** | `test-final-verbose.log`: 20 test functions pass; Totals 22 passed, 0 failed (includes `initTestCase`/`cleanupTestCase`) |

The 9 cases that fail before the fix are `malformedBaseNoteAttributeIsPreserved`, `fractionalBaseNoteAttributeKeepsFraction`, `overflowingBaseNoteAttributeIsPreserved`, `malformedAutomatedBaseNoteIsPreserved`, `malformedAutomationTimeIsPreserved`, `missingAutomationOutValueIsNotInvented`, `malformedPresetBaseNoteIsPreserved`, `fractionalPresetBaseNoteKeepsFraction`, and `overflowingPresetBaseNoteIsPreserved`. The RED output (`test-1.log`) shows the wrong values: `"12"` for `"bad"`, `"12"` for `"57.5"`, and `"-2147483648"` for `"2147483636"`.

Affected family: `UpgradeExtendedNoteRangeTest` is 1 CTest target (`Test #277`), containing 20 test functions. QTest's Totals line reports 22 because it also counts `initTestCase` and `cleanupTestCase`. No run above executed zero tests.

Gates (see Self-check for the caveat about `feature_check.py`):

| Gate | Command | EXIT | Log |
|---|---|---:|---|
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 | `gate-2-fork-sources.log` |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 | `gate-3-all-sources.log` |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` (before the declaration edit) | 0 | `gate-4-no-upstream.log` |
| no-upstream-regression | same, after `tests/upstream-modifications.txt` edit | 0 | `gate-9-no-upstream-after-decl.log` |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 | `gate-5-no-tautology.log` |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 | `gate-6-doc-figures.log` |
| check-namespace | `tests/scripted/check-namespace` | 0 | `gate-check-namespace.log` |
| complexity (ratchet, red on train) | `bash tests/complexity-gate.sh --check` | 1 | `gate-7-complexity.log` |
| file-length (ratchet, red on train) | `bash tests/file-length-gate.sh --check` | 1 | `gate-8-file-length.log` |

Complexity and file-length are red on the train. Neither log names `UpgradeExtendedNoteRange` or `UpgradeExtendedNoteRangeTest`, so no item is new in a file this fix changed. The changed source is 490 lines (under the 500 limit) and the test file is 372 lines.

## Files committed

- `src/core/UpgradeExtendedNoteRange.cpp`
- `tests/src/core/UpgradeExtendedNoteRangeTest.cpp`
- `tests/upstream-modifications.txt` (appended a reason to the existing `UpgradeExtendedNoteRange.cpp` entry, following `new-p12-270`)
- `docs/qwen-fixes-2026-10/cluster-new-p14-357.md`

## Self-check

- **Test fails with the change reverted and passes with it.** RED: `test-1.log`, EXIT 8, 9 of 20 test functions fail. Reverted-patch rerun: `test-4.log`, EXIT 8, same 9. GREEN: `test-5.log` EXIT 0, and `test-final-verbose.log` all 20 pass. Both commands and exit codes are in the table above.
- **Deterministic.** The helpers build in-memory DOM documents with fixed literal values. Nothing sleeps, reads the clock or depends on threads. The sensitivity runs produced identical failure counts.
- **Fix at the cause and complete.** All four sites that shift a base note or automation value were checked: the attribute, the automated element, the automation `time` node and the preset path. The preset path's second shift re-read the attribute and would have re-introduced the bug, so both shifts are now one checked `shift`. The sibling `upgrade_automationNodes` step (`src/core/DataFile.cpp:2213-2237`) also converts a malformed `value` to `outValue` `0`. This change does not alter that step, and it leaves the malformed node as saved, so the `0` it wrote stays. That is a separate upgrade and I did not change it.
- **No collateral damage.** For readable data the arithmetic is unchanged (see the Evidence list). The overflow boundary is kept: `2147483635` still shifts to `2147483647`, which is tested. The check only blocks values that would overflow `int` or do not read as numbers. The one behaviour change for readable data is that a fractional `basenote` is kept as a fraction instead of being cut to an integer or turned into `12`, which is what the finding asked for. Not run: broader project-load tests (`DataFile`-level suites). They are not in this build tree's ctest list, so I did not claim them.
- **Verdict is FIXED,** so no already-fixed or false-positive proof is needed.

Gate caveat, stated plainly: `feature_check.py` and `submit_check.py` both write their logs to `<worktree>/../<name>-feature-check` (`qwen/feature_check.py:71`), a folder outside this lane's write jail. `feature_check.py` stopped with `PermissionError` at its `logs.mkdir` (`gate-feature-1.log`). `submit_check.py` output is in `submit-check-1.log`. I ran each gate script `feature_check.py` calls directly, with the same commands, and logged them as above. The base-listing comparison in `feature_check.py` was not run; the manual check is the grep described in the Gates section.
