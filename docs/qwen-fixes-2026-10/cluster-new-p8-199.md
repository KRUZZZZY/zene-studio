FIXED

## Verdict

REAL: before this fix `OnePoleLowPassNode` accepted any `alpha` from a project
file, the constructor and `setAlpha`, and used it directly in the audio
recurrence.

- `src/core/RoutingNodes.cpp:50` - `loadParam()` converted the serialized
  attribute with `toFloat(&ok)` and returned any finite value; only parse
  failure fell back.
- `src/core/RoutingNodes.cpp:106` - `state += m_alpha * (dst[f] - state);`
  the one-pole recurrence. With `alpha = 3` the error is multiplied by
  `1 - alpha = -2` every frame, so the state alternates in sign and grows by
  2x per frame; with `nan`/`inf` the output is non-finite immediately.
- `src/core/RoutingNodes.cpp:121` - `loadSettings()` assigned the unchecked
  value straight to `m_alpha`.
- `include/RoutingNodes.h:68` - the explicit constructor stored its argument
  unchecked (`m_alpha(alpha)`), and `include/RoutingNodes.h:76` -
  `setAlpha()` did the same (`m_alpha = alpha`).

A malformed or hostile project file could therefore make the filter diverge to
inf/NaN and poison downstream nodes. The added tests reproduce that on the
pre-fix tree.

## Test matrix

All added cases are in `tests/src/core/RoutingGraphTest.cpp` (an existing
file). No new test file was needed: the final feature gate reports
`file-length ... no new item in your files`, so the added code did not create
a new over-length item and the existing registration covers it.

| Test | Input | Expected |
| --- | --- | --- |
| `LoadRejectsUnstableAlpha(above-one)` | `<param name="alpha" value="3"/>` | alpha = 1.0 |
| `LoadRejectsUnstableAlpha(below-zero)` | value `-1` | alpha = 0.0 |
| `LoadRejectsUnstableAlpha(nan)` | value `nan` | alpha = 0.5 (default) |
| `LoadRejectsUnstableAlpha(inf)` | value `inf` | alpha = 0.5 |
| `LoadRejectsUnstableAlpha(-inf)` | value `-inf` | alpha = 0.5 |
| `UnstableAlphaCannotDivergeInProcess` | alpha = 3, 8 x 256 frames of DC 1.0 | every sample finite and abs <= 1.0001 |
| `SetAlphaClampsAndIgnoresNonFinite` | setAlpha 3, -1, 0.25, NaN, inf, 1, 0 | 1, 0, 0.25, 0.25, 0.25, 1, 0 |
| `ConstructorSanitisesAlpha` | ctor 3, -1, NaN, 0.25 | 1, 0, 0.5, 0.25 |
| `LoadSettingsOnExistingNodeKeepsLastGoodAlpha(above-one)` | node 0.25, load 3 | 1.0 |
| `LoadSettingsOnExistingNodeKeepsLastGoodAlpha(below-zero)` | node 0.25, load -1 | 0.0 |
| `LoadSettingsOnExistingNodeKeepsLastGoodAlpha(nan/inf/-inf)` | node 0.25 | 0.25 (last good) |

## Fix

`src/core/RoutingNodes.cpp` gains a file-local
`sanitizeAlpha(candidate, fallback)`: non-finite values return the fallback,
everything else is clamped with `std::clamp(candidate, 0.0f, 1.0f)`.
`OnePoleLowPassNode::setAlpha()` keeps the current value as fallback, the
constructor uses 0.5f, and `loadSettings()` keeps the last good alpha.
`include/RoutingNodes.h` turns the constructor and `setAlpha()` into
declarations so the sanitising definitions in the `.cpp` are used.
`process()` is unchanged.

## Commands

Logs are under
`/home/kruzzzzy/.config/kads/zene/state/runs/new-p8-199.r1/logs/`.

