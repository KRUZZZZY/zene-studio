FIXED

# Cluster new-p22-66 — sample.normalize accepts NaN target_db

## Decision

REAL. Fixed on `bugs/qwen-new-p22-66`. Commit `6d0a1a220` carries the sample bakers, the test and the CMake
registration. The commit after it carries the `clip.set_gain` sibling, the manifests and this report.

### The defect

- `src/core/ControlCommandsSampleEdit.cpp:189-190` (`sample.amplify`) and `:241-242` (`sample.normalize`), as
  they were before the fix, checked the level with `x < lo || x > hi`. For NaN both comparisons are false, so
  NaN passed and the handler baked a NaN buffer. The finding's line 234 is the same check, now at `:241`.
- The value is reachable. Wire JSON cannot carry NaN, because Qt's parser rejects it. The Lua script bridge
  converts numbers with `lua_tonumber` (`src/core/ScriptCommandBridge.cpp:161`), so a script's `0/0` becomes a
  NaN `QJsonValue`. `ControlRegistry::invoke` validates it with `validateArgs`, whose `number` check is
  `value.isDouble()` (`src/core/ControlSchema.cpp:49`), which accepts NaN. The handler then runs.
- The same function had a second route to a non-finite bake that the finding does not name. The scale is
  `gainDbToLinear(target) / peak` (`src/core/ControlCommandsSampleEdit.cpp:255` before the fix). A subnormal
  peak such as 1e-39 makes the scale about 1e39, past float max, so it becomes inf and frames bake to inf or
  NaN. An infinite source sample makes the peak inf and the scale 0, and `inf * 0` is NaN. Both are the same
  "baked buffer is non-finite" defect, so both are fixed here.
- The non-baking sibling `clip.set_gain` has the same NaN-blind range test at
  `src/core/ControlCommandsClipEdits.cpp:347` before the fix. Its range is the one the `sample.amplify`
  description names ("the range both this bake and clip.set_gain declare"). A NaN there is stored in the
  clip's edits (`after.gain = gainDbToLinear(...)`, `ControlCommandsClipEdits.cpp:358`). It is fixed too, so the two commands agree.

### The fix

- `src/core/ControlCommandsSampleEdit.cpp:189-190`: `!std::isfinite(db) || db < -60.0 || db > 24.0`
  (`sample.amplify`, `gain_db`).
- `src/core/ControlCommandsSampleEdit.cpp:241-242`: `!std::isfinite(targetDb) || targetDb < -60.0 || targetDb > 0.0`
  (`sample.normalize`, `target_db`).
- `src/core/ControlCommandsSampleEdit.cpp:261-268`: after the existing silence refusal and before
  `sampleops::amplify`, refuse when `!std::isfinite(peak) || !std::isfinite(scale)`, with
  `ControlErrorKind::Refused`. This runs before `commitBake`, so no checkpoint is taken and no undo step is
  pushed. `<cmath>` is included at `:36`.
- `src/core/ControlCommandsClipEdits.cpp:348-349`: `!std::isfinite(db) || db < kMinGainDb || db > kMaxGainDb`
  (`clip.set_gain`). `<cmath>` is included at `:39`. The refusal returns before any edit or transaction.
- The idiom matches sibling handlers that already put `!std::isfinite(x) || ...` first, for example
  `src/core/ControlCommandsMixerTools.cpp:237` and `src/core/ControlCommandsNoteProbability.cpp:91`.

### Checked and not changed

- `sample.fade` takes no numeric argument, so it has nothing to guard.
- `sampleops::peakOf` (`src/core/SampleOperators.cpp:185`) uses `std::max`, which ignores NaN samples. A
  source that already holds NaN frames passes them through unchanged. A bake does not introduce them, so
  this is not part of the fix.
- No schema, command, or result shape changed. No new control command was added.

## Regression test

