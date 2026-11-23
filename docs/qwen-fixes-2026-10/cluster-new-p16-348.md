ALREADY-FIXED

# Cluster new-p16-348 - drawing automation can request billions of points

## Verdict

**ALREADY-FIXED.** The finding is real against the code it was written on: `automation.draw_shape`
accepts `start`/`end` anywhere in `0..INT_MAX` (schema `tickProperty()`,
`src/core/ControlVocabulary.cpp:117`) with `step` as low as 1
(`src/core/ControlCommandsAutomationLanes.cpp:299-303`), so the largest request is
`0..INT_MAX` by 1 - over two billion points. The handler does pass those values to point
generation (`src/core/ControlCommandsAutomationLanes.cpp:159`), but on this branch point
generation refuses the request before allocating anything.

The guard was landed earlier on this branch by commit `3f052a4b38` ("fix(automation): drawing a
shape could name billions of points", Luna cluster 2691), which is an ancestor of `HEAD`
(`git merge-base --is-ancestor 3f052a4b38 HEAD` returned 0 this session).

## Evidence (file:line, current tree)

- `src/core/AutomationClipTransforms.cpp:231-235` - the ceiling:
  `constexpr long long DrawShapeMaxPoints = 1 << 16;` (65536 points).
- `src/core/AutomationClipTransforms.cpp:248-257` - `AutomationClip::drawShape()` computes the
  resulting point count before it takes `m_clipMutex`, before it clears the range and before it
  inserts anything: `const long long wanted = (static_cast<long long>(end) - start) / step + 2;`
  then `if (wanted > DrawShapeMaxPoints) { return; }`.
- The command path is the only product caller of `drawShape`:
  `src/core/ControlCommandsAutomationLanes.cpp:159` (grep over the tree finds only this handler
  and the tests). With `start = 0`, `end = 0x7fffffff` and `step = 1` (all schema-legal) the
  callee computes `wanted = 2147483649 > 65536` and returns: no point is generated, no range is
  cleared, memory stays flat.
- Siblings in the same command file do not generate a span's worth of points: `transform`
  (`:169`) moves the points that already exist, `thin` (`:188`) removes, `set_curve` (`:202`)
  edits one existing node, and `automation.write_to_end`
  (`src/core/ControlCommandsAutomationTools.cpp:226-252`) writes exactly two nodes (`from` and
  `songEndFrom(from)`) regardless of the tick span. `automation.draw_shape` is the only
  span/step-driven generator.
- The existing regression is `tests/src/core/AutomationLanesTest.cpp:172-197`
  (`aDrawThatWouldNameBillionsOfPointsIsRefused`). It uses `0..200000` by 1 - the comment says
  it is "the same shape as 0..INT_MAX by 1, scaled down so the unfixed loop terminates" - and
  asserts the clip's points are untouched and that a draw under the cap still draws.

## Red/green proof that the guard and its test are load-bearing

I temporarily removed the two guard lines in `AutomationClipTransforms.cpp` (temporary edit,
restored immediately afterwards; `git diff --stat` is empty at `HEAD`), rebuilt and ran the
existing regression. Without the guard the test fails exactly as the finding predicts; with the
guard restored it passes. The sibling `AutomationLaneChildrenTest` passes as well.

| # | Command (from the worktree root unless noted) | Exit | Log / key output |
| --- | --- | ---: | --- |
| 1 | `cmake --build build --target AutomationLanesTest tripleoscillator -j4` | 0 | `build-1.log` |
| 2 | `cd build/tests && ctest -R '^AutomationLanesTest$' --output-on-failure` (baseline) | 0 | `test-1.log`: 1/1 passed |
| 3 | guard removed (temporary edit), `cmake --build build --target AutomationLanesTest -j4` | 0 | `build-2-red.log` |
| 4 | `cd build/tests && ctest -R '^AutomationLanesTest$' --output-on-failure` (RED) | 8 | `test-2-red.log`: `FAIL! : aDrawThatWouldNameBillionsOfPointsIsRefused`; `Actual 200001` / `Expected (before) 2` |
| 5 | guard restored (temporary edit reverted), `cmake --build build --target AutomationLanesTest AutomationLaneChildrenTest tripleoscillator -j4` | 0 | `build-3-green.log` |
| 6 | `cd build/tests && ctest -R '^(AutomationLanesTest\|AutomationLaneChildrenTest)$' --output-on-failure` (GREEN) | 0 | `test-3-green.log`: 2/2 passed |
| 7 | `cmake --build build --target zene -j4` | 0 | `build-4-zene.log` |
| 8 | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 | `gate-1-feature-check.log`: all 8 gates PASS |

Log folder: `/home/kruzzzzy/.config/kads/zene/state/runs/new-p16-348.r1/logs`.

The `ctest` exit 8 in row 4 is the proof that the existing test fails without the product
change; row 6 is the same test passing with it. Nothing else in this session changed product or
test files.

## Changed files

Product: none. The fix already exists on the branch (`3f052a4b38`).
Tests: none added or changed; the standing regression at
`tests/src/core/AutomationLanesTest.cpp:178` was re-proved red/green above.
Docs: `docs/qwen-fixes-2026-10/cluster-new-p16-348.md` (this report).

No new file under `src/`, no new control command, no snapshot or gate change.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it:** yes. With the
  guard removed, `ctest -R '^AutomationLanesTest$'` exited 8 with `Actual 200001` /
  `Expected (before) 2` (`test-2-red.log`); restored, the same run exited 0 (`test-3-green.log`).
- **The test is deterministic:** yes. It uses fixed ticks (0..200000, step 1), a fixed shape and
  seed, and asserts exact map contents; no clock, sleep, thread or random input.
- **The fix is at the cause and complete:** the ceiling is inside `AutomationClip::drawShape()`,
  the one function that turns a span/step into points, so every caller is covered. `grep` over the
  tree shows the only product caller is `automation.draw_shape`
  (`ControlCommandsAutomationLanes.cpp:159`); the test caller is the regression itself. The
  sibling commands (transform, thin, set_curve, write_to_end) do not generate span-sized point
  sets (see Evidence).
- **No collateral damage:** the guard predates this run and normal draws are unaffected - the
  normal-path command test `shapesThinTransformAndCurvesAreCommands` and the clip-level
  `shapesTransformAndThinning` both pass in `test-3-green.log`; a draw under the cap still draws
  (the regression's last assertion). No save/load, undo, registration or command surface changed
  in this run, so old projects and saved data behave exactly as before.
- **If ALREADY-FIXED: the code that shows it:** `src/core/AutomationClipTransforms.cpp:235`
  (ceiling) and `:256-257` (count computed and refused before the range is cleared or any node is
  inserted), reached from `src/core/ControlCommandsAutomationLanes.cpp:159`.

## Other findings

- `automation.draw_shape` still replies `ControlResult::success` when the point ceiling refuses
  the draw (the callee is `void` and the handler cannot see the refusal), so a caller gets a
  successful reply with the clip unchanged. This is not the memory-exhaustion bug in this
  cluster (the request is safely refused and nothing is allocated), but reporting the refusal at
  the command surface would be a separate behaviour change, so I did not make it here.
