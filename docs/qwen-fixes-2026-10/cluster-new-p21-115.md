FALSE-POSITIVE

new-p21-115: FALSE-POSITIVE

## Finding

"`ConvolutionReverbEffect` rebuilds the impulse on `sampleRateChanged` on the emitting thread"
(plugins/ConvolutionReverb/ConvolutionReverb.cpp, historical line 163). The claim: `sampleRateChanged`
"may be emitted from the audio/render thread", so the slot's kernel rebuild (allocation, resampling,
FFTW planning, `requestChangeInModel`/`doneChangeInModel`) would run on the audio thread.

## Decision: FALSE-POSITIVE

The slot never runs on the audio or render thread. Two independent facts show this.

### 1. Every emit of the signal is on the control thread, with audio quiesced

`AudioEngine::sampleRateChanged` is emitted in exactly three places in product code (fff grep
`src/core/AudioEngine.cpp sampleRateChanged`; the other emits are in tests only:
tests/src/core/AudioPortsModelTest.cpp:411,432, tests/src/core/DeviceDspTestParts2.h:128):

- `src/core/AudioEngine.cpp:812`, at the end of `AudioEngine::initDevices()`, at startup before
  processing starts.
- `src/core/AudioEngine.cpp:1427`, in `AudioEngine::setAudioDevice()`, after `stopProcessing()`
  (line 1422).
- `src/core/AudioEngine.cpp:1454`, in `AudioEngine::restoreAudioDevice()`, after `stopProcessing()`
  (line 1450). `startProcessing()` comes after the emit.

Callers of the two runtime paths:

- `setAudioDevice` has one product caller, `src/core/ProjectRenderer.cpp:267`, inside
  `ProjectRenderer::startProcessing()`. That runs *before* `start()` launches the render
  QThread, and the code says so at `ProjectRenderer.cpp:265-266`: "Have to do audio engine stuff with
  GUI-thread affinity in order to make slots connected to sampleRateChanged()-signals being called
  immediately." Its caller is `RenderManager` (`src/core/RenderManager.cpp:233`), reached from
  `renderNextTrack()`. The render thread's `finished()` signal reaches that function through a
  connection to the main-thread `RenderManager` object (`RenderManager.cpp:219`), so the call is
  queued onto the main thread.
- `restoreAudioDevice` has one product caller, `RenderManager::~RenderManager()`
  (`src/core/RenderManager.cpp:48-51`). RenderManager is created only on the main thread:
  `src/gui/modals/ExportProjectDialog.cpp:515`, `src/core/main.cpp:1578` (CLI render),
  `src/core/MasteringJob.cpp:324` (a stack object in `renderSourceMix()`, called from the CLI
  `MasteringJob` at `src/core/main.cpp:1541`).

So no audio or render thread ever emits the signal, and every runtime emit happens while the
audio device is stopped. That is the same engine contract that `plugins/Dispersion/Dispersion.cpp:66-68`
documents for its own `sampleRateChanged` slot.

### 2. Even a cross-thread emit would be queued onto the effect's thread

The connection is `connect(Engine::audioEngine(), &AudioEngine::sampleRateChanged, this, [this]{...})`
(`plugins/ConvolutionReverb/ConvolutionReverb.cpp:177`). It has a context object (`this`) and the
default `Qt::AutoConnection`. Qt docs (qtcore/qt.html, `Qt::ConnectionType`): "AutoConnection: If the
receiver lives in the thread that emits the signal, Qt::DirectConnection is used. Otherwise,
Qt::QueuedConnection is used. The connection type is determined when the signal is emitted." The
effect is a QObject created on the main thread, and no product code moves an effect to another thread
(fff grep `moveToThread` in `*.cpp`: only RemotePlugin.cpp:133,329, ScriptEngine.cpp:315,
TaskCenter.cpp:52, TrackContainerView.cpp:83, none of which is an Effect). If the signal were ever
emitted from the audio thread, the lambda would therefore be queued to the main thread anyway. This
is exactly the fix the finding proposes, and the code already has it.

The deadlock scenario ("the audio thread already holds the model-change guard") needs the slot to run
on the audio thread, and neither fact above allows that. The slot takes
`requestChangeInModel()` on the control thread, which is that guard's intended use.

No product code was changed, so this cluster has no regression test (a not-real verdict needs a code
reference, and the proof manifest carries a `code-reference` artifact).

## Commands run (EXIT codes copied from the session)

- `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p21-115 > .../logs/01-feature-check.log 2>&1; echo EXIT=$?`
  gave **EXIT=1**, and the gate is **BLOCKED**. The script crashed before it printed its table: the bunny-guard
  refused the script's own baseline step,
  `git -C /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/gate-baselines/tree checkout -q --detach 9913d36d5397025d1b7092097f50f6d50c652dc4`
  ("is not allowed in a Bunny lane"), and the result was `subprocess.CalledProcessError ... exit status 97` at
  feature_check.py:60 (`base_listings`). As the run rules require, I did not work around it. The only file this
  cluster adds is this report under docs/, so there is no new product code for the complexity,
  file-length or registration gates to judge. Even so, the gate result is unverified in this session.
- No build or ctest was run, because no product code or test changed.

## Self-check

`python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p21-115 > .../logs/02-submit-check.log 2>&1; echo EXIT=$?`
gave EXIT=1. Commits, clean tree, report and the claim-only closure PASS. The one FAIL is
`gates (feature_check.py)`, the guard-blocked gate described above. I cannot fix it from this lane
without working around bunny-guard, so that step stays BLOCKED, and I am leaving that FAIL for the
gate tooling owners.

- Regression test red/green: not applicable. The verdict is FALSE-POSITIVE, no product code changed, and
  there is nothing to revert. The proof manifest carries a `code-reference` instead.
- Determinism: not applicable, because no test was added.
- Fix at cause / callers and siblings: no fix was made. I checked every emitter and every runtime caller of
  the signal: AudioEngine.cpp:812/1427/1454, ProjectRenderer.cpp:267, RenderManager.cpp:48-51/219/233,
  ExportProjectDialog.cpp:515, main.cpp:1541/1578, MasteringJob.cpp:324.
- Collateral damage: none. The only change in this commit is this report file under docs/.
- FALSE-POSITIVE evidence: `plugins/ConvolutionReverb/ConvolutionReverb.cpp:177` (a context-object
  AutoConnection, so a cross-thread emit would be queued), `src/core/AudioEngine.cpp:1422,1427` and
  `:1450,1454` (each emit comes after `stopProcessing()`), and `src/core/ProjectRenderer.cpp:265-267` (the
  emit happens on the GUI thread before the render thread starts).

Delegation: Haiku ran the feature_check gate and reported the guard refusal (I verified it in
01-feature-check.log). I did the diagnosis myself: it was five targeted searches and two short reads, which took
less effort than writing a delegation brief would have. Sonnet was not started because there was no test or fix to
write.

## Other findings

- `feature_check.py` cannot run in this lane: its baseline checkout of the shared gate-baselines tree is
  refused by bunny-guard (see above). This needs a fix in the gate tooling, not in this repo.
