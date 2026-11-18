# M0 blocker fixes - batch C (automation value reads), 2026-10-08

Branch `bugs/m0-blk-automation`, starting at train 2's tip `8eacc41d2` (the brief said `28cc3a0b5`; the
task itself gave `8eacc41d2`). Worktree `zene-bunny-theory`.

## Rows

| Cluster | Verdict | Fix commit | Test | Red exit | Green exit | Gate exits |
| --- | --- | --- | --- | --- | --- | --- |
| 2647 (AutomatableModel::valueBuffer mutex on the audio thread) | BLOCKED for the general lock; the sidechain-amount read under it is FIXED in c674877bc | c674877bc (sidechain subset only) | PhaseDSidechainTest::theSidechainAmountReadTakesNoLock | 1 | 0 | see below |
| review-f1a74cc54 (sidechain amount read takes the mutex) | FIXED for an unlinked amount; a linked amount still takes the linked model's mutex (BLOCKED-ON-2647 for that branch) | c674877bc | theSidechainAmountReadTakesNoLock; theSidechainAmountIsUnchangedForEverySource | 1 | 0 | see below |
| 5528 (AutomationClip::writeBlockRamp / valueAt take m_clipMutex) | BLOCKED (reason below); no code change | - | - | - | - | - |

Gate exits (final tree, `--check` where the gate has it): complexity-gate 0, file-length-gate 0,
fork-sources-gate 0, no-upstream-regression-gate 0, rt-safety-sweep 0 (ctest `RtSafetySweep` 0,
`RtSafetySelfTest` 0).

## Evidence

- Red: with `Mixer.cpp` reverted to `route->amount()->valueBuffer()` (only that line), the probe
  `theSidechainAmountReadTakesNoLock` fails: `'completed' returned FALSE ... waited on the amount's
  valueBuffer() mutex`, process exit 1. Restored and verified byte-for-byte with `cmp`; rebuilt; green exit 0.
- Behaviour: `theSidechainAmountIsUnchangedForEverySource` checks the summed sidechain buffer against an
  identically configured model's `valueBuffer()` frame by frame (bit-exact) for a published ramp, an LFO
  controller and a manual change. The `PARTD_SIDECHAIN_AMOUNT` hash lines are identical between the baseline
  binary and the fixed binary (`fnv1a` 323a07fa3203dfb1 / 37f34ff56c876b74 / 7fad6c72da607dde).
- Full `PhaseDSidechainTest`: 14 of 14 pass (ctest `PhaseDSidechainTest` passes).
- Suites run (ctest, from `build/tests`): AutomatableModelTest, AutomationModesTest,
  SampleAccurateAutomationTest, ControlAutomationModesTest, MixerAbRegressionTest, MixerConcurrencyTest,
  MixerRoutingBackwardCompatTest, ModulationLayerTest, ModulationLayerValueTest, ModulationRateTest,
  PdcMixerTest, MixerChannelPanTest, MixerStripTest, SidechainInputsTest, AutomationLanesTest,
  AutomationWriteToEndTest, MixerListenTest, VcaGroupTest, MixerSendsOnFadersTest, PhaseDSidechainTest:
  20 of 20 pass.
  Three of them (ControlAutomationModesTest, AutomationLanesTest, AutomationWriteToEndTest) first failed with
  "this song exposes no automatable device parameter ... the binary needs the instrument modules". The
  `tripleoscillator` module had never been built in this tree (`build/plugins/TripleOscillator` held only
  CMake files). Building that target made all three pass with the fix in place. This is an environment
  gap, not a product result. The same three also depend on `LMMS_TEST_PLUGIN_DIR`, which I did not touch.
- Not run: the GUI-facing suites (AutomationEditorEditingTest, MixerMeteringTest and similar), and the
  full ctest bar. The brief limits test runs to the suites touched.

## 2647 - why the general lock stays (BLOCKED)

The fix that is exact is a lock-free read of one model by one thread. Applied to every
`valueBuffer()` caller it is not exact, for three reasons, each with evidence:

1. The mutex is the only guard on a model's cache across audio jobs. Mixer channel jobs run in parallel
   (the dependency-counter scheduler, Mixer.cpp), so a model read from two jobs in one period races
   without it. This was not reproduced at runtime; it is a code reading.
2. Controllers are shared and unguarded. `Controller::valueBuffer()` (src/core/Controller.cpp:124-130)
   checks `m_bufferLastUpdated != s_periods` without a lock and calls `updateValueBuffer()`, and
   `LfoController::updateValueBuffer()` (src/core/LfoController.cpp:88-104) advances the LFO's phase and
   its own amount model's buffer. One LFO driving two tracks' controls is read from two bus jobs
   (STAGE 2). That race is pre-existing and sits outside `m_valueBufferMutex`; removing the model lock
   would add a second race on the amount model. Unverified at runtime.
3. Linked rings take another model's mutex (AutomatableModel.cpp, the linked branch calls
   `next->valueBuffer()`), and `setValueInternal()` (AutomatableModel.cpp:354-358) writes `m_oldValue`
   without the lock, which the interpolation path reads.

So the general fix needs an owner decision (for example: audit each caller's single-owner invariant
and lock the shared controller state, or move the first computation of each period to the render thread).
The sidechain read is exact on its own because its only reader is the receiver's job and its ramp is
published on the render thread (Song.cpp, STAGE 0) before the jobs start.

## 5528 - why it stays BLOCKED

`writeBlockRamp()` (AutomationClip.cpp:763) takes `m_clipMutex` once per block, and the private
`valueAt(v, offset)` (AutomationClip.cpp:610-612) takes it again (recursive). The public `valueAt(TimePos)`
(AutomationClip.cpp:568-570) also locks.

A publish-on-edit snapshot, the fix idea, cannot be made correct today:

- `getTimeMap()` returns a mutable reference to the live curve (AutomationClip.h:144-146). At least 13 call
  sites bind that reference and edit it: AutomationEditor.cpp (7 sites), AutomationPointEdit.cpp:79,
  ControlCommandsAutomationTools.cpp:179, ControlCommandsAutomationLanes.cpp:215, AutomationNode.cpp:80 and
  :100, PianoRoll.cpp:1122. Some of these edits do not take `m_clipMutex` at all (AutomationNode::setInValue
  edits through the reference with no lock). A snapshot published from clip methods would never see them,
  so the audio thread would read a stale curve.
- The fix therefore needs the mutable accessor removed and every writer routed through a method that
  publishes, plus a reader-count protocol (the GUI waits for the audio reader count to reach zero before
  freeing an old snapshot). That is an API change across GUI and control code, which is the owner's call.

Not started, so no code changed for 5528.

## Ledger changes

- tests/rt-safety-scope.txt: two declared audio-thread paths, `MixerChannel::sumSidechainInputs` and
  `AutomatableModel::valueBufferForSoleReader`. Both have zero hits; the allowlist is unchanged (the four
  existing hits are the same allowlisted ones).
- tests/upstream-modifications.txt: three lines for the inherited files changed (AutomatableModel.h,
  AutomatableModel.cpp, Mixer.cpp).

## Notes

- The coordinator's environment notes gave the working directory as `zene-m0-train2`. My assigned worktree
  is `zene-bunny-theory` (branch `bugs/m0-blk-automation`); I stayed in it and did not touch the other.
- No push, merge, rebase, reset, stash or branch deletion. Commits: c674877bc (fix) and the report commit.
