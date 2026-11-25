ALREADY-FIXED

# cluster ci-539b88713935565b-port — `build / mingw64` on branch `bugs/qwen-ci-539b88713935565b-port-port`

ci-539b88713935565b-port: ALREADY-FIXED

Logs and manifest for this run: `~/.config/kads/zene/state/runs/ci-539b88713935565b-port.r5/logs` (manifest `proof.json`).

## Verdict

**ALREADY-FIXED on this branch.** Every mingw64 failure behind this cluster is fixed in code that is
already on the branch, so this job changes no product code. It does add one new regression test,
`RemotePluginShmUnusedHelperCheck`, which closes a gap in the existing guards (see "Regression test
added by this job").

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
* The guard is registered at `tests/CMakeLists.txt:6617` (`add_test(NAME Qt5WorkerThreadCheck ...)`).

### Later mingw64 failures in the saved hosted logs (checked, also fixed at HEAD)

I checked the newest mingw64 job logs saved for HEAD's ancestors under
`~/.local/share/zene-bunny-fix/qwen/train-ci/`. I read them as data only. Each one fails on a
different `-Werror=unused-function` helper, and HEAD fixes each of them:

| commit (ancestor of HEAD) | log:line | error | fixed at HEAD |
|---|---|---|---|
| `fa4a5f2e78` | `run-43786984969-job-133036727594.log:5814` | `RemotePluginAudioPortsTest.cpp:75:6 countCriticals defined but not used` | `tests/src/core/RemotePluginAudioPortsTest.cpp:75-84` is wrapped in `#ifndef SYNC_WITH_SHM_FIFO` (commit `79691f5414`, ancestor). `tests/CMakeLists.txt:3354` compiles the TU with `SYNC_WITH_SHM_FIFO` on every host (`RemotePluginAudioPortsShmCompileCheck`) |
| `31f559ab47` | `run-43764792400-job-132967358836.log:8904,8907` | `MidiOutTimingTest.cpp:107/83 measureAgainstRequested / measure defined but not used` | `tests/src/core/MidiOutTimingTest.cpp:69` `#ifdef LMMS_HAVE_ALSA` … `:259 #endif` encloses `measure` (`:84`) and `measureAgainstRequested` (`:108`). Their only users (`:287-288`) are inside `#ifdef LMMS_HAVE_ALSA` at `:267` (commit `f3b5426267`, ancestor) |
| `cc29a63dde` | `run-43710174450-job-132799809563.log:5378` | `ExportWavWriteErrorTest.cpp:201 riffSizes defined but not used` | `tests/src/core/ExportWavWriteErrorTest.cpp:190` `#ifdef Q_OS_UNIX` … `:225` encloses `riffSizes` (`:202`). Its user (`:336`) is inside `#ifdef Q_OS_UNIX` at `:312` (commit `9feb23eb1b`, ancestor) |

All three fix commits are ancestors of HEAD (`git merge-base --is-ancestor <c> HEAD` gave exit 0 for
`79691f5414`, `f3b5426267` and `9feb23eb1b`).

## Regression test added by this job

**The gap.** `RemotePluginAudioPortsShmCompileCheck` (`tests/CMakeLists.txt:3354`) compiles
`tests/src/core/RemotePluginAudioPortsTest.cpp` with `SYNC_WITH_SHM_FIFO`, the branch mingw64 takes
because it has no `<sys/ipc.h>`. But it builds without `-Werror=unused-function`, and it is not a
ctest. Deleting the `#ifndef SYNC_WITH_SHM_FIFO` guard around `countCriticals`
(`tests/src/core/RemotePluginAudioPortsTest.cpp:75-84`, helper at `:80`) would therefore stay green
on Linux while the hosted mingw64 job failed.

**The test.** `tests/remote-plugin-shm-unused-helper-check.py` (new, registered in
`tests/fork-sources.txt`) runs as ctest `RemotePluginShmUnusedHelperCheck` (`tests/CMakeLists.txt:6632`,
Linux and `PYTHON3_EXECUTABLE`, next to Qt5WorkerThreadCheck). It:
* takes that target's own compile command from `compile_commands.json`;
* adds `-DSYNC_WITH_SHM_FIFO -Werror=unused-function`;
* compiles the file to a discarded object (`-o /dev/null`);
* fails on a non-zero compiler exit or on any `warning:`/`error:` text.

Two choices follow from what the compiler does:
* It compiles to an object rather than using `-fsyntax-only`, because GCC reports unused static
  functions only once it generates code. With `-fsyntax-only` the pre-fix file came out clean.
* The file's `#include "RemotePluginAudioPortsTest.moc"` resolves to an empty stub placed first on
  the quote include path. An object that is never linked needs no moc code, and the stub makes the
  result the same whether or not AUTOMOC has run.

`--source` lets the check compile another copy of the file. That is how the RED run below uses the
pre-fix source.

It is deterministic: a single compiler invocation, with no clock, sleep or thread.
`tests/all-sources.txt` lists only C/C++ sources (the existing `tests/qt5-worker-thread-check.py` is
not in it), so the `.py` file is not added there. `tests/upstream-modifications.txt` is untouched.

## Commands run (logs in `~/.config/kads/zene/state/runs/ci-539b88713935565b-port.r5/logs`)

1. `cmake --build build --target zene RemotePluginAudioPortsShmCompileCheck -j4 > logs/build-1.log 2>&1`
   gave **EXIT=0**. Running `cmake build` directly was refused by the lane guard (EXIT=97,
   `configure-2.log`); `cmake --build` regenerated the build tree from the changed `tests/CMakeLists.txt`.
   Afterwards, `ctest -N -R '^RemotePluginShmUnusedHelperCheck$'` gave `Total Tests: 1`.
2. RED, against the source from just before the guard was added:
   `git show 79691f5414^:tests/src/core/RemotePluginAudioPortsTest.cpp > logs/RemotePluginAudioPortsTest.pre-fix.cpp`, then
   `python3 tests/remote-plugin-shm-unused-helper-check.py --root <worktree> --build-dir <worktree>/build --cxx /usr/bin/c++ --source logs/RemotePluginAudioPortsTest.pre-fix.cpp > logs/test-red-2.log 2>&1`
   gave **EXIT=1**. The only compiler error is
   `RemotePluginAudioPortsTest.pre-fix.cpp:75:6: error: 'void {anonymous}::countCriticals(QtMsgType, const QMessageLogContext&, const QString&)' defined but not used [-Werror=unused-function]`,
   the hosted job's own message. sha256 `faa3f7186ba19122dcb9ed8044a3053f1580bd9a4da5e443938f6bea29060fd6`.
   An earlier `test-red.log` was also EXIT=1, but it picked up the generated `.moc`, which matches
   HEAD's source, and showed two extra moc errors. Making the stub win removed them, and
   `test-red-2.log` is the result of record.
3. GREEN at HEAD, from `build/tests`:
   `ctest -R '^RemotePluginShmUnusedHelperCheck$' --output-on-failure > logs/test-green-2.log 2>&1`
   gave **EXIT=0**, with `1/1 Test #840: RemotePluginShmUnusedHelperCheck ... Passed` and `100% tests passed out of 1`.
   sha256 `168aa4d6ffc864ae13574ebe396ad2f540f9d7f317fee3325c2724c6f24d2d06`.
4. Gates: `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/gates-2.log 2>&1`. The result is in the Self-check section.

## Limits

* This box has no MinGW toolchain, so the hosted mingw64 job was not re-run. The local stand-ins
  are Qt5WorkerThreadCheck, which compiles against a Qt 5 shadow with `cxx11_future` off, and the
  new RemotePluginShmUnusedHelperCheck, which compiles the SHM branch with `-Werror=unused-function`.
* The new check covers `RemotePluginAudioPortsTest.cpp` only. The MidiOutTimingTest and
  ExportWavWriteErrorTest helpers are guarded by `LMMS_HAVE_ALSA` and `Q_OS_UNIX`, which are both
  true on Linux, so a Linux compile cannot exercise their off branch the same way.

## Self-check

* RED/GREEN: this job changes no product code, so there is nothing to revert. The regression is
  shown instead by compiling the pre-fix source (`79691f5414^`), which gave EXIT=1 with the hosted
  error, and HEAD's source through ctest, which gave EXIT=0 (commands 2–3).
* Determinism: one compiler invocation over fixed sources and flags. No wall-clock time, sleeps or races.
* Cause and siblings: the four former `QThread::create` call sites and the three `-Werror` helpers
  were checked at HEAD (the table above). An fff search finds no remaining compiled `QThread::create(`.
  The new check reuses the compile command of the existing SHM target, so it stays consistent with it.
* Collateral damage: none. Only a test script, its ctest registration and a fork-sources entry were
  added; no product code or saved-data path changed.
* ALREADY-FIXED evidence: `tests/src/core/TestWorkerThread.h:45`,
  `tests/src/gui/Warp/SampleClipWarpEditTest.cpp:434,464,552`, `tests/src/core/WarpMarkersTest.cpp:132`,
  `tests/src/core/RemotePluginAudioPortsTest.cpp:75-84`, `tests/src/core/MidiOutTimingTest.cpp:69,259`,
  `tests/src/core/ExportWavWriteErrorTest.cpp:190,225`, `tests/CMakeLists.txt:3354,6617`.
* Sibling commands (app.* and others): not affected. No command file was changed by this job.
* Realtime and undo: untouched.
* Gates and submit check: `feature_check.py` gave GATES_EXIT=0, `RESULT: PASS` (all eight gates PASS, `logs/gates-2.log`). `submit_check.py` runs after this commit, and its output is in `logs/submit-1.log`.

Delegation: Sonnet wrote `tests/remote-plugin-shm-unused-helper-check.py` with its CMake and
fork-sources registration, and made the first RED run. I reviewed the diff, made the stub `.moc`
take precedence, and reran RED and GREEN myself. Haiku ran no builds this round, because the
build-and-test steps were short and followed straight from the review.

## Other findings

None.
