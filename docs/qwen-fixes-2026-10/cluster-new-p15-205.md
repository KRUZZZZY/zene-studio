FIXED

# cluster new-p15-205: model sub-scenes survive a section reset

## Verdict

FIXED. `SessionSubScenes::clear()` is the section reset hook (`ProjectSection::clear`, reached through `ProjectSection::clearAll`). Before the fix it only emptied the preserved XML (`m_preserved`) and left `SessionModel::subScenes()` untouched, so a caller that used the section-level reset kept the sub-scene rows the model and the agent read. The fix clears the model rows as well, so the hook's contract holds regardless of who calls it or in what order.

The full `Song::clearProject` path masks the harm. `Song.cpp:1756` runs `m_sessionModel.clear()` before `Song.cpp:1798` runs `ProjectSection::clearAll()`, and `SessionModel::clear()` already empties `m_subScenes` (`SessionModel.cpp:340`). So a project close never showed the bug. The defect is in the hook itself: any caller that runs only the section reset, or runs it before the model clear, would leave stale rows. The hook's contract was broken and is now fixed regardless of caller order.

## Evidence

| Item | Location | What it shows |
|---|---|---|
| Before the fix | `include/SessionSubScenes.h:60` | `void clear() override { m_preserved.clear(); }`: the hook cleared only the preserved XML. |
| After the fix | `include/SessionSubScenes.h:60` | `void clear() override;`: the body moved out of line. |
| Hook registry | `include/ProjectSection.h:60-61` | `virtual void clear() = 0;` and the `clearAll` contract ("every registered section back to empty"). |
| Hook dispatch | `src/core/SongSections.cpp:59-61` | `ProjectSection::clearAll()` calls `clear()` on every registered section. |
| Project close order | `src/core/Song.cpp:1756` | `m_sessionModel.clear();` runs first and already empties the model rows. |
| Project close order | `src/core/Song.cpp:1798` | `ProjectSection::clearAll();` runs after, which is why the full path looked correct. |
| Model clear | `src/core/SessionModel.cpp:340` | `m_subScenes.clear();` inside `SessionModel::clear()`. |
| Session command | `src/core/ControlCommandsSession.cpp:580` | `model->clear();` in the session-state command path: `session.clear` already clears the model. |
| New definition | `src/core/SessionSubScenes.cpp:122-127` | `clear()` empties `m_preserved` and calls `session->setSubScenes({})` when `model()` is non-null. |

## Regression test

`tests/src/core/SessionSubSceneTest.cpp:362-391`, function `clearingTheProjectSectionsEmptiesTheModelRows`. It seeds the model with `setSubScenes`, calls `ProjectSection::clearAll()`, and checks three things: the model rows are empty, `session.get_state` reports no sub-scenes, and a project saved into a `QTemporaryDir` file has no `<z:subscenes` or `<z:subscene ` element. It runs three cases: one row, `SubScene::MaxCount` rows, and one row again after the earlier clears.

### RED / GREEN evidence

- RED (`test-1-red.log`): ctest reports `SessionSubSceneTest (Failed)` and "Errors while running CTest". Three `FAIL!` lines, each `SessionSubSceneTest.cpp(379)`: `'song->sessionModel().subScenes().empty()' returned FALSE (clearAll left the sub-scene rows in the model)`. The `QVERIFY2` in the lambda returns from the lambda only, so the 1-row, `SubScene::MaxCount` and repeat cases all ran and all failed. The packet records the exit as EXIT=8; that line is not in the log file itself.
- Unfixed re-check (`apply-reverse.log` EXIT=0, `build-3-unfixed.log` EXIT=0, `test-3-unfixed.log` EXIT=8): the fix reverted, the test still fails.
- Re-fix (`apply-refix.log` EXIT=0, `build-4-refixed.log` EXIT=0, `test-4-refixed.log` EXIT=0): the fix reapplied, the test passes.
- GREEN family (`test-2-green.log`): "100% tests passed, 0 tests failed out of 3". The exit line is not in the log file.
- Pre-commit family (`test-5-precommit.log`): all three pass, `EXIT=0`.

