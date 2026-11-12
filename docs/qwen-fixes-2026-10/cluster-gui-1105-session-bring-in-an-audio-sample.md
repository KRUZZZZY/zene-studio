FALSE-POSITIVE

gui-1105-session-bring-in-an-audio-sample: FALSE-POSITIVE

Evidence: the sweep used File > Import for a WAV. That menu entry is the MIDI / Hydrogen importer by design, so its refusal is the documented behaviour, not an audio-import defect.
- `MainWindow::onImportProject` offers only "MIDI sequences (*.mid *.midi *.rmi)", "Hydrogen projects (*.h2song)" and "All file types" at [MainWindow.cpp:2083-2090](src/gui/MainWindow.cpp) and dispatches `project.import` at [MainWindow.cpp:2096-2098](src/gui/MainWindow.cpp).
- `project.import` is documented as the import-filter route ("a Standard MIDI File's notes, a Hydrogen song") and refuses anything else with "MIDI sequences and Hydrogen songs are the formats it reads" at [ControlCommandsProjectLifecycle.cpp:386-412](src/core/ControlCommandsProjectLifecycle.cpp). The sweep saw exactly that message.
- Audio goes into a project through other routes, none of which the recorded steps used: a file drop on the arranger or a sample track runs `clip.import_file` ([ArrangerFileDrop.cpp:120](src/gui/ArrangerFileDrop.cpp); manual: data/manual/index.html:169, "Drop a MIDI file or an audio file on the arranger"), and double-clicking a sample clip opens the "Open audio file" dialog ([SampleClipView.cpp:521-537](src/gui/clips/SampleClipView.cpp)). The repro text confirms the WAV was never dropped on the arranger or chosen in that dialog; the "no waveform" observation was of an empty sample clip.
- The audio-import path works. `ClipImportFileTest::anAudioFileBecomesASampleClipAtTheTickOnASampleTrack` ([ClipImportFileTest.cpp:354-372](tests/src/core/ClipImportFileTest.cpp)) imports a WAV through `clip.import_file` into a `SampleClip` with a non-zero length and one-step undo. It passes (below).
- Luna's own doubt matches this. The expected-use line lists "Browser, drag and drop, or a menu import" as alternatives; this build has no audio menu import, and adding one would be a new feature and a change to the documented File > Import scope, not a bug fix. I did not change it. If the owner wants File > Import to accept audio, that is a separate feature decision. No production file changed and no regression test was added, because there is no defect to fail before the fix.

| Evidence | Command | Result |
| --- | --- | --- |
| Source audit of the import route and message | `rg -n 'project.import\|ImportFilter\|MIDI sequences\|Hydrogen' src/gui/MainWindow.cpp src/core/ControlCommandsProjectLifecycle.cpp data/manual/index.html` | EXIT=0; `logs/001-audit.log` |
| Build of the existing audio-import test, first try | `cmake --build build --target ClipImportFileTest -j4 > logs/002-build.log 2>&1` | EXIT=2: `ccache: Permission denied` creating a temp file under `~/.local/share/zene-bunny-fix/dsh/home/ccache-tmp-r6` (sandbox write limit, not a compile error); `ctest` then ran 0 tests (`logs/003-ctest.log`, "Not Run") |
| Same build with ccache off | `CCACHE_DISABLE=1 cmake --build build --target ClipImportFileTest -j4 > logs/004-build-noccache.log 2>&1` | EXIT=0 |
| Existing audio-import test (run from `build/tests`) | `ctest -R '^ClipImportFileTest$' --output-on-failure > logs/005-ctest.log 2>&1` | EXIT=0; 1 test run, 1 passed |

There is no RED/GREEN or step 4b pair: the verdict is not-real and no product file was edited.

Files committed: this report only (`docs/qwen-fixes-2026-10/cluster-gui-1105-session-bring-in-an-audio-sample.md`).

## Self-check

- Regression test fails without / passes with the change: not applicable. The verdict is FALSE-POSITIVE, no product file changed, so there is no fix to revert and no RED/GREEN pair.
- Determinism: no new test was added. The cited existing test `ClipImportFileTest::anAudioFileBecomesASampleClipAtTheTickOnASampleTrack` writes a generated 1-second WAV into a temp dir and calls `clip.import_file` directly, with no timing, sleeps or races.
- Fix at the cause / callers and siblings: nothing was changed. The siblings were read: `MainWindow::onImportProject` -> `project.import` (MIDI/Hydrogen filters only) versus `ArrangerFileDrop::importPath` -> `clip.import_file` (audio and MIDI) and `SampleClipView::mouseDoubleClickEvent` (audio dialog). Each does what its documentation says.
- Collateral damage: none. No code, saved data or command was touched, so the `ControlCommandsSnapshot` canary does not move.
- Not-real evidence: the code (file:line) and the manual contract are in the Evidence paragraph above; the passing audio-import test is the last row of the table.
- Gates: `feature_check.py` could not run here. It tries to create `zene-qwen-gui-1105-session-bring-in-an-audio-sample-feature-check` next to the worktree and gets `PermissionError` (EXIT=1, `logs/006-feature-check.log`), which is outside this lane's write limit. I did not work around it, and no gate result is claimed. The change is one markdown file, so it touches no source or gate input.
