# Cluster new-p12-278

**Verdict: FIXED**

The finding is real on this branch. `Vibed::playNoteImpl` handed `graphModel::samples()` straight to
`VibratingString`'s constructor, and `graphModel` is GUI-thread storage that every waveform action
(drag, smooth, normalize, the wave buttons, a project load) rewrites in place. A note starting while
such a write was in flight copied the model's storage on the audio thread without synchronisation.

- Vulnerable read (was `plugins/Vibed/Vibed.cpp:221`, now the `addString` argument at
  [Vibed.cpp:236](../../plugins/Vibed/Vibed.cpp:236)): `m_graphModels[i]->samples()`.
- Racy writers: `src/gui/widgets/Graph.cpp:516` `graphModel::setSampleAt`, `:524` `setSamples`,
  and every other mutator (`setWaveToSine`…`setWaveToNoise`, `smooth`, `normalize`, `shiftPhase`,
  `invert`, `clear`, `clearInvisible`, `convolve`) — each rewrites `m_samples` and then emits
  `samplesChanged`.
- Fix, declaration: [Vibed.h:76](../../plugins/Vibed/Vibed.h:76) `Vibed::WaveformSnapshot`,
  [Vibed.h:133](../../plugins/Vibed/Vibed.h:133) `publishWaveform`, [Vibed.h:134](../../plugins/Vibed/Vibed.h:134)
  `m_waveforms`.
- Fix, definition: [Vibed.cpp:130](../../plugins/Vibed/Vibed.cpp:130) one direct `samplesChanged`
  connection per string in the constructor (so the very first `setWaveToSine()` publishes too),
  [Vibed.cpp:136](../../plugins/Vibed/Vibed.cpp:136) `Vibed::publishWaveform`,
  [Vibed.cpp:236](../../plugins/Vibed/Vibed.cpp:236) the audio path now reads the published copy.

The handoff is three `float[128]` slots, one atomic index and the slot a reader may still be copying:
`publish()` fills the slot that is neither the published one nor the stale one and then flips the
index, so a reader always sees one waveform in full and the slot it is copying is not touched again
until the publication after next. One writer (the GUI thread, where every `samplesChanged` is
emitted), no lock, no allocation and no logging on either side; the audio path's cost is unchanged
(one atomic load, then the same copy it already made).