## Test matrix

| Case | Covered by |
|---|---|
| Normal, one row | `seedThenClear(1, "cleared-one")` |
| Exhaustion at `SubScene::MaxCount` | `seedThenClear(SubScene::MaxCount, "cleared-full")` |
| Repeat after earlier clears | `seedThenClear(1, "cleared-again")` |
| Null-model guard | `if (SessionModel* session = model())` in `SessionSubScenes::clear()`; the test runs with a model present, so the guard's null path is not exercised by this test. |
| Preserved / newer-section reconfiguration | `m_preserved.clear()` in the hook; covered by the existing round-trip tests in `SessionSubSceneTest` and `SessionSubSceneGridTest`, which stayed green in `test-5-precommit.log`. |
| Threading | UI / model thread only. No audio reader is involved and the test uses no clock, sleep or thread. |
| Determinism | Fixed row names and counts (`"Seed %1"`, `SubScene::MaxCount`), fixed track and scene indices, and a `QTemporaryDir` for the saved project. |
| Portability | The change uses only the existing Qt and C++ types already in the file. Verified by building on this host only. |

## Commands and exits

| Command | EXIT | Log |
|---|---|---|
| `cmake --build build --target zene SessionSubSceneTest ProjectSectionTest SessionSubSceneGridTest -j4` (pre-commit) | 0 | `logs/build-5-precommit.log` |
| `ctest -R '^(SessionSubSceneTest\|ProjectSectionTest\|SessionSubSceneGridTest)$'` (pre-commit) | 0 (3/3 pass) | `logs/test-5-precommit.log` |
| RED build and test | build log has no EXIT line; test EXIT=8 per packet, not in log | `logs/build-1-red.log`, `logs/test-1-red.log` |
| `apply-reverse` | 0 | `logs/apply-reverse.log` |
| unfixed build | 0 | `logs/build-3-unfixed.log` |
| unfixed test | 8 | `logs/test-3-unfixed.log` |
| `apply-refix` | 0 | `logs/apply-refix.log` |
| refixed build | 0 | `logs/build-4-refixed.log` |
| refixed test | 0 | `logs/test-4-refixed.log` |
| feature check (p4) | RESULT PASS, EXIT=0 | `logs/feature-check-1.log` |
| Direct runs 10/10, 4/4, 4/4 | not found in any log in the run directory; unverified | - |

## Self-check

1. **RED/GREEN exits.** RED: `SessionSubSceneTest` failed with three `FAIL!` lines at `SessionSubSceneTest.cpp(379)` (`test-1-red.log`); the EXIT=8 is per the packet and not written in that log. GREEN: the refixed test exits 0 (`test-4-refixed.log`), and the pre-commit family exits 0 (`test-5-precommit.log`).
2. **Deterministic seeded vectors and round trip.** The test uses fixed row names, counts and indices. The round trip writes a project into `QTemporaryDir`. No clock, sleep or thread is used.
3. **Cause, callers and siblings audit.** Cause: `SessionSubScenes::clear()` cleared only `m_preserved`. Callers: `ProjectSection::clearAll` via `Song::clearProject` (`Song.cpp:1798`); the model is cleared earlier at `Song.cpp:1756`, which masks the bug on that path. Siblings: `SessionModel::clear()` (`SessionModel.cpp:340`) and `session.clear` (`ControlCommandsSession.cpp:580`) already clear the model rows and were not changed.
4. **No collateral.** Full family green (`test-5-precommit.log`, 3/3). The existing project round-trip tests are inside `SessionSubSceneTest` and `SessionSubSceneGridTest`, and both pass in that run.
5. **ALREADY-FIXED / FALSE-POSITIVE.** N/A. The verdict is FIXED. The masking path (`Song.cpp:1756` before `Song.cpp:1798`) is recorded in the verdict section.

## Other findings

- The RED and test-2 logs do not contain their exit lines. The lead should take those exits from the packet's run record, not from these files.
- The direct runs 10/10, 4/4 and 4/4 do not appear in any log in the run directory. They are unverified here.