| Log | Command | Result |
| --- | --- | --- |
| `build-1.log` | `cmake --build build --target RoutingGraphTest` | EXIT=0 (`[100%] Built target RoutingGraphTest`) |
| `test-1-red.log` | `cd build/tests && ctest -R RoutingGraphTest --output-on-failure` | EXIT=8; 16 passed, 7 failed. This is the **earlier** version of the test set, before the `loadSettings` and constructor cases were added; it is not the RED for the final test. |
| `build-1b.log` | `cmake --build build --target RoutingGraphTest` | exit not recorded in the log; build finished with `[100%] Built target RoutingGraphTest` |
| `test-1b-red.log` | `cd build/tests && ctest -R RoutingGraphTest --output-on-failure` | exit not recorded in the log; ctest reported failures - 16 passed, 13 failed. This is the RED for the final test set. |
| `build-2.log` | `cmake --build build --target zene RoutingGraphTest RoutingGraphLiveTest RoutingGraphScheduleTest` | exit not recorded in the log; all four targets built (`[100%] Built target zene`, `... RoutingGraphTest`, `... RoutingGraphLiveTest`, `... RoutingGraphScheduleTest`) |
| `test-2-green.log` | `cd build/tests && ctest -R RoutingGraph --output-on-failure` | exit not recorded in the log; 3/3 passed (`100% tests passed out of 3`) |
| `build-3-red.log` | `cmake --build build --target RoutingGraphTest` | exit not recorded in the log; build finished with `[100%] Built target RoutingGraphTest` |
| `test-3-red.log` | `cd build/tests && ctest -R RoutingGraphTest --output-on-failure` | exit not recorded in the log; ctest reported failures - 16 passed, 13 failed (0% of 1 test) |
| `build-3-green.log` | `cmake --build build --target RoutingGraphTest` | exit not recorded in the log; build finished with `[100%] Built target RoutingGraphTest` |
| `test-3-green.log` | `cd build/tests && ctest -R RoutingGraphTest --output-on-failure` | exit not recorded in the log; 1/1 passed, 100% |
| `build-mut1.log` / `test-mut1.log` | rebuilt `RoutingGraphTest` with the upper clamp removed (`std::max` only), then `cd build/tests && ctest -R RoutingGraphTest --output-on-failure` | build exit not recorded in the log, target built; test exit not recorded in the log, ctest reported failures - 24 passed, 5 failed |
| `build-mut2.log` / `test-mut2.log` | rebuilt `RoutingGraphTest` with the `isfinite` guard removed, then `cd build/tests && ctest -R RoutingGraphTest --output-on-failure` | build exit not recorded in the log, target built; test exit not recorded in the log, ctest reported failures - 21 passed, 8 failed |
| `build-mut3.log` / `test-mut3.log` | rebuilt `RoutingGraphTest` with the `setAlpha` clamp removed, then `cd build/tests && ctest -R RoutingGraphTest --output-on-failure` | build exit not recorded in the log, target built; test exit not recorded in the log, ctest reported failures - 28 passed, 1 failed |
| `build-3b-1.log` / `test-3b-1.log` | rebuilt `RoutingGraphTest` with mutation (iv), the `loadSettings` bypass (settings assigned unchecked), then `cd build/tests && ctest -R '^RoutingGraphTest$' --output-on-failure` | build exit not recorded in the log, target built; test exit not recorded in the log, ctest reported failures - 18 passed, 11 failed |
| `build-3b-2.log` / `test-3b-2.log` | rebuilt `RoutingGraphTest` with mutation (v), the constructor bypass (`m_alpha(alpha)`), then `cd build/tests && ctest -R '^RoutingGraphTest$' --output-on-failure` | build exit not recorded in the log, target built; test exit not recorded in the log, ctest reported failures - 28 passed, 1 failed (`ConstructorSanitisesAlpha`) |
| `test-3b-green.log` | `cd build/tests && ctest -R RoutingGraph --output-on-failure` | exit not recorded in the log; 3/3 passed |
| `gates-1.log` | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p8-199` | exit not recorded in the log; 8/8 PASS, RESULT: PASS |
| `gates-final.log` | same feature check after this report was written | exit not recorded in the log; 8/8 PASS, RESULT: PASS |

### Step 4b, re-run against the committed fix (`bd71b7293c`)

Working tree clean at the start, product code identical to the commit. The
patch was extracted with
`git diff HEAD~1 HEAD -- include/RoutingNodes.h src/core/RoutingNodes.cpp > <logs>/fix.patch`
(EXIT=0, 74 lines, only those two files; the plain `git diff` is empty because
the repo is already committed). It was reverse-applied to get the RED build and
re-applied to get GREEN.

| Log | Command | Result |
| --- | --- | --- |
| `fix.patch` | `git diff HEAD~1 HEAD -- include/RoutingNodes.h src/core/RoutingNodes.cpp > <logs>/fix.patch` | EXIT=0; non-empty (74 lines), touches only the two product files |
| `fix.patch` | `git apply -R <logs>/fix.patch` | EXIT=0 |
| `build-4b-red.log` | `cmake --build build --target RoutingGraphTest` | EXIT=0 (`[100%] Built target RoutingGraphTest`), recorded as the last line of the log |
| `test-4b-red.log` | `cd build/tests && ctest -R RoutingGraphTest --output-on-failure` | EXIT=8; 16 passed, 13 failed; recorded as the last line of the log |
| `fix.patch` | `git apply <logs>/fix.patch` | EXIT=0 |
| `build-4b-green.log` | `cmake --build build --target RoutingGraphTest` | EXIT=0 (`[100%] Built target RoutingGraphTest`), recorded as the last line of the log |
| `test-4b-green.log` | `cd build/tests && ctest -R RoutingGraphTest --output-on-failure` | EXIT=0; 1/1 passed, 100%; recorded as the last line of the log |
| - | `git status --short` after the GREEN run | EXIT=0, empty output (no modified files) |
| - | `git diff HEAD~1 HEAD -- include/RoutingNodes.h src/core/RoutingNodes.cpp \| diff - <logs>/fix.patch` | EXIT=0 (product diff equals the patch) |
| `build-4b-family.log` | `cmake --build build --target zene RoutingGraphTest RoutingGraphLiveTest RoutingGraphScheduleTest` | EXIT=0, all four targets built; recorded as the last line of the log |
| `test-4b-family.log` | `cd build/tests && ctest -R RoutingGraph` | EXIT=0; 3/3 passed; recorded as the last line of the log |

The mutation runs show the new tests fail when each part of the guard is
removed, so they exercise the fix and not merely the build: (i) upper clamp
removed - 5 failures, (ii) `isfinite` guard removed - 8 failures, (iii)
`setAlpha` clamp removed - 1 failure, (iv) `loadSettings` bypass - 11 failures,
(v) constructor bypass - 1 failure (`ConstructorSanitisesAlpha`).

## Scope

- `process()` has no diff: the audio render thread path is untouched.
- No control commands were added, and no command registry changed.
- TSan is not run locally; the change is a load-time/setter guard and adds no
  threading, locking or shared state, so no concurrency is involved.

## Other findings

- `ConstantSourceNode` still stores an unclamped `value` from its constructor,
  `setValue()` and `loadSettings()` (`include/RoutingNodes.h:46,54`,
  `src/core/RoutingNodes.cpp:83,88`). `GainNode` likewise stores an unclamped
  `gain` (`include/RoutingNodes.h:92,99`,
  `src/core/RoutingNodes.cpp:160,165`). Both accept non-finite and arbitrarily
  large finite values; that is a separate finding and was not changed here.

## Self-check

Logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p8-199.r1/logs/`.
An exit code is cited only where the named log contains it.

