FALSE-POSITIVE

new-p21-17: FALSE-POSITIVE

The finding claims `newFromRecipe` in `src/core/ControlCommandsDocuments.cpp` captures `Song*` before `run("project.new")` and then uses the captured pointer for `saveProjectFile` and `restoreSession`, and that if `project.new` replaces the Song instance the pointer dangles. `project.new` never replaces the Song instance on this branch, so the captured pointer stays valid. No product file was changed.

## Evidence

- **The Song singleton is created once and deleted once.** `Engine::s_song` is declared at `include/Engine.h:146`, initialised to `nullptr` at `src/core/Engine.cpp:49`, assigned exactly once at `src/core/Engine.cpp:73` (`s_song = new Song;` inside `Engine::init`), and deleted exactly once at `src/core/Engine.cpp:131` (`deleteHelper( &s_song );` inside `Engine::destroy`). A search of `src/` and `include/` for `s_song =` finds no other assignment; a search for `new Song` finds no other allocation (`src/gui/...` hits are editors/transitions holding a pointer, not allocations).
- **`project.new` does not create a Song.** Its handler at `src/core/ControlCommandsProjectLifecycle.cpp:265-287` does `Song* song = Engine::getSong();` then `song->createNewProject()` or `song->createNewProjectFromTemplate(templ)`. It never assigns `Engine::s_song` and never deletes it.
- **Both `createNewProject` paths reset the same object in place.** `Song::createNewProject` at `src/core/Song.cpp:1844-1908` either forwards to `createNewProjectFromTemplate` (which is `loadProject(templ)` plus filename clearing at `src/core/Song.cpp:1913-1922`) or itself calls `clearProject()` at `src/core/Song.cpp:1867`. `Song::loadProject` at `src/core/Song.cpp:1970-2057` likewise loads into `this` and calls `clearProject()` at `src/core/Song.cpp:2057`. Neither path allocates a `Song`.
- **The code says the Song survives replacement.** `Song::clearProject` at `src/core/Song.cpp:1700` comments "The Song object survives project replacement, so issue a fresh path identity here." That is the design contract: a project load clears document state on the singleton, it does not swap the pointer.
- **The captured pointer in `newFromRecipe` is therefore the same object after `run("project.new")`.** `Song* const song = Engine::getSong();` at `src/core/ControlCommandsDocuments.cpp:209` is still `Engine::getSong()` after the two `run("project.new")` calls at `src/core/ControlCommandsDocuments.cpp:252` and `:256`. Its uses at `src/core/ControlCommandsDocuments.cpp:240` (inside the `restorePrevious` lambda passed to `restoreSession`) and `:255` (`song->saveProjectFile(path)`) are uses of the live singleton, not of freed memory. The rollback lambda captures `song` by value but the object it points at is never freed by `project.new`.

## Test matrix (written before concluding; no product edit)

| Path | Coverage |
| --- | --- |
| Normal: `project.new_document` mastering/live builds the recipe template and opens it | `newDocumentsCarryTheirTypesTemplate` in `tests/src/core/ArrangingCatchUpTest.cpp:280-296` — PASS (test-2.log) |
| Failure: unwritable template destination refuses before any `project.new` | `ControlSuccessPathsTest` recipe-template failure cases — PASS (test-3.log) |
| Failure: save fails after the recipe is built, session restored | `failedBuiltInDocumentSaveKeepsCurrentSession` in `tests/src/core/ControlSuccessPathsTest.cpp:544` — PASS (test-3.log) |
| Reconfiguration: pointer identity across `project.new` | No new test: the singleton contract above is the seam; a test asserting pointer equality would pass both before and after any "fix" and prove nothing about a dangling pointer that cannot occur |
| Audio-thread constraints | No product change; nothing new on the audio thread |
| Platform build | `ArrangingCatchUpTest` and `ControlSuccessPathsTest` build on this Linux configuration |

Because no product code changed, step 4b (reverse-apply the fix and show the RED) and a RED/GREEN pair do not apply. The existing family passing confirms no collateral damage.

