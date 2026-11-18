FIXED

# Cluster new-p16-64 - Dispersion frequency clamp lets NaN reach the audio output

Finding: `plugins/Dispersion/Dispersion.cpp:68` (historical line; the clamp was `:70` on the base and is `:94` after the fix):
`std::min(value, limit)` returns its first argument when that argument is NaN, so the Nyquist
clamp did not sanitize a NaN frequency. Verdict REAL, fixed on this branch.

## Decision evidence (file:line, current tree)

- The controls are read unchecked in `DispersionEffect::processImpl`:
  `plugins/Dispersion/Dispersion.cpp:70-72` on the base commit `639e1d23e`; after the fix `:94-97`.
- NaN reaches the model and is kept: `include/AutomatableModel.h:753` (FloatModel ctor),
  `src/core/AutomatableModel.cpp:419` (project load: `setValue( LocaleHelper::toFloat(...) )`),
  `src/core/AutomatableModel.cpp:606` (`fittedValue` uses `std::clamp`, which returns a NaN
  value unchanged). A saved `<freq value="nan"/>` therefore loads as NaN.
- NaN propagation: `w0 = ... * freq` and `a0 = 1 + sin(w0)/(reso*2)` become NaN, so
  `apCoeff1`/`apCoeff2` are NaN for every sample. The test below measured 512/512 non-finite
  output samples for a NaN frequency and 512/512 for a NaN resonance.
- Feedback has the same hole: a NaN feedback poisons `m_feedbackVal`, measured 510/512 samples.
- Root-cause note: the shared root is that `AutomatableModel::fittedValue` accepts NaN for every
  model. Changing it would alter every plugin's loading and automation, so it is out of scope here.
  The fix is at the reader, which also covers automation and `setValue`, the other ways a NaN
  reaches the control.

## Fix

`plugins/Dispersion/Dispersion.cpp` (+ `#include <cmath>`):

- `finiteOr(value, fallback)` in an anonymous namespace returns the control value when it is
  finite, otherwise the fallback. The fallbacks are the controls' construction defaults, as
  constants: `DefaultFrequency = 200`, `DefaultResonance = 0.707`, `DefaultFeedback = 0`
  (`plugins/Dispersion/DispersionControls.cpp:37-39`).
- The model's init value is deliberately not used. An attribute-form project load runs
  `setInitValue(NaN)` (`src/core/AutomatableModel.cpp:440`; `setInitValue` at `:1058-1075`), so
  `initValue()` is NaN exactly when the control was loaded as NaN from an attribute. Review round 1
  showed that fallback still rendered 512/512 non-finite samples for the attribute form.
- Applied to the three float controls read in `processImpl` (frequency, resonance, feedback).
  The Nyquist clamp `std::min(..., m_sampleRate * 0.499f)` now sees a finite value.
- Finite values are unchanged; only NaN takes the default. Amount (IntModel) and DC (BoolModel)
  cannot hold NaN and are untouched.
- Realtime: the helper is inline, reads a float, and does not allocate, lock, log or wait.
- Undo: no model is changed, so no undo step is affected.

Not changed, deliberately: the dry and wet levels (`dryLevel()` / `wetLevel()`) come from the
shared `Effect` base, so a NaN there would poison every effect; that is a separate, wider fix.

## Regression test

`tests/src/plugins/DispersionNonFiniteTest.cpp` (new, fork-NEW; registered in
`tests/CMakeLists.txt`, `tests/fork-sources.txt`, `tests/all-sources.txt`).

The test builds an effect, loads its controls from project XML (the real load path), and renders
one impulse. Seven test functions plus init/cleanup:

1. `nonFiniteFrequencyRendersLikeTheDefault` - child-element form `<freq value="nan"/>`: all samples
   finite and equal to the default render (tolerance 1e-6).
2. `nonFiniteResonanceRendersLikeTheDefault` - same for resonance (child form).
3. `nonFiniteFeedbackRendersLikeTheDefault` - same for feedback (child form).
4. `attributeFormNonFiniteFrequencyRendersLikeTheDefault` - attribute form `freq="nan"` on the
   `<dispersion>` element. Also checks that the init value is NaN, so the init-value path is exercised.
5. `attributeFormNonFiniteResonanceRendersLikeTheDefault` - same for `reso="nan"`.
6. `attributeFormNonFiniteFeedbackRendersLikeTheDefault` - same for `feedback="nan"`.
7. `finiteFrequencyStillDrivesTheFilter` - a finite 1000 Hz frequency must differ from the default
   render by more than 1e-3 (guards against a fix that overrides every value).

Each NaN test first checks that the control really loaded as NaN (so it exercises the bug).
A NaN difference counts as infinite in the comparison, so a non-finite render cannot pass as equal.
The test is deterministic: no timing, threads or audio device; the engine is only initialised for
the sample rate.

## Evidence (exact commands and exit codes)