1. **Regression test fails with the change reverted and passes with it.**
   Earlier session, with `fix.patch` reverse-applied: `test-4b-red.log:72`
   `Totals: 16 passed, 13 failed` and `test-4b-red.log:83` `EXIT=8`. After
   re-applying: `test-4b-green.log:5` `100% tests passed out of 1` and
   `test-4b-green.log:8` `EXIT=0`. The mutation run of this session
   (`test-6-red.log:77`, `test-8-green.log:10`) is in the note below.

2. **Deterministic.** The new tests use literal inputs only. The XML comes from
   `filteredDocument(QStringLiteral("3"))`, and
   `UnstableAlphaCannotDivergeInProcess` feeds a fixed DC buffer for 8 periods
   of 256 frames (`tests/src/core/RoutingGraphTest.cpp:775-795`). A search of
   that file for `sleep|chrono|this_thread|std::thread|rand\(|time\(` printed no
   lines. The seams are the XML document passed to `RoutingGraph::load` and the
   direct constructor and `setAlpha` calls. No wall-clock time or race is
   involved.

3. **Fix at the cause, with callers and siblings checked.** Every write to the
   one-pole `m_alpha` now goes through `sanitizeAlpha`
   (`src/core/RoutingNodes.cpp:58-62`): the constructor (`:92`), `setAlpha`
   (`:98`) and `loadSettings` (`:139`). The recurrence at `:124` is unchanged.
   Outside the tests, the only place that creates the node is the factory
   `src/core/RoutingGraph.cpp:386` (default constructor, then `loadSettings`).
   No non-test caller calls `OnePoleLowPassNode::setAlpha`; the `setAlpha(`
   hits in `src/` are on colour objects. The sibling nodes do not follow the
   new rule: `ConstantSourceNode` (`include/RoutingNodes.h:46,54`,
   `src/core/RoutingNodes.cpp:83,88`) and `GainNode` (`include/RoutingNodes.h:92,99`,
   `src/core/RoutingNodes.cpp:160,165`) still store unclamped values. They are
   recorded under Other findings and are out of scope for this fix.

