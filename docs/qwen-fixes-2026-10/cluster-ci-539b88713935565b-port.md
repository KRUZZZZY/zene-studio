ALREADY-FIXED

# cluster ci-539b88713935565b-port — `build / mingw64` on branch `bugs/qwen-ci-539b88713935565b-port-port`

ci-539b88713935565b-port: ALREADY-FIXED

## Verdict

**ALREADY-FIXED on this branch (HEAD `e4194e0bf5`).** This job's own commits change no product or test code (only this report). The branch does carry the
earlier fix's test files relative to `master`: `tests/qt5-worker-thread-check.py`,
`tests/src/gui/Warp/SampleClipWarpEditTest.cpp` and `tests/src/core/RemotePluginAudioPortsTest.cpp`
(`git diff --stat $(git merge-base master HEAD) HEAD` lists all three as added).

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

## Commands run, round 4 (current: logs and manifest in `~/.config/kads/zene/state/runs/ci-539b88713935565b-port.r4/logs`, manifest `proof.json`)

1. The same build command as round 3, into `r4/logs/build-green.log`: **EXIT=0** (`build-green.exit`). sha256 `5df4b9db8f951426dc00e50d4da8a132220c0947f180eea801f067ce47c7eeec`.
2. The same ctest command as round 3, from `build/tests`, into `r4/logs/test-green.log`: **EXIT=0** (`test-green.exit`), `100% tests passed out of 6`. sha256 `0e3ac382f07eb9b87ca3c586324cf9ec59ddeabb0a648240851c17166dcffdc2`.
3. `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > r4/logs/gates-1.log 2>&1`: **EXIT=0**, `RESULT: PASS`.
4. The manifest's `test` artifacts carry `test_file`, `test`, `green_command`, `green_exit` and `sha256`. Round 3's extra `log` key was refused by the schema, so it is gone.

## Commands run, round 3 (earlier run, kept for history, in `.r3/logs`)

1. `cmake --build build --target zene RemotePluginAudioPortsShmCompileCheck RemotePluginAudioPortsTest SampleClipWarpEditTest WarpMarkersTest MidiOutTimingTest ExportWavWriteErrorTest -j4 > logs/build-green.log 2>&1` gave **EXIT=0** (`build-green.exit`). The log ends `[100%] Built target ExportWavWriteErrorTest`. sha256 `7679ae9751aa55bb30fe650ec7f5f2a638069473fdf71a3685e1621305d0c763`.
2. From `build/tests`: `ctest -R '^(Qt5WorkerThreadCheck|RemotePluginAudioPortsTest|SampleClipWarpEditTest|WarpMarkersTest|MidiOutTimingTest|ExportWavWriteErrorTest)$' --output-on-failure > logs/test-green.log 2>&1` gave **EXIT=0** (`test-green.exit`), `100% tests passed out of 6`. sha256 `0ac246f5f004ae3ad8c00475c0ba23d2848d4532d6adab71aa87f9e3b5d4b9cb`.

## Commands run, round 2 (earlier run, kept for history; that log folder no longer exists)

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

* Regression tests: this job adds none, because it makes no fix. The branch-added guards `Qt5WorkerThreadCheck`
  (`tests/qt5-worker-thread-check.py`), `SampleClipWarpEditTest`, `RemotePluginAudioPortsTest` and the
  `RemotePluginAudioPortsShmCompileCheck` object target were built and run green in round 3, and again in round 4 (round-4 commands 1–2).
  The manifest records them as `test` artifacts with green_exit 0 and the sha256 of each log.
* Registration: this job's commits add or modify only this report, which is under `docs/`.
* Realtime, undo and commands: untouched.
* RED/GREEN with the product change reverted: not applicable. This job made no product change, so
  there is nothing to revert. The existing guards above are green at HEAD (ctest EXIT=0, 6/6).
* Determinism: the guards are compile checks (Qt5WorkerThreadCheck compiles against a shadow
  `qtcore-config.h`, and the SHM check compiles an OBJECT library). No clocks, sleeps or threads.
* Cause and siblings: all four former `QThread::create` call sites and the three `-Werror` helpers
  were checked at HEAD (tables above). An fff search finds no remaining compiled `QThread::create(`.
* Collateral damage: none. No code or saved-data path changed.
* ALREADY-FIXED evidence: `tests/src/core/TestWorkerThread.h:45`,
  `tests/src/gui/Warp/SampleClipWarpEditTest.cpp:434,464,552`, `tests/src/core/WarpMarkersTest.cpp:132`,
  `tests/src/core/RemotePluginAudioPortsTest.cpp:75-84`, `tests/src/core/MidiOutTimingTest.cpp:69,259`,
  `tests/src/core/ExportWavWriteErrorTest.cpp:190,225`, `tests/CMakeLists.txt:3349,6580`.
* Sibling commands (app.* and others): not affected. No command file was changed by this job. The
  files that submit_check lists as "tests added or changed" (`tests/control-*.py`) come from the
  branch's earlier merge commits against the base, not from this job.
* submit_check: `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> ci-539b88713935565b-port` gave EXIT=0, `RESULT: PASS - submit` (round-2 logs/submit-1.log).
* Round 3 gates: `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > r3/logs/gates-1.log 2>&1` gave EXIT=0, `RESULT: PASS`. The round-3 submit_check result is in `r3/logs/submit-1.log`. The round-4 result is in `r4/logs/submit-1.log`.

Delegation: Haiku (1) found the mingw64 job in the three newest ancestor CI log sets and extracted
the failing `error:` lines. Haiku (2) ran the build (command 1) and the ctest run (command 2) and
reported their exit codes. Sonnet and DeepSeek were not used: there was no code to write.

## Other findings

None.
