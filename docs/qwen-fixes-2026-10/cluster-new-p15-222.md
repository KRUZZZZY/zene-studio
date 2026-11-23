FALSE-POSITIVE

# Cluster new-p15-222 — Recorder destruction does not close admission to audio pushes

new-p15-222: FALSE-POSITIVE. No product code and no test change: the state the finding describes
(`TakeRecorder` destroyed while an audio callback is inside, or entering, `push()`) is not reachable.
The recorder is a member of the `Recorder` effect, `push()` runs only inside that effect's
`processImpl()`, and the engine holds its model-change mutex for the whole render period, so no
effect can be destroyed while the render thread is inside any effect — the same lock every effect
deletion takes. What the finding calls the "admission" problem is a lifetime contract of the effect
chain, and that contract is enforced.

This is a design/contract argument, not a sanitizer measurement: local TSan does not run on this box
(brief, Facts), so nothing below claims a TSan run.

## 1. The code the finding points at

The line numbers moved. Today the recorder's teardown and its audio-side entry are:

- `src/core/TakeRecorder.cpp:60-64` — `TakeRecorder::~TakeRecorder() { stop(); detach(); }`
- `src/core/TakeRecorder.cpp:111-123` — `stop()`: clears `m_recording` (seq_cst), spins
  `while (m_pushing.load(seq_cst) != 0) { std::this_thread::yield(); }` (line 118), sets
  `m_stopping`, joins the writer thread, `sf_close(m_file)`.
- `src/core/TakeRecorder.cpp:125-130` — `push()`: `m_pushing.fetch_add(1)` (127), copy only if
  `m_recording` (128), `m_pushing.fetch_sub(1)` (129).

The literal statement in the finding is true of those lines in isolation: `push()` is admitted
regardless of any "we are stopping" state, so a push can begin after `stop()`'s loop observed 0.
That alone is harmless (a later push copies nothing, because `m_recording` is already false, and the
file is only closed after the writer thread is joined). The finding's *harm* needs a second
premise: that the recorder can be destroyed while the render thread is still able to enter `push()`.
That premise is what the rest of this report refutes.

## 2. `push()` has exactly one caller, and it is effect processing

- `plugins/InsertRecorder/InsertRecorder.cpp:85-89` — `InsertRecorderEffect::processImpl()` calls
  `m_recorder.push(inOut.asSampleFrames().data(), inOut.frames())` at line 87. This is the only
  product call of `TakeRecorder::push` (`rg -n "m_recorder.push|\.push\(" plugins src include`
  finds no other, and `TakeRecorder::push` is defined at `TakeRecorder.cpp:125`).
- `plugins/InsertRecorder/InsertRecorder.cpp:79-83` — `~InsertRecorderEffect` calls
  `m_recorder.stop(); m_recorder.detach();`, and `TakeRecorder m_recorder` is a by-value member
  (`plugins/InsertRecorder/InsertRecorder.h:68`), so `~TakeRecorder` runs inside
  `~InsertRecorderEffect`. The recorder cannot outlive, or be destroyed before, its effect.

So a push can only be in flight while *the effect* is being processed by an audio thread.

## 3. Effect processing excludes every effect destruction

The render thread takes the model-change mutex for the whole period and releases it at the end:

- `src/core/AudioEngine.cpp:1217-1220` — `renderNextPeriod()` starts with
  `const auto lock = std::lock_guard{m_changeMutex};` and only then runs
  `renderStageNoteSetup()`, `renderStageInstruments()`, `renderStageEffects()`,
  `renderStageMix()` (`:1236-1240`).
- `src/core/AudioEngine.cpp:1145-1151` — `renderStageEffects()` dispatches the buses to the worker
  threads with `AudioEngineWorkerThread::startAndWaitForJobs()`, i.e. the workers run and are joined
  *inside* the period, still under that lock. The interleaved/planar entry points all funnel into
  `renderNextPeriod()` (`include/AudioEngine.h:671-677`), so any render, including a test that
  renders a period by hand, holds the lock while effects run.
- `src/core/AudioEngine.cpp:1718-1740` — `requestChangeInModel()` / `doneChangeInModel()` lock and
  unlock exactly that mutex on control threads.

Every path that destroys the recorder's effect takes that same lock:

