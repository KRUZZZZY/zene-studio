FIXED

# new-p17-91: missing Mallets rawwave files produce silent renders

## Verdict and evidence
REAL. `plugins/Stk/Mallets/Mallets.cpp` latches `m_filesMissing` in the constructor (line ~237) and `playNoteImpl()` returns silently when it is set (line ~453). The only warning was the `QMessageBox` in `MalletsInstrumentView` (line ~672, also gated on `getGUI() != nullptr`), so a headless render of a project had no diagnostic.

Fix: `MalletsInstrument::loadSettings()` now calls `Plugin::collectErrorForUI()` (-> `Song::collectError`) when `m_filesMissing`. That is the existing project-load diagnostic channel (same as Sf2Player.cpp:561/600/607 for a missing soundfont): `Song.cpp:2319-2333` prints the error summary to stderr for headless/unattended runs and `project.open` returns the list. Nothing was added to `playNoteImpl` (audio thread: no allocation/lock/log), no control command was added, and the render result is unchanged.

Regression test: `MalletsInstrumentViewTest::missingRawwavesAreReportedWhenAProjectLoadsTheInstrument` (tests/src/plugins/MalletsInstrumentViewTest.cpp). Normal path: instrument with rawwaves present reports nothing. Failure path: STK dir pointed at a non-existent directory, the instrument is built and loaded, the error summary must name Mallets and the directory. Deterministic (no timing).

## Commands (copied from my logs in /home/kruzzzzy/.config/kads/zene/state/runs/new-p17-91.r1/logs)
| Step | Command | Result |
| --- | --- | --- |
| RED build | `cmake --build build --target MalletsInstrumentViewTest -j4 > logs/build-1.log` | EXIT=0 |
| RED run (no fix) | `ctest -R MalletsInstrumentViewTest --output-on-failure > logs/test-1-red.log` (build/tests) | EXIT=8, new test FAIL: "a Mallets instrument with no STK rawwaves rendered silence and reported nothing" |
| GREEN | build-2.log, `ctest -R MalletsInstrumentViewTest > logs/test-2-green.log` | build EXIT=0, ctest EXIT=0 |
| 4b revert | `git diff -- plugins/Stk/Mallets/Mallets.cpp > logs/fix.patch && git apply -R logs/fix.patch`; rebuild (build-3-nofix.log); ctest (test-3-nofix.log) | REVERT=0, build EXIT=0, RED_EXIT=8 |
| 4b reapply | `git apply logs/fix.patch`; rebuild (build-4-fix.log); ctest (test-4-fix.log) | REAPPLY=0, build EXIT=0, GREEN_EXIT=0 |
| Final | `cmake --build build --target zene MalletsInstrumentViewTest PluginPortsMigrationTest -j4 > logs/build-8.log`; ctest (test-8-green.log) | build EXIT=0, ctest EXIT=0 |
| Gates | `python3 .../feature_check.py <worktree> > logs/feature-check.log` | EXIT=0, RESULT: PASS (all 8 rows) |

## Registration
`tests/upstream-modifications.txt` gets a new-p17-91 line for `plugins/Stk/Mallets/Mallets.cpp`. The test file was already registered; no new product file.

## Family check
`ctest -R "Mallets|PluginPortsMigration"` (logs/test-5-family.log): MalletsInstrumentViewTest passes; PluginPortsMigrationTest has 4 FAILs (malletsVoiceLimit*, slice8PluginsAreLiveAndExact, nonRealtimeLadspaEffectsAreInvalid). These are NOT caused by this change: the identical 4 FAILs occur with my fix reverted (logs/test-6-ppm-nofix.log, 4 each), and the log shows `FileRead::open: could not open ... thirdparty/stk-rawwaves/...` - that directory does not exist in this worktree.

## Other findings
- PluginPortsMigrationTest's Mallets cases fail on this box because the configured STK rawwave root (`thirdparty/stk-rawwaves`) is absent (/usr/share/stk/rawwaves exists); pre-existing, unrelated.
- The diagnostic fires on project load only. An instrument created and driven purely through the control API, never loaded from a file, still renders silently; reporting from the constructor would leave stale errors in the Song's list, so I left that alone.
- Unverified: I did not run a full `lmms render` on a real project without rawwaves; the evidence is the unit test plus the load path in Song.cpp:2319-2333.

## Self-check
(filled in after the completion check)