Regression: [tests/src/plugins/VibedWaveformHandoffTest.cpp](../../tests/src/plugins/VibedWaveformHandoffTest.cpp),
5 cases. It compiles the Vibed plug-in sources into the test (the SlicerT plug-in tests' shape) and
drives the real audio entry point `Instrument::playNote` with fresh note handles:

1. `oneWaveformRendersTheSameNoteEveryTime` — the premise: an unedited waveform renders a
   bit-identical block, so a block can be read as a waveform. (The test runs the device at twice the
   base sample rate so `VibratingString`'s oversample factor is 1 and a note's render is a pure
   function of its waveform.)
2. `everyWaveformActionReachesTheNextNote` — behaviour preservation: `normalize`,
   `setWaveToSquare`, `smooth` and `setWaveToNoise` each still change the *next* note. Passes before
   and after the fix.
3. `anUnpublishedWaveformEditNeverReachesANewNote` — the handoff contract: a waveform written into
   the model's storage without being published must not reach a later note. **Fails before the fix.**
4. `aWaveformEditedWhileNotesStartIsNeverReadHalfApplied` — the reported race through the real GUI
   API: a second thread drives `fillWith`/`setSampleAt` in the inward-from-both-ends order a
   back-and-forth drag produces, while notes keep starting; every block must equal one of the 130
   states that edit sequence passes through. **Fails before the fix** (a half-applied waveform was
   read on the very first read).
5. `initTestCase`/`cleanupTestCase` — engine, track, instrument and rate-rate setup.

The affected CTest family (`ctest -R '^Vibed'`) contains **1 CTest test** (test #4,
`VibedWaveformHandoffTest`, 6 cases including setup/teardown). It is the only Vibed test in this
build tree: no other test binary in `build/tests` is built here. 20 consecutive direct runs of the
final binary passed (log `repeat-2.log`).

## Evidence

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-278.r1/logs`. Build
commands ran from the worktree root; CTest commands ran from `build/tests`.

| Step | Exact command | EXIT | Result |
|---|---|---:|---|
| Test build (before fix) | `cmake --build build --target VibedWaveformHandoffTest > …/logs/build-5.log 2>&1; echo EXIT=$?` | 0 | Built the regression against the original product code. |
| RED before fix | `ctest -R '^VibedWaveformHandoffTest$' --output-on-failure > …/logs/test-1.log 2>&1; echo EXIT=$?` | 8 | 1/1 CTest test; `anUnpublishedWaveformEditNeverReachesANewNote` and `aWaveformEditedWhileNotesStartIsNeverReadHalfApplied` both FAIL. |
| Build with fix | `cmake --build build --target vibedstrings zene VibedWaveformHandoffTest > …/logs/build-9.log 2>&1; echo EXIT=$?` | 0 | Header changed, so the plug-in module and the app were rebuilt too. |
| GREEN | `ctest -R '^Vibed' --output-on-failure > …/logs/test-6.log 2>&1; echo EXIT=$?` | 0 | 1/1 CTest test passed (6 cases). |
| Save and remove the fix | `git diff -- plugins/Vibed/Vibed.cpp plugins/Vibed/Vibed.h > …/logs/fix.patch && git apply -R …/logs/fix.patch` | 0 | Only the production change reversed; test untouched. |
| RED without fix | `cmake --build build --target VibedWaveformHandoffTest > …/logs/build-11.log 2>&1; echo EXIT=$?` then `ctest -R '^Vibed' --output-on-failure > …/logs/test-8.log 2>&1; echo EXIT=$?` | 0 / **8** | 1/1 CTest test; the same two assertions FAIL (`4 passed, 2 failed`). |
| Restore the fix | `git apply …/logs/fix.patch` | 0 | Fix back in place. |
| Final build | `cmake --build build --target vibedstrings zene VibedWaveformHandoffTest > …/logs/build-12.log 2>&1; echo EXIT=$?` | 0 | App, plug-in module and test rebuilt. |
| Final GREEN | `ctest -R '^Vibed' --output-on-failure > …/logs/test-9.log 2>&1; echo EXIT=$?` | 0 | 1/1 CTest test passed. |
| Stability (20 runs) | `for i in $(seq 1 20); do ./VibedWaveformHandoffTest … done > …/logs/repeat-2.log 2>&1` | 0 | No flaky failure. |
| Complexity gate | `bash tests/complexity-gate.sh --check > …/logs/gate-1.log 2>&1; echo EXIT=$?` | **1** | Pre-existing: 6 regressions, none of them this change's files. New functions measured CCN ≤ 7. |
| File-length gate | `bash tests/file-length-gate.sh --check > …/logs/gate-2.log 2>&1; echo EXIT=$?` | **1** | Pre-existing: 14 regressions, none of them this change's files. |
| Gate 6 (upstream ledger) | `bash tests/no-upstream-regression-gate.sh > …/logs/gate-3.log 2>&1; echo EXIT=$?` | 0 | Both changed production files declared with reasons. |
| Fork-sources gate | `bash tests/fork-sources-gate.sh > …/logs/gate-4.log 2>&1; echo EXIT=$?` | 0 | New test file registered; 0 stale entries. |

Both ratchet gates are red **on this branch without this change**: measured by removing only the
`tests/fork-sources.txt` line for the new test (the two modified product files are inherited and are
not in the fork manifest), the regression lists are byte-identical to the run above —
`tests/src/gui/XyPadPairTest.cpp`, `src/core/DawProjectReadTracks.cpp`,
`src/core/ControlCommandsMixerTools.cpp`, `src/core/ControlCommandsMixerStrip.cpp`,
`src/core/ProjectRevisions.cpp`, `src/gui/ControlCommandsShortcuts.cpp` (complexity) and 14
over-long files including `tests/src/core/DeviceDspTest.cpp` and `src/core/TakeLane.cpp`
(file-length). No gate limit or baseline was touched, and no `Vibed` file appears in either
regression list.

## Files in this commit

- `plugins/Vibed/Vibed.h` — `WaveformSnapshot`, `m_waveforms`, `publishWaveform()` declaration.
- `plugins/Vibed/Vibed.cpp` — publish on `samplesChanged`; audio path reads the published copy.
- `tests/src/plugins/VibedWaveformHandoffTest.cpp` — new regression (fork-NEW).
- `tests/CMakeLists.txt` — builds the test and compiles the Vibed sources into it.
- `tests/fork-sources.txt` — registers the new test.
- `tests/upstream-modifications.txt` — declares the three changed inherited files with reasons.
- `docs/qwen-fixes-2026-10/cluster-new-p12-278.md` — this report.