- `src/core/EffectChain.cpp:531-558` — `EffectChain::clear()` calls `requestChangeInModel()` (535),
  then `delete e` for each effect (550) while still inside it, and `doneChangeInModel()` (553).
  `EffectChain::~EffectChain()` (`:150-153`) and `EffectChain::loadSettings()` (`:192-200`, which
  holds `requestChangesGuard()` and then calls `clear()`) both go through it. A song, a track or a
  mixer channel torn down takes this path, so a recorder destroyed that way is destroyed with the
  render thread parked on the mutex.
- `src/core/EffectChain.cpp:307-330` — `EffectChain::removeEffect()` takes the same lock (309), erases
  the effect and clears its back-pointer, then releases it (325); the effect is no longer rendered
  from the moment the lock is released. Callers delete after that, at a point where the effect is
  off the chain and cannot be processed: `src/gui/EffectRackView.cpp:152-153`
  (`removeEffect(e); e->deleteLater();`), `src/core/ControlStructuralSupport.cpp:631-632` and
  `:686-687` (`removeEffect` then `deleteLater`, with the comment "the audio engine may still hold the
  pointer for the period in flight, so it is deleted later"), `src/core/ControlDeviceMoves.cpp:152-153`,
  `src/core/ControlCommandsW3Engine.cpp:167-169`, `src/core/ControlCommandsTrackPresets.cpp:138-139`.
- `src/core/EffectChain.cpp:65` and `:245` are the only `delete e` sites outside `clear()`, and both
  delete an effect that was never appended to `m_effects` (a failed `Effect::instantiate`, and an
  instance replaced by a `DummyEffect` before the `m_effects.push_back` at `:251`), so the render
  thread never saw either.

Conclusion: while any audio thread can be inside `processImpl()` for a given effect — and therefore
inside `TakeRecorder::push()` for that effect's recorder — no control thread can be destroying that
effect, because destruction requires the mutex the audio thread holds for as long as it renders.
The `when` of the finding ("an audio callback starts a push as the recorder is being destroyed")
therefore needs a lifetime violation elsewhere (an effect destroyed while still on a rendered chain),
and no caller does that.

A corollary worth stating, because it is the part that looks dangerous in `stop()`: the
`yield()` spin at `TakeRecorder.cpp:118` cannot spin on a push that will never finish while a caller
holds the change lock, for the same reason — if the control thread holds that lock, the render thread
is not in `push()`. `plugin.record`'s stop path (`src/core/ControlCommandsRecorder.cpp`, control
thread, no lock held) can briefly spin against a genuinely running `push()`, which is what the
counter is for, and it ends when that push's `fetch_sub` runs.

## 4. What was not done, and why

- No regression test. The procedure's step 4/4b builds a test that fails without a fix; there is no
  fix here, so there is nothing for such a test to fail on. Writing a test that "shows" the absence
  of a race would have to inject a push into a destroyed object — i.e. it would assert the very
  lifetime violation the product does not commit.
- No product edit. The counter protocol in `stop()` is right for its actual job (draining a take
  while rendering continues): after `m_recording` is cleared no new push copies, and the file is
  closed only after the writer thread is joined. Adding a "dead" flag to `push()`, or a lock around
  it, would either be dead code or break the realtime rule (no lock, no wait on the audio thread) —
  and it still could not close an admission window from inside the object, which is why the finding's
  own suggested fix is a change of lifetime, not of `push()`.

## 5. Commands run (this session), with their exit codes

Logs: `/home/kruzzzzy/.config/kads/zene/state/runs/new-p15-222.r1/logs/`

| # | command, in the order run | log | EXIT |
|---|---|---|---|
| 1 | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | `gate-1.log` | 0 (RESULT: PASS) |
| 2 | the code-reading sweep quoted above (grep -n over `TakeRecorder.cpp`, `InsertRecorder.cpp`, `AudioEngine.cpp`, `EffectChain.cpp`, `EffectRackView.cpp`) | `code-reading-1.log` | 0 |
| 3 | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p15-222` (before this report existed) | `submit-check-1.log` | 1 (the two report FAILs, no other) |
| 4 | the second code-reading sweep (`rg -l TakeRecorder src include plugins`, the `push()` callers, the `of()` callers, `~PeakController`) | `code-reading-2.log` | 0 |
| 5 | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (again, with this report in the tree) | `gate-2.log` | 0 (RESULT: PASS) |
| 6 | `git add docs/qwen-fixes-2026-10/cluster-new-p15-222.md && git commit -m "..."` → commit `16b118691d` (`git show --stat`: 1 file changed, 171 insertions) | — | 0 |
| 7 | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p15-222` (final, after that commit) | `submit-check-2.log` | 0 (RESULT: PASS - submit) |

