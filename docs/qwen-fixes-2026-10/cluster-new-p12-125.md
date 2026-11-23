FALSE-POSITIVE

# Cluster new-p12-125 — "Non-threaded LV2 worker runs work synchronously from scheduling"

## Verdict

The code does execute `work()` inline when `m_threaded == false`, exactly as the finding says. That is not a
defect: the non-threaded worker is selected only for a render-only (`Engine::init(true)`) engine, which is the
offline-render and headless-script path with no real-time audio device, and the LV2 Worker specification
explicitly allows (and expects) immediate execution in that situation. Every engine that drives a real audio
device is initialised with `Engine::init(false)`, so its LV2 workers are always threaded. There is therefore no
path where plug-in `work()` runs on a real-time audio thread. No product change was made.

## Evidence (all line numbers re-read in this worktree, branch `bugs/qwen-new-p12-125`, product tree `c6b40333d8`; the report-only commits after it change no code)

| Fact | Location | Code |
| --- | --- | --- |
| Inline execution is the non-threaded branch of `scheduleWork()` | `src/core/lv2/Lv2Worker.cpp:203-211` | `else { … m_workLock->wait(); m_interface->work(m_handle, staticWorkerRespond, this, size, data); m_workLock->post(); }` |
| Threaded mode queues instead of executing | `src/core/lv2/Lv2Worker.cpp:189-201` | `if (m_threaded) { … m_requests.write(…); m_sem.post(); }` |
| Worker mode is chosen from `renderOnly()` | `src/core/lv2/Lv2Proc.cpp:603-604` | `bool threaded = !Engine::audioEngine()->renderOnly();` / `m_worker.emplace(&m_workLock, threaded);` |
| `renderOnly()` is the constructor flag | `include/AudioEngine.h:160` | `bool renderOnly() const { return m_renderOnly; }` |
| Render-only engine has no real device: `AudioDummy` + `MidiDummy` | `src/core/AudioEngine.cpp:796-800` | `if( m_renderOnly ) { m_audioDev = new AudioDummy( … ); … }` |
| The GUI engine, which opens a real device, is never render-only | `src/gui/GuiApplication.cpp:215` | `Engine::init(false);` |
| Render-only is used only by the headless offline paths | `src/core/main.cpp:1479` (`--render`) and `src/core/main.cpp:1626` (`--script`) | `Engine::init( true );` |
| The only place `AudioEngine` is constructed is from that flag | `src/core/Engine.cpp:72` | `s_audioEngine = new AudioEngine( renderOnly );` |
| The LV2 Worker spec permits immediate execution for free-wheeling/offline rendering | `/usr/include/lv2/worker/worker.h:138-147` | "This function is always safe to call from run(), but it is not guaranteed that the worker is actually called from a different thread. In particular, when free-wheeling (for example, during offline rendering), the worker may be executed immediately. …" |
| The response path is consistent with it | `src/core/lv2/Lv2Worker.cpp:146-151` | non-threaded `respond()` calls `m_interface->work_response(...)` directly, as the spec's "delivered immediately" wording requires |

## Why the finding's mechanism cannot happen

1. `m_threaded == false` exists only when `Engine::audioEngine()->renderOnly()` is true
   (`src/core/lv2/Lv2Proc.cpp:603`). `Engine::init(true)` is reached only from `--render` and `--script`
   (`src/core/main.cpp:1479`, `:1626`); the GUI calls `Engine::init(false)` (`src/gui/GuiApplication.cpp:215`).
2. In the render-only engine the audio device is `AudioDummy`, not a real-time backend
   (`src/core/AudioEngine.cpp:796-800`). `--render` additionally swaps in a file device and renders on
   `ProjectRenderer`'s own thread, which `src/core/ProjectRenderer.cpp:288-290` documents as
   "Everything below renders on this thread alone". Neither path feeds a real-time audio callback.
3. A real device (`tryAudioDevices()`, `src/core/AudioEngine.cpp:801-803`) is only reached when
   `renderOnly()` is false, i.e. when the worker is threaded. So the "audio thread can block … causing
   dropouts" scenario has no reachable configuration.
4. The spec text above says immediate execution is legal precisely for offline rendering and gives the reason
   (sample-accurate single-threaded processing). Refusing it would discard the intended offline semantics.

