FIXED

# cluster new-p9-249: LFO state races with audio-thread buffer generation

new-p9-249: FIXED

## Decision: REAL

- `src/core/Controller.cpp:167-177` `Controller::prepareValueBufferForRender()` calls
  `updateValueBuffer()` from the render prepass (`tests/rt-safety-scope.txt:49` lists
  `LfoController::updateValueBuffer` as a serial controller prepass update).
- Before the fix, `LfoController::updateValueBuffer()` read plain `float m_duration` and the plain
  function pointer `m_sampleFunction`. It also read and wrote plain `float m_heldSample`.
- `src/core/LfoController.cpp:66-72`: the speed, multiplier and waveform models connect to
  `updateDuration()` and `updateSampleFunction()` with `Qt::DirectConnection`. Those slots run on the
  thread that edits the model (the UI), and they wrote the same plain fields. That is a data race (UB).
- A second effect of the same waveform race: the wave model's value is published before its
  `dataChanged` slot updates `m_sampleFunction`. A render period that runs in between, after a change
  from UserDefined (function `nullptr`) to WhiteNoise, called `m_sampleFunction(phase)` through a
  null pointer. The regression test reproduces this deterministically as a SIGSEGV at address 0x0.

## Fix (at the cause)

- `include/LfoController.h:85-96`: `m_duration` and `m_heldSample` become `std::atomic<float>`.
  `m_sampleFunction` becomes `std::atomic<SampleFunction>`.
- `src/core/LfoController.cpp:42-45`: static_asserts that both atomics are always lock-free, so the
  render thread never locks.
- `src/core/LfoController.cpp:104-105`: `updateValueBuffer()` takes one acquire snapshot of the
  duration and the sample function per period. It uses those snapshots everywhere in the loop
  (lines 112, 136-153, 161).
- `src/core/LfoController.cpp:136`: the white-noise branch skips a null function, the same guard the
  default branch already had.
- `src/core/LfoController.cpp:244`: `updateDuration()` publishes with a release store.
- `src/core/LfoController.cpp:272-273`: on WhiteNoise, the slot seeds the held sample before it
  publishes the noise function.
- `src/core/LfoController.cpp:221`: `updatePhase()` loads the duration atomically.
- Every reader and writer of the three fields is in `LfoController.cpp`. `rg` finds no other user in
  src, include or plugins. Saved projects are unchanged: save and load serialize the models, not
  these fields.
- Realtime: the render-side change adds only lock-free atomic loads and stores. Nothing allocates,
  locks or logs. There are no new control commands and undo is not touched.
- Concurrency reasoning: TSan does not run on this box. The argument is by design: every
  cross-thread access to these three fields is now an atomic operation, so the access is race-free.
  The release/acquire pairing makes a seeded held sample visible before the noise function is
  observed.

## Regression tests (tests/src/core/LfoControllerUserWaveTest.cpp)

- `parameterStateSharedWithRenderIsAtomic`: checks that the three fields shared between the UI and
  render threads are atomics, using a requires-expression in a generic lambda that compiles both
  ways. It also checks that the published duration is still `rate * speed` scaled by the multiplier
  (/100 or *100).
- `renderBetweenWaveChangeAndSlotDoesNotCallNullFunction`: sets UserDefined, then sets WhiteNoise
  under a `QSignalBlocker` to model render running before the slot. It advances the period and
  checks that every value in the buffer is finite and in [0, 1]. No timing or threads are involved.

## Commands and EXIT codes (logs in /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-249.r1/logs)