4. **No collateral damage.** `sanitizeAlpha` is the identity on finite values
   in [0, 1], so a saved project with an in-range alpha loads unchanged. Only
   values outside [0, 1] or non-finite values change: they fall back to the
   last good value (`setAlpha`, `loadSettings`) or to 0.5 (constructor,
   `src/core/RoutingNodes.cpp:92`). No in-repo caller passes an alpha outside
   [0, 1]. The [0, 1] bound is derived from the recurrence at `:124`
   (`state += m_alpha * (dst[f] - state)`), not from a documented range:
   `docs/ROUTING-GRAPH-LIVE.md` states none. `rg -l onepole_lowpass . -g '!build/**'`
   lists no uncompressed project file in the repo; compressed project files
   were not searched.

5. **ALREADY-FIXED / FALSE-POSITIVE.** Not applicable: the verdict is REAL.
   The pre-fix code is in the Verdict section; the fix is at
   `src/core/RoutingNodes.cpp:58-62,92,98,139`.

6. **Sibling commands.** `git show --stat bd71b7293c` lists four files: this
   report, `include/RoutingNodes.h`, `src/core/RoutingNodes.cpp` and
   `tests/src/core/RoutingGraphTest.cpp`. None is a command file, so this fix
   changes none of the 15 listed commands (`app.about` through
   `arrangement.get_state`). The branch diff against `fa4a5f2e7` does include
   `ControlCommands*` files from merged work, but none reaches the one-pole
   filter: a search of `src/core/ControlCommands*.cpp` and
   `include/ControlRegistry*.h` finds no `OnePoleLowPassNode` and no filter
   `alpha`. Their only `alpha` hits are "0.3.0-alpha" release-note references
   (`include/ControlRegistry.h:584`,
   `src/core/ControlCommandsExportPresets.cpp:19`,
   `src/core/ControlCommandsVcaEdit.cpp:40`,
   `src/core/ControlCommandsOutOfProcess.cpp:20`,
   `src/core/ControlCommandsMastering.cpp:41`). I did not open the handler
   bodies of the 15 listed commands.

## Extra mutation check (this session)

Mutation: the `sanitizeAlpha` body replaced by `return candidate;`
(`logs/mutation.patch`). `test-6-red.log:77` shows `Totals: 16 passed, 13 failed`.
The 13 `FAIL!` lines span five test functions: `LoadRejectsUnstableAlpha` (5
rows), `UnstableAlphaCannotDivergeInProcess` (`test-6-red.log:46`),
`SetAlphaClampsAndIgnoresNonFinite`, `ConstructorSanitisesAlpha` and
`LoadSettingsOnExistingNodeKeepsLastGoodAlpha` (5 rows). The ctest summary is
at `test-6-red.log:81-87`. This log has no `EXIT=` line, so the exit code of
this run is not recorded and is not claimed here.

Revert with `git apply -R` of the patch: `test-8-green.log:10` shows
`100% tests passed out of 3`. That log also has no `EXIT=` line, so the exit
code of the revert run is not recorded either.

The mutation and revert builds were not logged in the log folder; they went to
a tmp path. `build-5.log` is the earlier 3-target build, and `build-7.log` does
not exist, so neither is claimed to cover this run.