`tests/src/core/ControlSampleBakeGuardTest.cpp` (new), method `nonFiniteLevelsAreRefusedAndWriteNothing`.
It covers five refusals:

1. NaN `gain_db` on `sample.amplify`: `InvalidArgs`.
2. NaN `target_db` on `sample.normalize`: `InvalidArgs`.
3. A subnormal-peak source (only frame = 1e-39f) with `target_db` 0: `Refused`, message contains "finite".
4. An infinite-sample source with `target_db` 0: `Refused`, message contains "finite".
5. NaN `gain_db` on `clip.set_gain`: `InvalidArgs` (`:158-160`).

After each refusal the source frames are compared bit-for-bit with the frames before the call. A final
`control.undo` must remove the generated clip, which proves no refusal pushed an undo step.

It is a new file, not an addition to `tests/src/core/ControlSampleOperatorTest.cpp`. My first version added the
method there, which took that file to 541 code lines, and `file-length-gate.sh` then reported "new file over
500 lines". The helpers are copied, not shared, so the sibling test is unchanged.

Registration:
- `tests/CMakeLists.txt`: source entry, and `set_tests_properties(... QT_QPA_PLATFORM=offscreen)`.
- `tests/fork-sources.txt` and `tests/all-sources.txt`: one line each, placed before
  `ControlSampleOperatorTest.cpp`. `tests/all-sources-reproduce.sh` runs the header recipe of
  `all-sources.txt` and prints REPRODUCES, so that position is verified. `fork-sources.txt` has a multi-block
  recipe in its header that no script runs as a whole. The gate checks only registration for that file.

## Commands and results

