# Cluster new-p12-198: process watcher shutdown-flag data race

**Verdict: FIXED**

`ProcessWatcher::stop()`/`reset()` stored `m_quit` on the calling thread while the
watcher's `run()` loop read it on the watcher thread. `volatile` does not
synchronize threads, so this was a C++ data race on the shutdown flag and the
watcher was not obliged to observe a shutdown reliably. The flag is now
`std::atomic<bool>` ([include/RemotePlugin.h:100](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-198/include/RemotePlugin.h:100);
the stores are at lines 83/89 and the reads are
[src/core/RemotePlugin.cpp:127](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-198/src/core/RemotePlugin.cpp:127)
and [src/core/RemotePlugin.cpp:131](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-198/src/core/RemotePlugin.cpp:131)).
`<atomic>` was already included by the header; the constructor's `m_quit(false)`
initialiser and every `stop()`/`reset()`/`run()` source line are unchanged in
form (an atomic stores/loads through the same expressions). No audio-thread path
is involved (the watcher thread plus the main thread's destructor path), and the
atomic is lock-free, so nothing realtime-hostile was added.

The test seam mirrors the established pattern of
`include/ProjectRenderer.h:40,133` for cluster new-p9-370: under `LMMS_TESTING`
the header forward-declares the regression class
([include/RemotePlugin.h:47](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-198/include/RemotePlugin.h:47))
and `ProcessWatcher` friends it
([include/RemotePlugin.h:96](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-198/include/RemotePlugin.h:96)).

The regression in
[tests/src/core/RemotePluginAudioPortsTest.cpp:272](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-198/tests/src/core/RemotePluginAudioPortsTest.cpp:272)
asserts that `ProcessWatcher::m_quit` is a `std::atomic<bool>` through a
fail-closed detection template (lines 249-253): `decltype(T::m_quit)` is only
substitutable when the friend seam is present, and the type check only passes
when the flag is atomic. That makes the test fail **at run time** in every
broken state - original code (no seam, `volatile`), or the whole product patch
reverted - instead of failing to compile, which would have let a stale test
binary pass. Verified: the test compiled and failed (exit 8) before the fix and
after the production patch was reverted; it passed (exit 0) with the fix.

## Evidence

All ctest invocations ran from `build/tests`. The affected ctest family
(`ctest -R 'RemotePlugin|ZynSeparateProcess'`) contains 3 tests; the fixed runs
executed 3/3 successfully.

| Check | Command (exact) | Exit/result |
|---|---|---|
| RED before fix - build (`build-1.log`) | `cmake --build build --target RemotePluginAudioPortsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-198.r1/logs/build-1.log 2>&1; echo EXIT=$?` | 0 |
| RED before fix (`test-1.log`) | `ctest -R '^RemotePluginAudioPortsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-198.r1/logs/test-1.log 2>&1; echo EXIT=$?` | **8**; `processWatcherQuitFlagIsAtomic()` - `'( WatcherQuitFlagIsAtomic<ProcessWatcher>::value )' returned FALSE` |
| Fixed build, every target including the header (`build-2.log`) | `cmake --build build --target zene lmmsobjs RemotePluginAudioPortsTest RemotePluginClientE2ETest FakeRemotePluginClient ZynSeparateProcessTest vestige vsteffect ZynAddSubFxCore RemoteZynAddSubFx RemoteVstPlugin64 NativeLinuxRemoteVstPlugin64 > .../build-2.log 2>&1; echo EXIT=$?` | 0 |
| GREEN after fix (`test-2.log`) | `ctest -R '^RemotePluginAudioPortsTest$' --output-on-failure > .../test-2.log 2>&1; echo EXIT=$?` | **0**; 1/1 passed |
| GREEN, affected family (`test-3.log`) | `ctest -R 'RemotePlugin\|ZynSeparateProcess' --output-on-failure > .../test-3.log 2>&1; echo EXIT=$?` | **0**; 3/3 passed (RemotePluginAudioPortsTest, RemotePluginClientE2ETest, ZynSeparateProcessTest) |
| PROOF: remove production patch (`fix.patch`) | `git diff -- include/RemotePlugin.h > .../fix.patch && git apply -R .../fix.patch; echo EXIT=$?` | 0 |
| PROOF: rebuild without the fix (`build-3.log`) | `cmake --build build --target zene RemotePluginAudioPortsTest > .../build-3.log 2>&1; echo EXIT=$?` | 0 |
| PROOF: RED without the fix (`test-4.log`) | `ctest -R '^RemotePluginAudioPortsTest$' --output-on-failure > .../test-4.log 2>&1; echo EXIT=$?` | **8**; same assertion FALSE |
| PROOF: restore patch + rebuild (`build-4.log`) | `git apply .../fix.patch; echo EXIT=$?` then `cmake --build build --target zene RemotePluginAudioPortsTest > .../build-4.log 2>&1; echo EXIT=$?` | 0; 0 |
| PROOF: GREEN with the fix again (`test-5.log`) | `ctest -R '^RemotePluginAudioPortsTest$' --output-on-failure > .../test-5.log 2>&1; echo EXIT=$?` | **0**; 1/1 passed |
| Final dependent rebuild (`build-5.log`) | `cmake --build build --target zene lmmsobjs RemotePluginAudioPortsTest RemotePluginClientE2ETest FakeRemotePluginClient ZynSeparateProcessTest vestige vsteffect ZynAddSubFxCore RemoteZynAddSubFx RemoteVstPlugin64 NativeLinuxRemoteVstPlugin64 > .../build-5.log 2>&1; echo EXIT=$?` | 0 |
| Final family run (`test-6.log`) | `ctest -R 'RemotePlugin\|ZynSeparateProcess' --output-on-failure > .../test-6.log 2>&1; echo EXIT=$?` | **0**; 3/3 passed |
| Complexity gate (`gate-1.log`) | `bash tests/complexity-gate.sh --check > .../gate-1.log 2>&1; echo EXIT=$?` | **1** - pre-existing, unrelated (below) |
| File-length gate (`gate-2.log`) | `bash tests/file-length-gate.sh --check > .../gate-2.log 2>&1; echo EXIT=$?` | **1** - pre-existing, unrelated (below) |

`...` is `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-198.r1/logs`.

## Gate exits: red for reasons that pre-date this cluster

Both required gates were run unpiped after the final code and ledger edit; both
exited **1**. The failures are provably independent of this change:

- `git diff HEAD --name-only` shows this cluster changed exactly three files:
  `include/RemotePlugin.h`, `tests/src/core/RemotePluginAudioPortsTest.cpp`,
  `tests/upstream-modifications.txt`.
- Both gates measure only their scope manifest, `tests/fork-sources.txt`
  (`SOURCES="$HERE/fork-sources.txt"`, complexity-gate.sh:59 and
  file-length-gate.sh:77). Neither changed source is in that manifest
  (`rg 'RemotePlugin' tests/fork-sources.txt` -> no match), so neither gate ever
  reads this cluster's files, and `rg RemotePlugin gate-1.log gate-2.log` -> no
  match: none of the 6 complexity or 14 file-length REGRESSION lines concern
  this change.
- The cited violations are committed files of the train-5 merge wave, e.g.
  `tests/src/core/VideoTrackTest.cpp` (589 lines) entered in merge `337ee790d`,
  while `tests/file-length-baseline.tsv` was last maintained by `ba66cdb87` and
  `tests/complexity-baseline.tsv` by `40104e236`; `tests/src/gui/XyPadPairTest.cpp`
  (CCN 16) entered via `7a61ab54d`/`7cab3b650`.
- Per the standing rules this cluster did not touch gate limits or baselines and
  did not change unrelated code; the failures need the owning lanes' refactors or
  a maintainer's recorded re-anchor.

## Files committed

- `include/RemotePlugin.h`
- `tests/src/core/RemotePluginAudioPortsTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p12-198.md`