Round 1 rows below use log folder `new-p16-64.r1/logs`; round 2 rows (the attribute-form rework)
use `new-p16-64.r2/logs`.

Round 2 (rework: construction defaults + attribute-form regression):

| Step | Command (cwd) | Exit | Result |
| --- | --- | --- | --- |
| Build, reworked | `CCACHE_DISABLE=1 cmake --build build --target DispersionNonFiniteTest -j4` (log `build-1-green.log`) | 0 | no errors or warnings |
| GREEN, ctest | `ctest -R '^DispersionNonFiniteTest$' --output-on-failure` (build/tests), log `test-1-green-fix.log` | 0 | 100% of 1 |
| GREEN, binary | `./DispersionNonFiniteTest` (build/tests), log `test-2-green-direct.log` | 0 | Totals: 9 passed, 0 failed (7 functions + init/cleanup); attribute-form rows PASS |
| Reversed: rework only | `git diff -- plugins/Dispersion/Dispersion.cpp > rework.patch; git apply -R rework.patch`, rebuild, run binary, log `test-3-rework-reversed.log` | 3 | attribute frequency 512/512, resonance 512/512, feedback 510/512 non-finite; 3 FAIL, 6 PASS. The round-1 `initValue()` fallback fails exactly where the review said. |
| Reversed: whole fix (to base `639e1d23e`) | `git diff 639e1d23e -- plugins/Dispersion/Dispersion.cpp > full-fix.patch; git apply -R full-fix.patch`, rebuild, run binary, log `test-4-base-reversed.log` | 6 | 6 FAIL (all NaN cases, both forms), 3 PASS |
| Re-applied | `git apply rework.patch` / `git apply full-fix.patch` (forward), rebuild, run | 0 | GREEN again (`test-5-green-final.log`, `test-6-ctest-final.log`, ctest exit 0) |
| Plugin module | `CCACHE_DISABLE=1 cmake --build build --target dispersion -j4` (log `build-5-plugin-dispersion.log`) | 0 | no errors |
| zene | `CCACHE_DISABLE=1 cmake --build build --target zene -j4` (log `build-6-zene.log`) | 0 | no errors |
| PluginPorts, reworked | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` (build/tests), log `test-8-portsmig-final.log` | 8 | 82 passed, 1 failed (same LADSPA case as round 1), 2 skipped. Dispersion sha256 identical to reference `4d21a52a...`. |

Round 1 rows (`new-p16-64.r1/logs`):

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-64.r1/logs/`.

| Step | Command (cwd) | Exit | Result |
| --- | --- | --- | --- |
| RED, original code | `$TMPDIR/dnf-build/DispersionNonFiniteTest` (build/tests), log `test-1-red.log` | 3 | 3 FAIL (freq 512/512, reso 512/512, feedback 510/512 non-finite samples); 3 PASS |
| 4b revert | `git apply -R .../logs/fix.patch` | 0 | fix removed from the working tree |
| RED, reverted, ctest | `cmake --build build --target DispersionNonFiniteTest -j4` (ccache disabled, see below), then `ctest -R '^DispersionNonFiniteTest$' --output-on-failure` (build/tests), log `test-7-ctest-red-reverted.log` | 8 | `Test #594 ... Failed`, 0% passed |
| 4b re-apply | `git apply .../logs/fix.patch` | 0 | fix restored (identical to the staged change) |
| GREEN, ctest | same build, then `ctest -R '^DispersionNonFiniteTest$' --output-on-failure` (build/tests), log `test-8-ctest-green.log` | 0 | `Test #594 ... Passed`, 100% of 1 |
| GREEN, manual (same flags) | `python3 $TMPDIR/dnf-build/manual_build.py`, then the binary, log `test-3-green.log` | 0 | Totals: 6 passed, 0 failed (4 test functions + init/cleanup) |

