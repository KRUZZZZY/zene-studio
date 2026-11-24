ALREADY-FIXED

# cluster ci-539b88713935565b-port — `build / mingw64` on branch `bugs/qwen-ci-539b88713935565b-port-port`

ci-539b88713935565b-port: ALREADY-FIXED

## Verdict

**ALREADY-FIXED on this branch (HEAD `e4194e0bf5`).** No product or test code changed in this job.

The cluster's original failure (the reported commit `c20ef591b5`, which is an ancestor of HEAD) was
`'create' is not a member of 'QThread'` in `tests/src/gui/Warp/SampleClipWarpEditTest.cpp`: the
hosted mingw64 Qt 5.15.6 has `QT_FEATURE_cxx11_future -1`. The diagnosis and fix are in
`docs/qwen-fixes-2026-10/cluster-ci-539b88713935565b.md`. That fix (commit `14e637c600`) is an
ancestor of HEAD (`git merge-base --is-ancestor 14e637c600 HEAD` gave exit 0), and the code is present:

* `tests/src/core/TestWorkerThread.h:45` `class TestWorkerThread : public QThread`, the portable
  replacement.
* `tests/src/gui/Warp/SampleClipWarpEditTest.cpp:434,464,552` and `tests/src/core/WarpMarkersTest.cpp:132`
  now use `test::TestWorkerThread`.
* An fff search for `QThread::create(` over the worktree matches no first-party compiled source. The
  only hits are comments (`plugins/SpectrumAnalyzer/Analyzer.h:188,191`,
  `tests/reference/SpectrumAnalyzer/Analyzer.h:61,64`, the header doc) and the fixtures in the guard
  script `tests/qt5-worker-thread-check.py`.
* The guard is registered at `tests/CMakeLists.txt:6580` (`add_test(NAME Qt5WorkerThreadCheck ...)`).

### Later mingw64 failures in the saved hosted logs (checked, also fixed at HEAD)

I checked the newest mingw64 job logs saved for HEAD's ancestors under
`~/.local/share/zene-bunny-fix/qwen/train-ci/`. I read them as data only. Each one fails on a
different `-Werror=unused-function` helper, and HEAD fixes each of them:

| commit (ancestor of HEAD) | log:line | error | fixed at HEAD |
|---|---|---|---|
| `fa4a5f2e78` | `run-43786984969-job-133036727594.log:5814` | `RemotePluginAudioPortsTest.cpp:75:6 countCriticals defined but not used` | `tests/src/core/RemotePluginAudioPortsTest.cpp:75-84` is wrapped in `#ifndef SYNC_WITH_SHM_FIFO` (commit `79691f5414`, ancestor). `tests/CMakeLists.txt:3342-3356` compiles the TU with `SYNC_WITH_SHM_FIFO` on every host (`RemotePluginAudioPortsShmCompileCheck`) |
| `31f559ab47` | `run-43764792400-job-132967358836.log:8904,8907` | `MidiOutTimingTest.cpp:107/83 measureAgainstRequested / measure defined but not used` | `tests/src/core/MidiOutTimingTest.cpp:69` `#ifdef LMMS_HAVE_ALSA` … `:259 #endif` encloses `measure` (`:84`) and `measureAgainstRequested` (`:108`). Their only users (`:287-288`) are inside `#ifdef LMMS_HAVE_ALSA` at `:267` (commit `f3b5426267`, ancestor) |
| `cc29a63dde` | `run-43710174450-job-132799809563.log:5378` | `ExportWavWriteErrorTest.cpp:201 riffSizes defined but not used` | `tests/src/core/ExportWavWriteErrorTest.cpp:190` `#ifdef Q_OS_UNIX` … `:225` encloses `riffSizes` (`:202`). Its user (`:336`) is inside `#ifdef Q_OS_UNIX` at `:312` (commit `9feb23eb1b`, ancestor) |

All three fix commits are ancestors of HEAD (`git merge-base --is-ancestor <c> HEAD` gave exit 0 for
`79691f5414`, `f3b5426267` and `9feb23eb1b`).

## Commands run (logs in `~/.config/kads/zene/state/runs/ci-539b88713935565b-port.r2/logs`)

1. `cmake --build build --target zene RemotePluginAudioPortsShmCompileCheck RemotePluginAudioPortsTest MidiOutTimingTest ExportWavWriteErrorTest SampleClipWarpEditTest WarpMarkersTest -j4 > logs/build-1.log 2>&1` gave **EXIT=0**. The log ends `[100%] Built target WarpMarkersTest`, with no `error:` lines.
2. From `build/tests`: `ctest -R 'Qt5WorkerThreadCheck|RemotePluginAudioPortsTest|MidiOutTimingTest|ExportWavWriteErrorTest|WarpMarkersTest|SampleClipWarpEditTest' --output-on-failure > logs/test-1.log 2>&1` gave **EXIT=0**. The summary was `100% tests passed out of 6`: SampleClipWarpEditTest, ExportWavWriteErrorTest, MidiOutTimingTest, RemotePluginAudioPortsTest, WarpMarkersTest and Qt5WorkerThreadCheck.
3. `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/gates-1.log 2>&1` gave **EXIT=0**, `RESULT: PASS`: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity and file-length all PASS.

## Limits

* This box has no MinGW toolchain, so the hosted mingw64 job was not re-run here. The local proof
  uses the on-host stand-ins the earlier fixes added: Qt5WorkerThreadCheck, which compiles against a
  Qt 5 shadow with `cxx11_future` off, and RemotePluginAudioPortsShmCompileCheck, which compiles the
  TU with `SYNC_WITH_SHM_FIFO`.
* No hosted mingw64 log exists yet for HEAD itself. If the job turns red again, it will be a new
  signature, not this cluster.

## Self-check

* Regression test: none is new, because there is no fix in this job. The existing regression guards
  `Qt5WorkerThreadCheck` and `RemotePluginAudioPortsShmCompileCheck` were built and run green (commands 1–2).
* Registration: no files were added or modified besides this report, which is under `docs/`.
* Realtime, undo and commands: untouched.

Delegation: Haiku (1) found the mingw64 job in the three newest ancestor CI log sets and extracted
the failing `error:` lines. Haiku (2) ran the build (command 1) and the ctest run (command 2) and
reported their exit codes. Sonnet and DeepSeek were not used: there was no code to write.

## Other findings

None.
