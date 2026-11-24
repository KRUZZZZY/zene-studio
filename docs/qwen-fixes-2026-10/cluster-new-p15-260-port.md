FIXED

# Cluster new-p15-260-port - stutter playback state shared across concurrent notes

## Verdict: REAL

The AudioFileProcessor keeps its stutter continuation (`m_nextPlayStartPoint` / `m_nextPlayBackwards`) on the
instrument, because the feature is defined as "continue sample playback across notes". Every note of that
instrument therefore shares it. Note jobs are not pinned to one thread:

- `src/core/AudioEngine.cpp:1115-1116` puts every play handle in the worker queue:
  `fillJobQueue(m_playHandles); startAndWaitForJobs();`.
- `include/PlayHandle.h:84-87` defaults `affinityMatters()` to false; `NotePlayHandle` does not override it, so
  two notes of one sampler can run `playNoteImpl()` on two workers at once.

On this port base the two fields are already `std::atomic` (`plugins/AudioFileProcessor/AudioFileProcessor.h:206-207`,
from `b4078a8627`), so the original plain-field data race was partly fixed before this job. The remaining defect
was the note-start read: `playNoteImpl()` tested `m_nextPlayStartPoint` against the sample end and then loaded it
again to initialise the voice. A concurrent note could publish a new position (including one at/after the end)
between those reads, so the value tested was not the value the note started from. That is a real race in the
feature's contract and is what this port fixes.

## Fix

- `plugins/AudioFileProcessor/AudioFileProcessor.h:147-171`: `StutterStart` and `stutterStartForNote()` read the
  frame and direction once, restart the continuation at the sample start when stutter is on and the frame is at
  the end, and return the exact pair the note must start from. The helper is inline, so the regression test can
  call the same code without linking the plugin module.
- `plugins/AudioFileProcessor/AudioFileProcessor.cpp:188-199`: the first period uses that one snapshot for both
  the end-of-sample decision and `state.setFrameIndex()` / `setBackwards()`.
- `plugins/AudioFileProcessor/AudioFileProcessor.cpp:75-78`: `static_assert` that both atomics are lock-free, so
  a platform build fails loudly instead of taking a lock on the render path.
- Every other access is an explicit relaxed load/store: the magic reset key (`:171-175`), the per-period publish
  (`:261-265`), `beatLen()` (`:430-433`), and the control-thread resets in `reverseModelChanged()` (`:497-502`),
  `stutterModelChanged()` (`:515-519`) and `pointChanged()` (`:590-591`).

## Test matrix

| Path | Coverage |
|---|---|
| Normal | `SamplerEnvelopeTest::stutterContinuesWhereThePreviousNoteStopped` renders a ramp sample, renders two periods of a note, then starts a second note and checks it begins at the ramp value where the first stopped. |
| Failure / reset | `SamplerEnvelopeTest::stutterStartIsOneCoherentSnapshot` injects a continuation at `endFrame` and checks the returned pair is `(startFrame, false)` and the published pair was reset; it also checks a mid-sample continuation is returned unchanged, and that stutter off neither inspects nor resets it. No thread, sleep or clock is used. |
| Exhaustion | No voice-pool limit or policy is touched. `SamplerVoiceReuseTest` remains green in the family run. |
| Reconfiguration | Both reset slots and the start/end-point slot use the same relaxed atomic stores; the save/load round trip and the old-project test remain green. |
| Audio thread | Only relaxed atomic loads/stores on the note-start path; the two `static_assert`s require lock-free atomics. No allocation, lock, wait, signal or log is added. |
| Platforms | Configured Linux/Qt 6 build run; the change is portable C++20 and uses no Qt-6-only API. The lock-free assertions are the platform guard (`f_cnt_t` is `std::uint64_t` in `include/LmmsTypes.h:43`). |

The regression target and its dependencies exist: `tests/CMakeLists.txt:1080` registers `SamplerEnvelopeTest`,
`:2995-2998` gives it the `audiofileprocessor` dependency, and the new include directory entry is at `:2999-3003`.

## Commands and evidence

All logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p15-260-port.r2/logs`.

| Run | Command | Log | EXIT |
|---|---|---|---:|
| Baseline build | `cmake --build build --target SamplerEnvelopeTest -j4` | `build-1-sampler-envelope.log` | 0 |
| Baseline test | `cd build/tests && ctest -R '^SamplerEnvelopeTest$' --output-on-failure` | `test-2-baseline-built.log` | 0 |
| RED test build (pre-fix product) | `cmake --build build --target SamplerEnvelopeTest -j4` | `build-3-red-test.log` | 0 |
| **RED** | `cd build/tests && ctest -R '^SamplerEnvelopeTest$' --output-on-failure` | `test-3-red.log` | **8** |
| GREEN build | `cmake --build build --target audiofileprocessor SamplerEnvelopeTest -j4` | `build-5-green.log` | 0 |
| GREEN | `cd build/tests && ctest -R '^SamplerEnvelopeTest$' --output-on-failure` | `test-4-green.log` | 0 |
| Save product diff | `git diff -- plugins/AudioFileProcessor/AudioFileProcessor.cpp plugins/AudioFileProcessor/AudioFileProcessor.h > <logs>/fix.patch` | `fix.patch` | 0 |
| Reverse product diff | `git apply -R <logs>/fix.patch` | - | 0 |
| Reversed build | `cmake --build build --target audiofileprocessor SamplerEnvelopeTest -j4` | `build-10-red-reversed.log` | 0 |
| **RED, final reverse** | `cd build/tests && ctest -R '^SamplerEnvelopeTest$' --output-on-failure` | `test-9-red-reversed.log` | **8** |
| Reapply product diff | `git apply <logs>/fix.patch` | - | 0 |
| Restored build | `cmake --build build --target audiofileprocessor SamplerEnvelopeTest -j4` | `build-11-restored.log` | 0 |
| **GREEN, restored** | `cd build/tests && ctest -R '^SamplerEnvelopeTest$' --output-on-failure` | `test-10-green-restored.log` | 0 |
| GREEN totals | `cd build/tests && ctest -R '^SamplerEnvelopeTest$' -V` | `test-12-green-verbose.log` | 0 (12 passed, 0 failed) |
| Family build | `cmake --build build --target SamplerTakeTest AudioFileProcessorWaveViewTest AudioFileProcessorPlayheadTest SamplerEnvelopeTest SamplerVoiceReuseTest SamplerTakeGuiTest -j4` | `build-8-family.log` | 0 |
| Family tests | `cd build/tests && ctest -R 'Sampler|AudioFileProcessor' --output-on-failure` | `test-11-family-final.log` | 0 (6/6) |
| Full application | `cmake --build build --target zene -j4` | `build-9-zene.log` | 0 |
| Gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | `feature-check-2.log` | 0 (all 8 rows PASS) |

The RED failure in both reversed runs is the same assertion (`test-9-red-reversed.log:14`):

```
FAIL!  : SamplerEnvelopeTest::stutterStartIsOneCoherentSnapshot() '(HasStutterStartSnapshot<AudioFileProcessor>::value)' returned FALSE. (the stutter continuation frame and direction must be read together for a note start)
```

In both RED runs `stutterContinuesWhereThePreviousNoteStopped` still passed, so the failure is the missing
coherent snapshot, not the stutter feature itself. The restored run has 12 passed, 0 failed.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it (exit codes).** Product files
  only (`.cpp` + `.h`) were captured in `fix.patch`, reversed with EXIT 0, rebuilt (`build-10-red-reversed.log`,
  EXIT 0), and `ctest -R '^SamplerEnvelopeTest$'` returned **EXIT 8** (`test-9-red-reversed.log`). Reapplying
  `fix.patch` returned EXIT 0, the rebuild returned EXIT 0 (`build-11-restored.log`), and the same ctest returned
  **EXIT 0** (`test-10-green-restored.log`; verbose totals `test-12-green-verbose.log`, 12 passed).
- **The test is deterministic: no wall-clock timing, sleeps or races (name the seam or injected values).** It
  injects an explicit continuation (`endFrame`, `true`) and a mid-sample continuation into the same members the
  render path uses, then calls the exact `stutterStartForNote()` function `playNoteImpl()` calls. The normal-path
  case reads a ramp sample back from rendered audio, also on the test thread. No thread, sleep, timer or clock is
  involved. The test uses SFINAE so it compiles against the pre-fix declaration and fails at runtime instead of
  failing to build.
- **The fix is at the cause and complete: every caller and sibling of what you changed was checked and stays
  consistent.** The module's only readers/writers of the continuation are the first-period read (now one call),
  the per-period publish, the magic reset note, `beatLen()`, and the three control-thread reset slots; all are
  listed above and use relaxed atomics. `tests/reference/AudioFileProcessor/` is a pristine reference copy and is
  not compiled. The header and definition changed together, and the plugin, `SamplerEnvelopeTest`, the whole
  sampler family and `zene` were rebuilt.
- **No collateral damage: old projects and saved data still load as before, and no limit or check hits other
  callers.** The two fields are runtime-only and are not saved or loaded; `oldProjectsLoadWithTheOldBehaviour`
  and `settingsRoundTripAndTheLoopControlsStayStable` pass. No voice limit, parameter index or command schema
  changed. The only new check is the compile-time lock-free requirement on the two atomics the render path
  already used.
- **Realtime, undo and commands.** The note-start path adds only relaxed atomic loads/stores (no allocation, lock,
  wait, signal or log); the `static_assert`s reject a non-lock-free implementation. No command or undo step was
  added, so `ControlCommandsSnapshot` is unchanged.
- **If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line).** Not claimed: the verdict is FIXED.
  The base had the atomics from `b4078a8627`, but the coherent note-start snapshot was missing.
- **Sibling commands in the command files you changed.** No command file, schema or handler changed. The command
  surfaces `app.about`, `app.ask`, `app.help`, `app.online_help`, `app.task_cancel`, `app.tasks`, `app.tour`,
  `app.tours`, `app.update_check_get`, `app.update_check_run`, `app.update_check_set`, `app.version`,
  `app.whats_new`, `arrangement.fit_tracks` and `arrangement.get_state` are therefore unaffected; the sampler's
  `plugin.param_*` indices are also unchanged (no model was added or reordered).
- **Registration.** The changed inherited files are recorded in `tests/upstream-modifications.txt`
  (`plugins/AudioFileProcessor/AudioFileProcessor.h`, `.cpp`, `tests/CMakeLists.txt`, all ending with a newline).
  `tests/src/core/SamplerEnvelopeTest.cpp` is already a fork source in `tests/fork-sources.txt`. `feature_check.py`
  passed all eight rows.

## Other findings

- The pre-existing `SamplerEnvelopeTest::windowControlsDriveTheModels` icon-pixmap warnings (`File not found`) and
  fontconfig warnings are test-environment noise, unchanged by this fix.
- `m_nextPlayBackwards` is always false in current code; it remains part of the continuation pair for behaviour
  compatibility and is now read atomically with the frame.