Affected family: `ctest -R Dispersion` matches only this test (Test #594); the tree has no
registered `DispersionControls`, `Dispersion` or `DispersionControlDialog` ctests (the three
`tests/reference/Dispersion/` files are the frozen pre-migration copies used by the PART C
harness, not ctest entries). Count of tests executed: 4 test functions, 1 ctest entry.

PluginPortsMigrationTest (the behaviour-preservation harness that renders the live Dispersion
plugin against its frozen reference), `ctest -R '^PluginPortsMigrationTest$'` from build/tests, log
`test-9-portsmig.log`, exit 8: 82 passed, 1 failed, 2 skipped. The Dispersion comparison passes:
`migratedPluginsPreserveBehaviour` prints the same sha256 for the migrated and reference renders of
dispersion (32768 samples), and `dispersionRemainsStableAboveNyquist` passes. The one failure is
`nonRealtimeLadspaEffectsAreInvalid` ("'description != nullptr' ... SWH Decimator test fixture was not
loaded"), a LADSPA fixture that is absent here. It does not touch Dispersion. I did not build the base
to prove it fails there too, so it is recorded as unverified against base. Two SF2 cases skip
("Sf2Player is not built in this configuration").

Build notes:
- The first registered-target build failed with `ccache: error: failed to create temporary file ...
  Permission denied` (sandbox path, log `build-13-ctest-target.log`). Every build after that ran
  with `CCACHE_DISABLE=1` set for that one command only. No build configuration was changed by it.
- Building the `dispersion` plugin target (log `build-12-plugin-dispersion.log`, exit 0) made CMake
  re-check the tree automatically, because `tests/CMakeLists.txt` had changed. That is how
  `DispersionNonFiniteTest` became visible to ctest. I did not run a configure step myself: the
  bunny guard refused `cmake -S . -B build` (exit 97), as intended.
- Before the tree regenerated, the test was built by hand (`manual_build.py`, same compile flags
  and link line as `MultitapEchoBoundsTest`), which gave the RED and first GREEN runs.

Plugin build: `CCACHE_DISABLE=1 cmake --build build --target dispersion -j4` (log
`build-12-plugin-dispersion.log`), EXIT=0, `Built target dispersion`, no errors or warnings.

## Gates

`feature_check.py` cannot run from this lane: it writes its logs to
`<worktree>/../zene-qwen-new-p16-64-feature-check/`, outside the write-jail (PermissionError,
log `gate-1-feature-check.log`). I ran each of its commands by hand from the worktree, logs in
`gate-2-<name>.log` (round 1 in `new-p16-64.r1/logs`; round 2 re-run in `new-p16-64.r2/logs`, same exits and
the same 7 and 22 ratchet lines, none in a changed file):

| Gate | Exit | Note |
| --- | --- | --- |
| fork-sources | 0 | |
| all-sources-reproduce | 0 | new test path inserted in sorted position |
| no-upstream-regression | 0 | |
| check-namespace | 0 | |
| no-tautology | 0 | |
| doc-figures | 0 | |
| complexity (ratchet) | 1 | 7 REGRESSION lines, none in a file this branch changes (WasmSandbox, XyPadPairTest, DawProjectReadTracks, ControlCommandsMixerTools, ControlCommandsSessionRecordLand, ControlCommandsMixerStrip, ControlCommandsShortcuts). Red on the train on purpose. |
| file-length (ratchet) | 1 | 22 REGRESSION lines, none in a changed file. Red on the train on purpose. |

The changed files this branch has: `plugins/Dispersion/Dispersion.cpp`,
`tests/src/plugins/DispersionNonFiniteTest.cpp`, `tests/CMakeLists.txt`, `tests/all-sources.txt`,
`tests/fork-sources.txt`, `tests/upstream-modifications.txt`. Under the gate's rule (fail only on
items in changed files that are new against the base) both ratchets pass. I could not compute the
base listing, which needs a code-only checkout outside the jail; the rule does not depend on it here
because no listed item is in a changed file.

## Self-check

- Regression FAILS with the change reverted and PASSES with it. Round 2: whole fix reversed
  (`test-4-base-reversed.log`, exit 6), rework reversed (`test-3-rework-reversed.log`, exit 3), reworked
  fix (`test-2-green-direct.log`, exit 0, ctest `test-1-green-fix.log` exit 0). Round 1: test-1-red
  (original code, exit 3), test-7-ctest-red-reverted (ctest, exit 8), test-8-ctest-green (ctest, exit 0).
- Deterministic: no timers, sleeps, threads, files or audio device. The inputs are injected as
  project XML and an impulse buffer; the only environment input is the engine's sample rate, which
  is the same for both renders of a pair.
- At the cause, and complete for the reader: every read of the three float controls in
  `processImpl` now goes through `finiteOr`, with construction defaults that cannot be NaN. Both
  project forms are covered: the child element (`<freq value=...>`) and the attribute (`freq=...`),
  which also sets the init value. The only other reader is the constructor's
  `m_sampleRate` (engine value, not a control). `processImpl` is the only caller of the coefficient
  path. The root in `AutomatableModel::fittedValue` is recorded above and left as is (wider effect).
- No collateral damage: finite values take the same path as before (test 7 shows they still drive
  the filter; the default-vs-default comparisons are exact). Project files with finite values load
  the same. Only a NaN value, which before produced a silent NaN output, changes.
- Verdict is FIXED, not ALREADY-FIXED or FALSE-POSITIVE, so no "already fixed" proof is needed.

## Files committed

- `plugins/Dispersion/Dispersion.cpp` (fix)
- `tests/src/plugins/DispersionNonFiniteTest.cpp` (regression, new)
- `tests/CMakeLists.txt` (registration)
- `tests/fork-sources.txt`, `tests/all-sources.txt`, `tests/upstream-modifications.txt` (manifests)
- `docs/qwen-fixes-2026-10/cluster-new-p16-64.md` (this report)