| step | command | EXIT |
|---|---|---|
| RED build | `cmake --build build --target LfoControllerUserWaveTest -j4 > logs/02-build-red.log 2>&1` | 0 |
| RED run | `ctest -R '^LfoControllerUserWaveTest$' --output-on-failure > logs/03-test-red.log 2>&1` (build/tests) | 8 (FAIL `durationIsAtomic`; SegFault in the null-function test) |
| GREEN build | `cmake --build build --target zene LfoControllerUserWaveTest MidiControllerMenuTest RackDeferredLifetimeTest UndoHonestyTest UndoHonestyStateTest AutomationPrepassProofTest PhaseDSidechainTest -j4 > logs/04-build-green.log 2>&1` | 0 |
| GREEN run | `ctest -R '^LfoControllerUserWaveTest$' --output-on-failure > logs/05-test-green.log 2>&1` | 0 |
| family (every test including LfoController.h) | `ctest -R '^(MidiControllerMenuTest\|RackDeferredLifetimeTest\|UndoHonestyTest\|UndoHonestyStateTest\|AutomationPrepassProofTest\|PhaseDSidechainTest\|LfoControllerUserWaveTest)$' --output-on-failure > logs/06-test-family.log 2>&1` | 8: 6/7 pass; RackDeferredLifetimeTest fails only its two effect cases (`"amplifier" wasn't found`). The failure predates this change; see below |
| 4b revert | `git diff -- include/LfoController.h src/core/LfoController.cpp > logs/fix.patch && git apply -R logs/fix.patch` | 0 |
| 4b revert build | `cmake --build build --target zene LfoControllerUserWaveTest RackDeferredLifetimeTest -j4 > logs/07-build-revert.log 2>&1` | 0 |
| 4b revert run | `ctest -R '^LfoControllerUserWaveTest$' --output-on-failure > logs/08-test-revert.log 2>&1` | 8 (FAIL + SegFault) |
| baseline rack | `ctest -R '^RackDeferredLifetimeTest$' --output-on-failure > logs/09-test-rack-revert.log 2>&1` (without the fix) | 8 (the same two effect cases fail: pre-existing) |
| 4b reapply | `git apply logs/fix.patch` | 0 |
| 4b reapply build | `cmake --build build --target zene LfoControllerUserWaveTest -j4 > logs/10-build-reapply.log 2>&1` | 0 |
| 4b reapply run | `ctest -R '^LfoControllerUserWaveTest$' --output-on-failure > logs/11-test-reapply.log 2>&1` | 0 |
| gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/12-feature-check.log 2>&1` | 0 (RESULT: PASS, all 8 rows PASS) |

Registration: `tests/upstream-modifications.txt` has new entries for `include/LfoController.h` and
`src/core/LfoController.cpp`. The test file is already listed in `tests/fork-sources.txt:3525`.

## Other findings

- `LfoController::updatePhase()` (`src/core/LfoController.cpp:219-223`) is connected without
  `Qt::DirectConnection` to `Song::playbackStateChanged` and `playbackPositionJumped`. It writes plain
  `m_currentPhase` and `Controller::m_bufferLastUpdated` on the main thread, while
  `updateValueBuffer()` reads and writes both on the render thread. This is the same class of race,
  but on a transport trigger, and fixing it needs a phase-reset handoff design (a pending flag that
  render consumes). It was not changed here.
- RackDeferredLifetimeTest's two effect cases fail with or without this change because the
  "amplifier" plugin cannot be found in this build tree (`logs/09-test-rack-revert.log`).

## Self-check

- **The test fails without the fix and passes with it:** yes. With `fix.patch` reverted
  (`git apply -R`, EXIT=0), the rebuild exited 0 and `ctest -R '^LfoControllerUserWaveTest$'`
  exited 8: `parameterStateSharedWithRenderIsAtomic` failed and the null-function test segfaulted
  (`logs/08-test-revert.log`). With it reapplied (`git apply`, EXIT=0), the rebuild exited 0 and the
  test exited 0 (`logs/11-test-reapply.log`).
- **Deterministic:** yes. No threads, sleeps or clocks are involved. The structural test checks
  types with a requires-expression. The interleave test uses `QSignalBlocker` on `m_waveModel` as the
  seam that models render running between the model publish and `updateSampleFunction()`. It then
  calls `Controller::triggerFrameCounter()` and `valueBuffer()` on the test thread. The injected
  values are speed 0.01 and multiplier 1 (/100), so the noise phase wraps inside one buffer.
- **At the cause and complete:** every read and write of `m_duration`, `m_sampleFunction` and
  `m_heldSample` is in `src/core/LfoController.cpp`. `rg` finds no other user in src, include or
  plugins, and every access is now atomic: render snapshot at lines 104-105 and 136-153,
  `updatePhase` at 221, `updateDuration` at 244, `updateSampleFunction` at 253-276, and the
  constructor at 57-61. I rebuilt every target that includes `LfoController.h`: zene, the
  ControllerRackView and LfoControllerDialog GUI sources inside zene, and the 7 tests in
  `logs/04-build-green.log`, all with EXIT=0. The tests passed except for the pre-existing amplifier
  cases (`logs/06-test-family.log`, `logs/09-test-rack-revert.log`). The sibling `updatePhase()`
  race on `m_currentPhase` is listed under Other findings.
- **No collateral damage:** the save and load paths (`saveSettings`/`loadSettings`) serialize the
  models only and are unchanged. Values are unchanged too: duration is still rate * speed with the
  /100 or *100 multiplier (asserted in the test), and the noise held sample is still seeded when the
  waveform switches. I added no limit or check, apart from a null guard on a branch that previously
  crashed. UndoHonestyTest, including `whiteNoiseStartsWithAValidHeldSample`, passes
  (`logs/06-test-family.log`).
- **ALREADY-FIXED / FALSE-POSITIVE evidence:** not applicable. The verdict is FIXED.
- **Sibling commands (app.*, arrangement.*):** not affected. The checker lists them from the
  branch's whole diff against its base. My commits (`git diff HEAD~2 --stat`) touch only
  `include/LfoController.h`, `src/core/LfoController.cpp`,
  `tests/src/core/LfoControllerUserWaveTest.cpp`, `tests/upstream-modifications.txt` and this
  report. No command file or control command was changed.