The finding's own proposed guard ("refuse this worker mode during real-time processing") is already the
selection rule at `src/core/lv2/Lv2Proc.cpp:603`: threaded whenever the engine is not render-only.

## What was checked around the change (siblings / callers)

- The only `Lv2Worker` construction is `src/core/lv2/Lv2Proc.cpp:604`; `grep -rn "Lv2Worker(" src include`
  finds the constructor plus this one emplacement.
- `respond()` (`src/core/lv2/Lv2Worker.cpp:132-153`), `workerFunc()` (`:159-181`), `emitResponses()`
  (`:220-233`) and `notifyPluginThatRunFinished()` (`include/Lv2Worker.h:57-60`) all already have a matching
  non-threaded branch or are harmless when no queue is used. There is no second path into `scheduleWork()`:
  it is reached through the `LV2_Worker_Schedule` lambda at `src/core/lv2/Lv2Worker.cpp:75-79`.
- No header, product source, control command, undo state or save format was modified, so no other caller can
  be affected.

## Commands run and exit codes

| Command | EXIT | Log / result |
| --- | --- | --- |
| `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 | `logs/feature-check-1.log` — all 8 rows PASS, `RESULT: PASS` |
| `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p12-125` | 0 | `logs/submit-check-1.log` — `RESULT: PASS - submit`, no FAIL rows |

No build and no test run is claimed: the verdict is FALSE-POSITIVE, so there is no product change and no RED/GREEN
pair is applicable. `submit_check.py` has a claim-only branch for such closures when its diff range contains no
product file; in this lane the range also contains the pre-existing train commits, so its
"tests added or changed: PASS" row is populated by the train's test files, not by this job. It finds no FAIL.

## Self-check

- **Regression test FAILS reverted / PASSES with the fix:** not applicable. There is no product change and no
  bug to fix; the finding is a false positive. No RED/GREEN result is claimed. `submit_check.py` reports no
  FAIL for this closure.
- **Test deterministic:** not applicable, for the same reason; no test was added or run.
- **Fix at the cause and complete:** no fix was made. Every user of `Lv2Worker` and every sibling method of
  `scheduleWork()` was inspected (list above), and all of them already distinguish threaded from non-threaded
  consistently with the LV2 Worker specification.
- **No collateral damage:** no code, header, save format, command or test changed, so old projects and saved
  data load exactly as before.
- **FALSE-POSITIVE evidence:** `src/core/lv2/Lv2Proc.cpp:603` selects the non-threaded worker only when
  `Engine::audioEngine()->renderOnly()` is true; `src/core/main.cpp:1479` and `:1626` are the only render-only
  `Engine::init` callers, `src/gui/GuiApplication.cpp:215` is the real-audio GUI path and passes `false`; and
  `/usr/include/lv2/worker/worker.h:138-147` states immediate execution is allowed for offline rendering.
- **Sibling commands:** this commit changes no command file, so there are no command siblings to keep
  consistent. `submit_check.py` listed `app.*`, `arrangement.*` and other ids only because its diff range
  (`FIX-BASE..HEAD`) also contains the pre-existing train commits; `git diff --name-only` for this job's own
  commit (`20dd5964fa`) contains exactly one path, the report. No control command, undo state, reversibility
  class or `ControlCommandsSnapshot` canary is touched.
- **Completion check:** `submit_check.py` (log `logs/submit-check-1.log`, EXIT=0) printed six PASS rows and
  `RESULT: PASS - submit`; the rerun after the report update is `logs/submit-check-2.log` (EXIT=0) and the
  final rerun is `logs/submit-check-3.log`. Its "tests added or changed" row names `tests/control-*.py` files
  that belong to the pre-existing train range, not to this job: this job's commit changes only this report.

## Other findings

- `AudioEngine::renderNextPeriod()` opens a `RealtimeScope` unconditionally (`src/core/AudioEngine.cpp:1212`).
  Under a `WANT_DEBUG_RTSAN` build an offline render that reaches a non-threaded LV2 `work()` could report the
  plug-in's own allocations as realtime violations, although the offline path has no realtime deadline. This
  is an instrumentation false alarm, not a dropout defect; it is not fixed here (out of scope).
