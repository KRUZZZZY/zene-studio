Verdict: FIXED

new-p16-209: FIXED

Finding: Lua wrappers dereference a project object that the command-queue flush deleted. The finding cites `src/core/ScriptBindings.cpp:687-692, 797-801` (historical lines). On the current base the same code is `LuaTrack::name()` (now `src/core/ScriptBindings.cpp:703`) and `LuaPatternClip::noteCount()` (now `:813`).

Root cause, confirmed by reading the code: each accessor checks its `QPointer` before calling `ScriptEngine::instance()->flushCommandsForRead()` and then dereferences the pointer with no second check. The flush applies the queue on the apply side. A queued project replacement (`project.new`, `project.open`) deletes the object, the `QPointer` clears, and the dereference runs on a null pointer. `flushCommandsForRead()` is at `src/core/ScriptEngine.cpp:727-747`; `applyCommands` runs inline there when called off the worker thread.

The same unchecked-after-flush pattern existed in five more accessors in the same file, all fixed the same way:

| Accessor | Before (base) | After (this branch) |
| --- | --- | --- |
| `LuaBoolModel::value()` | `src/core/ScriptBindings.cpp:554-559`: `m_model->value()` after flush | `:561` `return m_model ? m_model->value() : false;` |
| `LuaInstrumentTrack::name()` | `:589-595` | `:597` `return m_track ? m_track->name() : QString();` |
| `LuaInstrumentTrack::volume()` | `:613-618` | `:621` `return m_track ? m_track->getVolume() : 0;` |
| `LuaInstrumentTrack::panning()` | `:630-635` | `:639` `return m_track ? static_cast<int>(m_track->panningModel()->value()) : 0;` |
| `LuaTrack::name()` (the finding's site) | `:692-698` | `:703` `return m_track ? m_track->name() : QString();` |
| `LuaPatternClip::name()` | `:768-774` | `:780` `return m_clip ? m_clip->name() : QString();` |
| `LuaPatternClip::noteCount()` (the finding's site) | `:801-806` | `:813` `return m_clip ? static_cast<int>(m_clip->notes().size()) : 0;` |

The pre-flush early return is kept, so a null wrapper still returns without touching the queue (no behaviour change for invalid views). Only the post-flush dereference changed. Sites checked and left alone, as already correct: `LuaPatternClip::lengthTicks()` (`:785-790`, ternary after the flush), `LuaPatternClip::note()` (`:808-818`, null check after the flush), `LuaSong::tempo()`/`masterVolume()` (no `QPointer`; they call `song()` after the flush). Not audited: the `flushCommandsForRead()` callers in `ScriptDawBindings.cpp`, `ScriptDawEdit.cpp`, `ScriptDawEffects.cpp` and `ScriptCommandBridge.cpp`. They are outside the finding's file and were not changed.

Regression test: `tests/src/core/ScriptBindingsTest.cpp:510` `readAcrossAQueuedProjectReplacementIsSafe()`, with helper `queueProjectNew()` at `:56`. Each of seven reads gets a fresh view, then a queued `project.new` RegistryCall (the same request `zene.command()` queues, but enqueued without that call's own flush). The read's flush applies it, deletes the object, and the test asserts both the sentinel return and `!isValid()` afterwards. The test runs on the test thread, where the flush applies inline (`ScriptEngine.cpp:746`). It does not exercise the worker-thread apply pump. Determinism: no clock, sleeps or threads.

Note on the RED run: the first read in the test (`LuaTrack::name`) crashes before later reads run, so the RED run does not show each sibling failing separately. The sibling reads are covered by the GREEN run and the revert in step 4b, which fails the whole test.

Lua-level reachability: `zene.command()` flushes before it returns, so a script alone cannot leave a project replacement pending when it calls `view:name()`. The test therefore builds the pending state from C++. That is the same queue state a read sees when a replacement is queued from another path.

## Commands and results

All builds use `env CCACHE_TEMPDIR=$TMPDIR/ccache-new-p16-209` because the default ccache temp dir is not writable from this sandbox (`build-1-red.log` failed on that, EXIT=2). `build-2-red.log` failed on a namespace error in my test helper (EXIT=2), fixed before the RED build recorded below (`build-3-red.log`, EXIT=0). Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-209.r1/logs/`.

| Evidence | Command (from worktree or `build/tests`) | Result |
| --- | --- | --- |
| RED build, test with no product fix | `cmake --build build --target ScriptBindingsTest -j4` (`build-3-red.log`) | EXIT=0 |
| RED run | `ctest -R '^ScriptBindingsTest$' --output-on-failure` from `build/tests` (`test-1-red.log`) | EXIT=8; `SIGSEGV, SEGV_MAPERR, accessing address 0x0000000000000000` inside `readAcrossAQueuedProjectReplacementIsSafe` |
| GREEN build, with fix | `cmake --build build --target zene ScriptBindingsTest -j4` (`build-4-green.log`) | EXIT=0 |
| GREEN run | `ctest -R '^ScriptBindingsTest$' --output-on-failure` (`test-2-green.log`) | EXIT=0; 1/1 passed |
| 4b: save the fix as a patch | `git diff -- src/core/ScriptBindings.cpp > logs/fix.patch` | EXIT=0 |
| 4b: revert the fix | `git apply -R logs/fix.patch` | EXIT=0 |
| 4b: rebuild reverted | `cmake --build build --target zene ScriptBindingsTest -j4` (`build-5-reverted.log`) | EXIT=0 |
| 4b: RED on reverted code | `ctest -R '^ScriptBindingsTest$' -V --output-on-failure` (`test-3-reverted-RED.log`) | EXIT=8; SIGSEGV at address 0x0, same test |
| 4b: re-apply the fix | `git apply logs/fix.patch` | EXIT=0 |
| 4b: rebuild with fix | `cmake --build build --target zene ScriptBindingsTest -j4` (`build-6-regreen.log`) | EXIT=0 |
| 4b: GREEN with fix | `ctest -R '^ScriptBindingsTest$' -V --output-on-failure` (`test-4-regreen-GREEN.log`) | EXIT=0; `Totals: 30 passed, 0 failed`; `readAcrossAQueuedProjectReplacementIsSafe()` PASS |
| Family build | `cmake --build build --target <ScriptDawEffectsTest ControlAutomationScriptTest ScriptCommandBridgeTest ScriptDawBindingTest ScriptDawRemovalUndoTest ScriptDawModelRegistrationTest ScriptDawQueueFullTest ScriptDawPairReservationTest ScriptEngineTest ScriptImportTest ScriptClockTest ScriptMemoryBudgetTest ScriptStabilisationTest ScriptBudgetActionTest> -j4` (`build-8-family.log`) | EXIT=0. An earlier attempt (`build-7-family.log`) also named `LuaApiSurface`, which is a ctest entry, not a make target, so make stopped at once. |
| Doc-manifest test | `ctest -R '^LuaApiSurface$' -V --output-on-failure` (`test-5-luaapisurface.log`) | EXIT=0; 1/1 passed |
| Family ctest (final source) | `ctest -R 'Script|ControlAutomationScriptTest' --output-on-failure` (`test-6-family.log`, repeated on the final rebuild as `test-8-final-family.log`) | EXIT=8; 15 tests run, 12 passed, 3 failed (see below) |
| Baseline for the 3 failures: revert fix, rebuild, run the 3, re-apply | `git apply -R`, `cmake --build build --target zene ControlAutomationScriptTest ScriptEngineTest ScriptMemoryBudgetTest -j4` (`build-9-baseline-unfixed.log`, EXIT=0), `ctest -R 'ControlAutomationScriptTest|ScriptEngineTest|ScriptMemoryBudgetTest' --output-on-failure` (`test-7-baseline-unfixed.log`), `git apply` | Baseline EXIT=8. The same 6 `FAIL!` assertions as with the fix. |
| Final rebuild on fixed source | `cmake --build build --target zene <family targets> -j4` (`build-10-final-fixed.log`) | EXIT=0 |
| Feature gates | Run as `python3 -I` on a copy of `feature_check.py` with two paths relocated (see below): `python3 -I $TMPDIR/fc-new-p16-209/feature_check_relocated.py <worktree>` (`gates-2-feature-check.log`) | EXIT=0; RESULT PASS; all 8 rows PASS |

Family result detail: `ScriptBindingsTest` 30 passed, 0 failed (the new test is one of the 30). `ScriptDawQueueFullTest` passed. The 3 failing tests are the same on the unfixed code: `ControlAutomationScriptTest` (4 `FAIL!`: no instrument track with parameters in the fresh song, because the `TripleOscillator` plugin is not found in this build), `ScriptEngineTest::testInstrumentParameterReadWrite` (`displayName()` is `"dummy"` instead of `TripleOscillator`; same cause), and `ScriptMemoryBudgetTest::aSystemAllocationFailureIsNotReportedAsBudgetExhaustion` (a system allocation of an impossible size returned non-null in this environment). None touches the changed accessors. They are pre-existing in this environment and are not claimed fixed.

Gate note: the stock `feature_check.py` exited EXIT=1 with `PermissionError` (`gates-1-feature-check.log`). It creates its log folder at `<worktree>/../<name>-feature-check`, which is outside the write-jail. I did not edit the shared script. I ran an unmodified copy in `$TMPDIR` with two lines relocated: `ROOT` (so it reads the already-cached merge-base baseline in `qwen/gate-baselines/`) and the log folder (into the run folder). The gate logic is unchanged. The baseline for `639e1d23e` was cached, so the ratchet rows compare against the same base as the stock run.

Files committed for this verdict: `src/core/ScriptBindings.cpp` (fix), `tests/src/core/ScriptBindingsTest.cpp` (regression test), `docs/qwen-fixes-2026-10/cluster-new-p16-209.md` (this report). Both product files are already in `tests/fork-sources.txt` (`src/core/ScriptBindings.cpp` at line 1884, `include/ScriptBindings.h` at 1092). The test file is an existing registered test, so no registration change is needed.

## Self-check

- RED/GREEN: RED is `test-1-red.log` (EXIT=8, SIGSEGV in the new test, before any product edit). GREEN is `test-2-green.log` (EXIT=0). Step 4b proves the test depends on the fix: reverting only the product patch gives `test-3-reverted-RED.log` (EXIT=8), and re-applying gives `test-4-regreen-GREEN.log` (EXIT=0, 30 passed).
- Determinism: the test uses no wall-clock, sleeps or races. The queue is drained on the test thread. The apply path is the same one the existing `savedViewsOfAReplacedProjectAreInvalidated` test uses.
- Cause and callers: all seven unchecked post-flush dereferences in `ScriptBindings.cpp` are fixed; the other read-after-flush sites in this file were checked and are already correct. Callers in other files were not audited (see above).
- Collateral: the pre-flush early returns are unchanged, so invalid views still skip the flush exactly as before. Save and load paths and the realtime audio thread are not touched. The 3 pre-existing failures are shown to be identical with and without the fix.
- Gates: `gates-2-feature-check.log` (EXIT=0, all rows PASS, run through the relocated copy described above).
- Completion check: `python3 -I /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p16-209 > logs/submit-check-1.log 2>&1` gave EXIT=1. PASS: commits on the branch, working tree clean, this report, test registered/changed. FAIL: `gates (feature_check.py)` only. `submit_check.py` runs the stock `feature_check.py`, which writes its logs to `<worktree>/../<name>-feature-check`, a folder outside the write-jail; it exits with `PermissionError` (`gates-1-feature-check.log`, EXIT=1). The same gate logic passes with the log folder relocated (`gates-2-feature-check.log`, EXIT=0, all eight rows PASS). This row needs the stock gate to run with a writable log folder; I did not edit the shared script or its paths. The gate-result claim here rests on `gates-2-feature-check.log`, which is the relocated run.
- Answers to the completion check's questions:
  - Regression test fails with the product change reverted and passes with it: RED `test-1-red.log` EXIT=8 (SIGSEGV), revert-RED `test-3-reverted-RED.log` EXIT=8, GREEN `test-4-regreen-GREEN.log` EXIT=0 (30 passed).
  - Deterministic: no clock, sleeps or threads. The injected state is a queued `project.new` RegistryCall built in `queueProjectNew()` (`ScriptBindingsTest.cpp:56`) and drained by the read's own flush on the test thread.
  - Cause and siblings: the seven unchecked post-flush dereferences in `ScriptBindings.cpp` are fixed (table above). The other read-after-flush sites in that file were checked and are already correct. Callers in other files were not audited.
  - Collateral: no limit or check was added. The change only re-reads a `QPointer` after the flush. Serialisation, project load/save and audio-thread code are not touched. The three pre-existing failures are identical with and without the fix (see the family table).
  - Verdict is FIXED, so no ALREADY-FIXED or FALSE-POSITIVE claim needs a code citation.