No build and no ctest run: this job changes no C++, so there is no stale binary to distrust and no
test target to build. `git status` after the commit is empty (checked with `git status --short`).

## Self-check

- **The regression test FAILS with your product change reverted and PASSES with it (exit codes).**
  Not applicable: the verdict is FALSE-POSITIVE and this job changes no product code, so there is no
  revertible hunk and no test to fail (`git show --stat 16b118691d` touches only this file, and so does
  the follow-up commit that finishes this section). The test
  files the check lists as "tests added or changed" (`tests/control-audio-test-tone.py`,
  `tests/control-no-audio-device.py`, `tests/control-session-lifecycle-transcript.py`) are not from
  this job: they belong to earlier commits in `FIX-BASE..HEAD`, which carries 429 files from the
  train before this session started (`gate-1.log`, line 1).
  The check script records this as claim-only closure ("tests added or changed" PASS with the note
  "claim-only closure: independent QA must verify its cited code/contract"), which is the intended
  path for this verdict.
- **The test is deterministic.** No test was added. Nothing in this job depends on timing.
- **The fix is at the cause and complete: every caller and sibling was checked.**
  There is no fix. The audit done instead, over every caller and sibling: the only `push()` caller is
  `InsertRecorderEffect::processImpl` (`plugins/InsertRecorder/InsertRecorder.cpp:87`); the effect is
  the only owner of a `TakeRecorder` (`plugins/InsertRecorder/InsertRecorder.h:68`). The other take
  recorders in the tree (`MultiTrackRecorder`, `SamplerTakeRecorder`, `MidiTakeRecorder`,
  `MidiTakeCapture`) are separate types and are not touched by this finding.
  `rg -l TakeRecorder src include plugins` (`code-reading-2.log`) lists 20 paths; apart from the class
  itself and the plugin, they are those siblings and files that only include
  `AudioEngine.h`/`InstrumentTrack.h`. The users of `TakeRecorder` itself are
  `ControlCommandsRecorder.cpp:61` and `EffectDeviceMenu.cpp:149`, both `TakeRecorder::of()` on the
  control thread, plus the plugin's own `lastTake()`/`setLastTake()`
  (`plugins/InsertRecorder/InsertRecorder.cpp:62,69`) — never an audio callback.
- **No collateral damage.** No product, data, save/load or format behaviour changes; this commit adds
  one markdown file. Old projects load exactly as before because nothing they load was edited.
- **If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended contract
  that shows it.** The contract and its code: effect processing runs under `m_changeMutex`
  (`src/core/AudioEngine.cpp:1217-1220`, workers joined inside the period at `:1145-1151`), and every
  effect destruction takes that mutex (`src/core/EffectChain.cpp:531-558` for `clear()`/`~EffectChain`
  /`loadSettings`, `:307-330` for `removeEffect` with the callers deleting afterwards, e.g.
  `src/gui/EffectRackView.cpp:152-153`). Sections 2 and 3 above give the rest.
- **Sibling commands in the command files you changed.** None was changed by this job; the list is
  inherited from the 429 files the branch already carries against `FIX-BASE` (`gate-1.log` line 1).
  `plugin.record` and `plugin.recorder_land` — the two commands that reach this recorder — are
  untouched, and their behaviour is unchanged by a report-only commit.
- **Realtime / undo / commands.** No code runs on the audio thread beyond what already did; no
  allocation, lock, wait or log was added there. No undo step and no command was added or changed, so
  `ControlCommandsSnapshot` is unmoved.

## Other findings

- `src/core/PeakController.cpp:74-80`: `~PeakController` removes its effect from the chain under the
  change lock but does not delete it; the `PeakControllerEffect` is a QObject child and is deleted by
  its parent's destruction, which is not the same path as `EffectChain::clear()`. I did not chase
  whether that can delete a peak effect while it is still on a rendered chain. It is unrelated to this
  cluster and I did not change it.
- `EffectChain::clear()` calls `Engine::audioEngine()->requestChangeInModel()` without a null check on
  the engine; during static teardown the engine may already be gone. Pre-existing, unrelated, not
  changed.