## Commands and exit codes

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-17.r1/logs`

| Evidence | Command | Result |
| --- | --- | --- |
| Feature gate script | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-17 > .../gates-1.log 2>&1; echo EXIT=$?` | EXIT=0, RESULT PASS (8 rows) |
| Build affected test target, first try | `cmake --build build --target ArrangingCatchUpTest -j4 > .../build-1.log 2>&1; echo EXIT=$?` | EXIT=0 |
| Family via ctest, plugins not yet built | `ctest -R '^ArrangingCatchUpTest$' --output-on-failure > .../test-1.log 2>&1; echo EXIT=$?` (from `build/tests`) | EXIT=8. 12 passed, 1 failed, 1 skipped. The one failure is `queuedInstrumentHandlesAreRemovedBeforeTheirTrackCanDie` (dummyinstrument vs tripleoscillator) because the plugin `.so` files were not yet built in this tree. |
| Build required plug-ins | `cmake --build build --target tripleoscillator kicker -j4 > .../build-2.log 2>&1; echo EXIT=$?` and `cmake --build build --target eq lomm compressor analyzer -j4 > .../build-3.log 2>&1; echo EXIT=$?` | EXIT=0 both |
| Family via ctest, after plug-ins built | `ctest -R '^ArrangingCatchUpTest$' --output-on-failure > .../test-2.log 2>&1; echo EXIT=$?` (from `build/tests`) | EXIT=0. 100% tests passed, 1 of 1 |
| Build second family | `cmake --build build --target ControlSuccessPathsTest -j4 > .../build-4.log 2>&1; echo EXIT=$?` | EXIT=0 |
| Second family via ctest | `ctest -R '^ControlSuccessPathsTest$' --output-on-failure > .../test-3.log 2>&1; echo EXIT=$?` (from `build/tests`) | EXIT=0. 100% tests passed, 1 of 1 |
| Completion check, pre-report | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p21-17 > .../submit-check-1.log 2>&1; echo EXIT=$?` | EXIT=1, expected before this report was written |

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p21-17.md` (this report). No product file was changed.

## Self-check

Pre-submit run: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p21-17 > .../submit-check-1.log 2>&1` → EXIT=1 before this commit. Items it lists and my answer:

- **Regression test fails with the change reverted and passes with it.** Not applicable: there is no product change to revert and no new test. The verdict is not-real, so there is no RED to show. The existing families pass (`ctest` EXIT=0, 1/1 each, test-2.log and test-3.log).
- **Deterministic (no timing, sleeps, races).** No new test was written. The existing `ArrangingCatchUpTest` and `ControlSuccessPathsTest` binaries drive commands synchronously through `ControlRegistry::invoke` with no sleeps, timers or wall-clock reads on the `newFromRecipe` path.
- **Fix at the cause; callers and siblings checked.** No fix was made. The claim's premise was checked everywhere it could be: the only assignment to `s_song` (`src/core/Engine.cpp:73`), the only `project.new` handler (`src/core/ControlCommandsProjectLifecycle.cpp:265-287`), and both `createNewProject` paths (`src/core/Song.cpp:1844-1908`, `:1913-1922` via `loadProject` at `:1970-2057`). The sibling commands in the same file (`project.recent` at `src/core/ControlCommandsDocuments.cpp:295`, `project.switch` at `:331`) also re-read `Engine::getSong()` per call and are consistent.
- **No collateral damage.** No product file changed, so saved projects and existing behaviour are untouched. The existing `newDocumentsCarryTheirTypesTemplate` (`tests/src/core/ArrangingCatchUpTest.cpp:280`) and both `ControlSuccessPathsTest` failure-rollback cases pass.
- **Verdict evidence (FALSE-POSITIVE).** `include/Engine.h:146` + `src/core/Engine.cpp:49`, `:73`, `:131`; `src/core/ControlCommandsProjectLifecycle.cpp:265-287`; `src/core/Song.cpp:1700`, `:1844-1908`, `:1913-1922`, `:1970-2057`; `src/core/ControlCommandsDocuments.cpp:209`, `:240`, `:252`, `:255-256`.
