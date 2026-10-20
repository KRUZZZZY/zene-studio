# cluster new-p15-83: the instrument track's render path reads GUI state

Batch: `new-p15-83` (this file), `new-p16-89` (clip collection allocates on the audio thread),
`3192` (MPE expression updates race with note rendering) - all three in
`src/tracks/InstrumentTrack.cpp`.

## Verdict: FIXED

new-p15-83: FIXED

(Batch: this file carries one line per bug of the three-bug batch in
`src/tracks/InstrumentTrack.cpp`. The lines for `new-p16-89` and `3192` are added by their own
commits, each with its own section, evidence and regression below.)

## new-p15-83: the render path reads GUI state - FIXED

### Root cause

`src/tracks/InstrumentTrack.cpp:945` (before the fix):

```cpp
	processHandle->processTimePos(
		_start, m_pitchModel.value(), gui::getGUI() && gui::getGUI()->pianoRoll()->isRecording());
```

`InstrumentTrack::play()` runs on the AUDIO THREAD (`Song::processNextBuffer` ->
`AudioEngine::renderStageNoteSetup`), and it asked the GUI for the piano roll's recording state:

* `gui::getGUI()->pianoRoll()` is a `QWidget` the GUI thread owns; `PianoRoll::m_recording` is a
  plain `bool` (`include/PianoRoll.h:406`) the GUI thread writes - a data race with the audio
  thread's read (undefined behaviour), and a Qt call from the wrong thread;
* with no GUI at all (a headless renderer, the agent surface, a test binary) the state is simply
  not there, so the detuning of a live MIDI note was never recorded;
* `GuiApplication::m_pianoRoll` is set to `nullptr` when the piano roll window is destroyed
  (`src/gui/GuiApplication.cpp:304-306`), so the old expression could dereference a null window
  while the transport ran.

### Fix

* `include/InstrumentTrack.h`: the recording state is PUBLISHED for the audio thread -
  `setPianoRollRecording()` / `isPianoRollRecording()`, one relaxed `std::atomic_bool`
  (`s_pianoRollRecording`).
* `src/tracks/InstrumentTrack.cpp:939-947`: `play()` reads the published flag; the two GUI
  includes the read needed (`GuiApplication.h`, `PianoRoll.h`) are gone - the render path of a
  core class no longer pulls a GUI header in.
* `src/gui/editors/PianoRoll.cpp`: the piano roll publishes its state where it starts and stops
  recording - `record()` (:4411), `recordAccompany()` (:4438), `stop()` (:4494).
* `include/PianoRoll.h`: `stopRecording()` (:137) publishes the end of the recording as well, so
  the published state can never outlive the pass it belongs to.

Every place that changes `PianoRoll::m_recording` was checked: `record()`,
`recordAccompany()`, `stop()` and `stopRecording()` (the fourth is the only other writer; the
constructor's initial `false` needs no publication because the flag starts `false`).

### Regression

`tests/src/tracks/InstrumentTrackRenderPathTest.cpp` -
`theRenderPathTakesTheRecordingStateFromThePublishedFlag()`:

* a live MIDI-input note (origin `MidiInput`) with a detuning curve on a real track with a real
  instrument (the `DummyInstrument` the missing-plugin path leaves behind);
* with the published flag SET, one `InstrumentTrack::play()` pass records the note's detuning
  (the track's pitch, 50, stored as 0.5) - the assertion reads the automation curve back;
* the negative control: with the flag CLEARED, the same pass records nothing.

No wall-clock timing, no sleeps, no racing threads: the values are injected (the published flag,
the track's pitch) and read back.

### Evidence table

| Step | Command | Exit | Notes |
|------|---------|------|-------|
| Build (product) | `cmake --build build --target zene` | 0 | `build-1.log`, `build-2-reconfigure.log` |
| Build (test) | `cmake --build build --target InstrumentTrackRenderPathTest` | 0 | `build-test3.log` |
| GREEN | `ctest -R '^InstrumentTrackRenderPathTest$' --output-on-failure` (build/tests) | 0 | `test-2-green.log` - 1/1 passed |
| RED (fix reverted) | `git diff -- src/tracks/InstrumentTrack.cpp > fix-bug1.patch && git apply -R fix-bug1.patch`, rebuild, `ctest -R '^InstrumentTrackRenderPathTest$' --output-on-failure` | 8 | `test-3-red.log` - `FAIL! ... the render path did not record the live note's detuning while the piano roll recorded` |
| GREEN (fix re-applied) | `git apply fix-bug1.patch`, rebuild, `ctest -R '^InstrumentTrackRenderPathTest$' --output-on-failure` | 0 | `test-4-refix.log` - 1/1 passed |
| Family | `ctest -R 'InstrumentTrackRenderPathTest\|HeadlessGuiGuardTest\|PatmanLoadTest\|MpePlaybackTest\|MpeInputPathTest' --output-on-failure` | 0 | `test-5-family.log` - 5/5 passed |
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 | `gate-1.log` - 8/8 PASS |

The RED patch is the file that carries the BEHAVIOUR (`src/tracks/InstrumentTrack.cpp`). The
published-state API lives in `include/InstrumentTrack.h` and stays in place for the revert, so the
regression test still builds and fails on the reverted behaviour - reverting the header as well
would leave the test without the API it drives, and no test could be run at all.

### Files committed (new-p15-83)

- `include/InstrumentTrack.h` - the published recording state (the API and the atomic word).
- `src/tracks/InstrumentTrack.cpp` - the render path reads the published state; the GUI includes
  are gone.
- `src/gui/editors/PianoRoll.cpp` - the piano roll publishes its recording state.
- `include/PianoRoll.h` - `stopRecording()` publishes the end of the recording.
- `tests/src/tracks/InstrumentTrackRenderPathTest.cpp` - the new regression (fork-new source).
- `tests/CMakeLists.txt` - registers the new test.
- `tests/fork-sources.txt`, `tests/all-sources.txt` - register the new test source.
- `tests/upstream-modifications.txt` - the divergence ledger for the changed inherited files.
- `docs/qwen-fixes-2026-10/cluster-new-p15-83.md` - this report.
