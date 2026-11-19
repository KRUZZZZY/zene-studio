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
| `test-1-red.log` | `cd build/tests && ctest -R RoutingGraphTest --output-on-failure` | EXIT=8; 16 passed, 7 failed (0% of 1 test) |
| `build-2.log` | `cmake --build build --target RoutingGraphTest` | EXIT=0 (`[100%] Built target RoutingGraphTest`) |
| `test-2-green.log` | `cd build/tests && ctest -R RoutingGraph --output-on-failure` | EXIT=0; 3/3 passed |
| `build-3-red.log` | `cmake --build build --target RoutingGraphTest` | EXIT=0 (`[100%] Built target RoutingGraphTest`) |
| `test-3-red.log` | `cd build/tests && ctest -R '^RoutingGraphTest$' --output-on-failure` | EXIT=8; 16 passed, 13 failed (0% of 1 test) |
| `build-3-green.log` | `cmake --build build --target RoutingGraphTest` | EXIT=0 (`[100%] Built target RoutingGraphTest`) |
| `test-3-green.log` | `cd build/tests && ctest -R '^RoutingGraphTest$' --output-on-failure` | EXIT=0; 1/1 passed, 100% |
| `build-mut1.log` / `test-mut1.log` | rebuild with `sanitizeAlpha` reverted to passthrough, then test | build EXIT=0; test EXIT=8, 5 failed |
| `build-mut2.log` / `test-mut2.log` | rebuild with the non-finite fallback removed, then test | build EXIT=0; test EXIT=8, 8 failed |
| `build-mut3.log` / `test-mut3.log` | rebuild with the `setAlpha` clamp removed, then test | build EXIT=0; test EXIT=8, 1 failed |
| `gates-1.log` | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p8-199` | EXIT=0; 8/8 PASS, RESULT: PASS |
| `gates-final.log` | same feature check after this report was written | EXIT=0; 8/8 PASS, RESULT: PASS |

The three mutation runs show the new tests fail when each part of the guard is
removed, so they exercise the fix and not merely the build.

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
