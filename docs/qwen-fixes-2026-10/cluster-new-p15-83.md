FIXED

# cluster new-p15-83: the instrument track's render path reads GUI state

Batch: `new-p15-83` (this file), `new-p16-89` (clip collection allocates on the audio thread),
`3192` (MPE expression updates race with note rendering) - all three in
`src/tracks/InstrumentTrack.cpp`.

## new-p15-83: the render path reads GUI state - FIXED

### Finding and fix

Before the fix, `InstrumentTrack::play()` called `gui::getGUI()->pianoRoll()->isRecording()`
while processing each note handle (`src/tracks/InstrumentTrack.cpp:945` in the parent commit).
`play()` runs on the audio thread. `PianoRoll::m_recording` is a plain GUI-owned `bool`
(`include/PianoRoll.h:410`), so this was both a cross-thread Qt access and a data race. The old
expression also assumed the piano-roll window still existed.

The render loop now reads `InstrumentTrack::isPianoRollRecording()` at
`src/tracks/InstrumentTrack.cpp:945`. The getter loads one relaxed `std::atomic_bool`, declared
and initialized false at `include/InstrumentTrack.h:371-378,487`. The GUI publishes true in both
recording entry paths, `PianoRoll::record()` and `recordAccompany()` (`src/gui/editors/PianoRoll.cpp:4411,4438`),
and false in `PianoRoll::stop()` and `PianoRoll::stopRecording()` (`src/gui/editors/PianoRoll.cpp:4494`,
`include/PianoRoll.h:132-138`). The two GUI headers formerly needed by the render path were removed.

### Regression

`tests/src/tracks/InstrumentTrackRenderPathTest.cpp:79-126`,
`theRenderPathTakesTheRecordingStateFromThePublishedFlag()`, creates a live MIDI-input handle,
sets the track pitch to 50, injects recording-on, and verifies the detuning curve stores 0.5. Its
negative control injects recording-off and verifies no detuning is recorded. The test uses explicit
`TimePos` values and the published-state setter; it has no sleeps, wall-clock checks, or racing
threads.

## Self-check

- **Does the regression fail with the product change reverted and pass with it?** Yes. I reverted
the render-path consumer in `src/tracks/InstrumentTrack.cpp` while retaining the atomic setter as
the test's injection seam. The reverted build returned 0; the test returned 8 and failed the
recorded-detuning assertion. After reapplying the product patch, the build returned 0 and the same
test returned 0 (1/1 passed). Logs: `build-3-reverted.log`, `test-2-red.log`, `build-4-restored.log`,
`test-3-green.log` in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-83.r2/logs`.
- **Is the test deterministic?** Yes. The seam is `InstrumentTrack::setPianoRollRecording(true/false)`;
the pitch is set to 50 and the note/automation positions use explicit `TimePos` values. Both passes
run synchronously in the test thread; there is no timing or scheduling dependency.
- **Is the fix at the cause and complete?** Yes. `InstrumentTrack::play()` is the only caller of
`NotePlayHandle::processTimePos()` (`src/tracks/InstrumentTrack.cpp:945`; definition
`src/core/NotePlayHandle.cpp:701`). I checked every write to `PianoRoll::m_recording`: initialization
is false (`src/gui/editors/PianoRoll.cpp:173`), starts are `record()`/`recordAccompany()` at
`4408,4435`, and stops are `stop()` at `4491` and `stopRecording()` at `include/PianoRoll.h:134`.
Each runtime transition publishes the matching value. The static published flag also starts false.
- **Is there collateral damage to old projects or other callers?** No. This adds no limit or range
check. The new state is transient and is not serialized; project loading, saving, and saved data are
unchanged. The only render caller now consumes the same recording decision through an atomic load.
The audio-thread change adds no allocation, free, lock, wait, or log.
- **Does ALREADY-FIXED or FALSE-POSITIVE evidence apply?** Not applicable: the verdict is FIXED.
The parent commit contains the unsafe GUI query at `src/tracks/InstrumentTrack.cpp:945`, while the
current line reads the published flag; `PianoRoll::m_recording` is a plain `bool` at
`include/PianoRoll.h:410`.

### Evidence table

All ctest commands below ran from `build/tests`. `EXIT` is the command's reported exit code.

| Step | Exact command | Exit | Evidence |
|------|---------------|------|----------|
| Make revert patch | `git diff HEAD^ HEAD -- src/tracks/InstrumentTrack.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-83.r2/logs/fix.patch` | 0 | Consumer-only behavioral revert patch |
| Revert product change | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-83.r2/logs/fix.patch` | 0 | Test seam retained |
| Build reverted code | `cmake --build build --target zene InstrumentTrackRenderPathTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-83.r2/logs/build-3-reverted.log 2>&1; echo EXIT=$?` | 0 | Rebuilt `zene` and test before red run |
| RED | `ctest -R '^InstrumentTrackRenderPathTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-83.r2/logs/test-2-red.log 2>&1; echo EXIT=$?` | 8 | `the render path did not record the live note's detuning`; 1/1 failed |
| Reapply product change | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-83.r2/logs/fix.patch` | 0 | Restored committed implementation |
| Build restored code | `cmake --build build --target zene InstrumentTrackRenderPathTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-83.r2/logs/build-4-restored.log 2>&1; echo EXIT=$?` | 0 | Rebuilt `zene` and test before green run |
| GREEN | `ctest -R '^InstrumentTrackRenderPathTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-83.r2/logs/test-3-green.log 2>&1; echo EXIT=$?` | 0 | 1/1 passed |
| Affected family | `ctest -R 'InstrumentTrackRenderPathTest|HeadlessGuiGuardTest|PatmanLoadTest|MpePlaybackTest|MpeInputPathTest' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-83.r2/logs/test-4-family.log 2>&1; echo EXIT=$?` | 0 | 5/5 passed |
| Feature gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p15-83 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-83.r2/logs/gate-2.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-83.r2/logs/gate-2.log; echo EXIT=$rc; exit $rc` | 0 | `gate-2.log`: 8/8 PASS; exit saved in log |
| Completion check | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p15-83 new-p15-83` | after commit | The completion check is run after this report is committed |

### Files committed (new-p15-83)

- `include/InstrumentTrack.h` - published atomic recording state and accessor.
- `src/tracks/InstrumentTrack.cpp` - render path reads the published state; GUI includes removed.
- `src/gui/editors/PianoRoll.cpp` and `include/PianoRoll.h` - publish recording transitions.
- `tests/src/tracks/InstrumentTrackRenderPathTest.cpp` - deterministic regression.
- `tests/CMakeLists.txt`, `tests/fork-sources.txt`, `tests/all-sources.txt` - register the test.
- `tests/upstream-modifications.txt` - records inherited source changes.
- `docs/qwen-fixes-2026-10/cluster-new-p15-83.md` - this report.