Paths are relative to the worktree. EXIT codes are copied from the logs in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p22-66.r1/logs`.

Environment note: ccache could not write its temp files under `zene-bunny-fix/dsh/home` (the sandbox denied it).
Every build below sets `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/ccache-bunny/tmp-p22-66`. The first build attempt,
before that setting, exited 2 with "ccache: error: failed to create temporary file" (`build-1-red.log`).
`cmake .` by hand was refused by the build tree's configure guard (exit 97, `cmake-regen-final.log`). Make's own
regeneration worked, so I did not configure the tree.

### Regression proof (final changes: both fixes)

`logs/fix.patch` is the `ControlCommandsSampleEdit.cpp` diff, and `logs/clip-fix.patch` is the
`ControlCommandsClipEdits.cpp` diff. Both are relative to the pre-fix code.

| Step | Command (from the worktree or `build/`) | EXIT | Result |
|---|---|---|---|
| Save patch | `git diff -- src/core/ControlCommandsClipEdits.cpp > logs/clip-fix.patch` | 0 | 22 lines (`cmds-final3.log`) |
| RED | `git apply -R logs/fix.patch`; `git apply -R logs/clip-fix.patch`; `cmake --build . --target ControlSampleBakeGuardTest zene -j4`; `cd tests && ctest -R ControlSampleBakeGuardTest --output-on-failure` | 0; 0; 0; **8** | FAIL at `ControlSampleBakeGuardTest.cpp:122`, `nanGain.ok` Actual 1 Expected 0. 2 passed, 1 failed (`test-14-red-both.log`) |
| GREEN | `git apply logs/fix.patch`; `git apply logs/clip-fix.patch`; build; ctest | 0; 0; 0; **0** | 1/1 passed (`test-15-green-both.log`) |
| Sibling sub-case | `git apply -R logs/clip-fix.patch` (sample fix still applied); build; ctest | 0; 0; **8** | FAIL at `:160`, `nanClipGain.ok` Actual 1 Expected 0. The sample assertions before it passed (`test-16-clip-off.log`) |
| Restore | `git apply logs/clip-fix.patch`; build zene and test; ctest | 0; 0; 0; **0** | 1/1 passed (`test-17-final.log`) |

The sample-sub-case runs, from the earlier version of the same test, have the same assertions and line numbers:

| Step | Command | EXIT | Result |
|---|---|---|---|
| Subnormal scale | Replace `if (!std::isfinite(peak) || !std::isfinite(scale))` with `if (false)`; build; ctest | 0; **8** | FAIL at `:140`, `overflow.ok` Actual 1 (`test-9-scale-guard-off.log`) |
| Infinite sample | Replace it with `if (!std::isfinite(scale))`; build; ctest | 0; **8** | FAIL at `:152`, `infiniteSource.ok` Actual 1 (`test-10-peak-guard-off.log`) |

Both were restored. `git diff -- src/core/ControlCommandsSampleEdit.cpp` was byte-equal to `logs/fix.patch`
(`cmp` EXIT 0, `SAME_AS_FIX_PATCH`).

### Affected family

| Command | EXIT | Result |
|---|---|---|
| `cmake --build . --target ClipEditsTest ClipPolarityTest ClipEditsMenuTest ClipWaveformEditTest -j4` | 0 | built (`build-18-clip-family.log`) |
| `cmake --build . --target LinkedClipEditsTest -j4` | 0 | built (`build-19-linked.log`) |
| `cd tests && ctest -R "ClipEdits|ClipPolarity|ClipWaveformEdit|ControlSample|LinkedClipEdits" --output-on-failure` | 0 | 10/10 passed: ClipEditsTest, ClipPolarityTest, LinkedClipEditsTest, ControlSampleArrayTest, ControlSampleOperatorTest, ControlSampleBakeGuardTest, ControlSampleStereoTest, ClipEditsMenuTest, ClipWaveformEditTest, ControlClipPolarity (`test-20-family-final.log`) |

The first run of this family (`test-18-clip-family.log`) exited 8 with `LinkedClipEditsTest` "Not Run". I had
matched it by name but not built it. The build and the run above are the fix for that. The other 9 passed.

Test counts: ctest executed 1 test for `ControlSampleBakeGuardTest` and 10 for the family. Nothing ran zero tests.

### Gates

`python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` exits 1 without printing a table. It
fails with `PermissionError` while creating `<worktree>-feature-check/` (`gates-1.log`), which is outside the
write jail. It also builds a baseline worktree in `zene-cachyos` for the ratchets. I did not do either. Instead
I ran each gate command the script would run, from the worktree, with logs in the LOG FOLDER
(`gates-final3.log`):

| Gate | Command | EXIT |
|---|---|---|
| fork-sources (registration, plus rerun of the all-sources recipe) | `bash tests/fork-sources-gate.sh` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 (REPRODUCES) |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 |

The two ratchets are red on the train, as the brief says. Under feature_check's rule they fail only on items new
in files this branch changed. Neither ratchet's REGRESSION lines name `src/core/ControlCommandsSampleEdit.cpp`,
`src/core/ControlCommandsClipEdits.cpp`, `tests/src/core/ControlSampleBakeGuardTest.cpp` or `tests/CMakeLists.txt`
(`gate6-complexity.log`, `gate6-file-length.log`: 7 and 22 REGRESSION lines, all in other files). By that rule
both pass. I did not check this against the base listing, because that needs the baseline worktree.

`submit_check.py` also runs feature_check, so its gate line fails for the same reason (`submit-check-1.log`).

## Self-check

- **The regression test fails with the change reverted and passes with it.** Yes. RED `test-14-red-both.log`
  exit 8 at `:122`. GREEN `test-17-final.log` exit 0. The clip-only sub-case `test-16-clip-off.log` exit 8 at
  `:160`. The sample sub-cases exit 8 at `:140` and `:152`. The table above has every command and EXIT.
- **The test is deterministic.** It has no wall-clock timing, sleep, thread or randomness. The inputs are fixed
  values: `std::numeric_limits<double>::quiet_NaN()`, the float literal `1.0e-39f`, and
  `std::numeric_limits<float>::infinity()`. The source is set by a fixture call, `replaceSample`, with no
  checkpoint, so the test controls the state and the command under test starts from it.
- **The fix is at the cause, and the siblings are consistent.** The cause is a range comparison that is false
  for NaN, so the finite check comes first, in each handler that uses that range. The scale guard covers the
  other non-finite route into the same bake. Siblings checked by reading the code:
  - `sample.amplify`, `sample.normalize`: fixed.
  - `sample.reverse`, `sample.fade` (`ControlCommandsSampleEdit.cpp`): use the same `commitBake`, which this
    change does not touch. `sample.reverse` takes no numeric argument, and `sample.fade` takes only `direction`.
    Neither is affected.
  - `sample.crop`, `sample.make_unique` (`src/core/ControlCommandsSampleTools.cpp`), `sample.edit_range`
    (`src/core/ControlCommandsSampleRange.cpp`), `sample.generate` (`src/core/ControlCommandsSample.cpp`),
    `sample.spectrogram` (`src/core/ControlCommandsAudioAnalysis.cpp`), `sample.split_stereo`
    (`src/core/ControlCommandsSampleStereo.cpp`): registered in other files. The helpers `resolveBakeTarget`,
    `commitBake` and `sourceState` are in an anonymous namespace in `ControlCommandsSampleEdit.cpp`, so none of
    these share the changed code path. Not affected.
  - `clip.set_gain` (`src/core/ControlCommandsClipEdits.cpp:348-349`): fixed. It declares the same range.
  - Not audited: the other numeric `clip.*` edits (`src/core/ClipEdits.cpp`, `src/core/Clip.cpp` use
    `gainDbToLinear`/`peakOf`). I did not check them for the same NaN check. That is a gap in this report.
- **No collateral damage.**
  - Every value that was accepted before is still accepted. The ranges -60..24 (gain) and -60..0 (target) are
    unchanged. The new checks fire only for NaN or infinity.
  - The scale check fires only when the scale or the peak is non-finite. At 0 dB, a finite scale needs a peak
    above about 2.9e-39 (1/FLT_MAX), and at -60 dB about 2.9e-42. Those are subnormal levels. An infinite
    sample is non-finite audio. No legitimate audio is refused.
  - Save and load code is unchanged. A project already saved with a NaN clip gain still loads as it did
    before. This change does not repair it. That would be a separate data migration.
  - Old projects and rendering are unaffected, because no format, schema or result shape changed.
- **The verdict is FIXED, not ALREADY-FIXED or FALSE-POSITIVE.** The code evidence is the `:189-190` and `:241-242`
  before-state above. The verdict question does not apply.
- **Sibling items from `submit_check` (the `sample.*` list).** Answered in the "fix is at the cause" item above.

## Files committed

- Commit `6d0a1a220`: `src/core/ControlCommandsSampleEdit.cpp`, `tests/src/core/ControlSampleBakeGuardTest.cpp`,
  `tests/CMakeLists.txt`.
- The following commit: `src/core/ControlCommandsClipEdits.cpp`, `tests/src/core/ControlSampleBakeGuardTest.cpp`
  (the `clip.set_gain` assertion), `tests/fork-sources.txt`, `tests/all-sources.txt`,
  `docs/qwen-fixes-2026-10/cluster-new-p22-66.md`.

## Not done

- The full `ctest` suite was not run. I ran the affected families: the sample family, and the clip family that
  calls `clip.set_gain`.
- The other numeric `clip.*` edit commands were not audited for the same NaN check (see Self-check).
- `feature_check.py` could not run in this sandbox (`PermissionError` on its log directory, outside the write
  jail). The six gates above were run by hand. Complexity and file-length are reported as red on the train with
  no REGRESSION line in the changed files. That is the rule, but the base comparison is unverified.
- `sampleops::peakOf` ignores NaN samples, so a source that already contains NaN keeps it through a bake (see
  "Checked and not changed"). Out of scope for this finding.
