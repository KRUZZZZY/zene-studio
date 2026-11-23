ALREADY-FIXED

Cluster 4297 — "Starting a second Neural Amp load can freeze the GUI"
(`plugins/NeuralAmp/NeuralAmpEffect.cpp`, historical line 101).

## Decision

The finding describes `setModelPath()` joining the previous worker synchronously before
starting the next one, called from the control dialog on the GUI thread. That was the
code of `loadWorker`-per-request; it was replaced on this branch before this job started,
by commit `bfce7381fc` "feat(neuralamp): selecting a model returns at once; the last
request wins (Q050-030)" (2026-10-12), whose diff deletes exactly that join:

```
-	// A previous load must be finished before starting another one, otherwise
-	// two workers could race on the published pointer.
 	if (m_loadThread.joinable())
 	{
 		m_loadThread.join();
 	}
-	m_loadThread = std::thread(&NeuralAmpEffect::loadWorker, this, path);
```

Current code, `plugins/NeuralAmp/NeuralAmpEffect.cpp:118-141`:

```cpp
void NeuralAmpEffect::setModelPath(const QString& path)
{
	// Record the request and return: the caller (the GUI thread) never waits for a load. A request made
	// while the worker is busy replaces the one still waiting, so the last request is the one that wins.
	std::lock_guard<std::mutex> lock(m_loadMutex);
	if (m_shuttingDown) { return; }
	m_pendingPath = path;
	m_hasPending = true;
	if (m_workerRunning) { return; }          // line 126: the second load never joins the first
	// The previous worker has cleared m_workerRunning as its last locked act, so this join is only
	// the thread's final return.
	if (m_loadThread.joinable())
	{
		m_loadThread.join();                  // lines 130-133: reached only when no worker runs
	}
	m_workerRunning = true;
	m_loadThread = std::thread(&NeuralAmpEffect::workerLoop, this);
}
```

The only join on the selecting path is lines 130-133, and it is reached only when
`m_workerRunning` is already false — a flag the worker clears inside `m_loadMutex` as its
last locked act before returning (`workerLoop`, lines 138-155, `m_workerRunning = false`
at line 148). A load in progress therefore never makes the caller wait: the request is
recorded and dropped into the pending slot, and `workerLoop` picks up the newest path in
its next iteration, discarding superseded ones.

`grep -rn "m_loadThread\|setModelPath\|workerLoop" plugins/NeuralAmp/*` shows the whole
surface: callers `plugins/NeuralAmp/NeuralAmpControlDialog.cpp:108` (the dialog's Load /
Browse handler), `plugins/NeuralAmp/NeuralAmpControls.cpp:51` (project load) and
`:61-65` (`NeuralAmpControls::setModelPath`), plus the constructor's optional
`LMMS_NAM_MODEL` default at `NeuralAmpEffect.cpp:84`. All of them go through the method
above. The second and last join in the file is the destructor's
(`NeuralAmpEffect.cpp:93-111`), which is not on the "choose another model" path.

The effect's guard test already exists on this branch:
`tests/src/plugins/NeuralAmpModelLoadTest.cpp:198`
`selectingWhileASlowLoadRunsReturnsPromptly`, registered at `tests/CMakeLists.txt:1138`
and compiled with the test-only seam `ZENE_NAM_TEST_SEAM` (`tests/CMakeLists.txt:3079-3092`).
I did not write it (commit `bcfa3a83a6`); I ran it, and I ran it again with the blocking
join reinstated to show that it catches this defect.

## Evidence run this session

Log folder `/home/kruzzzzy/.config/kads/zene/state/runs/4297.r1/logs`.

Green, as the branch stands:

```sh
cmake --build build --target NeuralAmpModelLoadTest -j4      # logs/build-1.log  EXIT=0
cd build/tests && ctest -R NeuralAmpModelLoadTest --output-on-failure   # logs/test-1.log  EXIT=0
# 1/1 Test #396: NeuralAmpModelLoadTest ... Passed  2.28 sec
```

Red, with the pre-fix behaviour reinstated (temporary, reverted below). The patch is
`logs/red-revert.patch`: `std::lock_guard` becomes `std::unique_lock` and the early
return at line 126 becomes a join of the running worker — the removed `bfce7381fc` code.

```sh
git diff -- plugins/NeuralAmp/NeuralAmpEffect.cpp > logs/red-revert.patch   # DIFFEXIT=0
cmake --build build --target NeuralAmpModelLoadTest neuralamp -j4            # logs/build-2-red.log  EXIT=0
```

The guard test `selectingWhileASlowLoadRunsReturnsPromptly` then never returns: the
selecting call blocks in the join while the gate holds the first load open. I let it run
12 s, captured the threads with gdb, and terminated it (`logs/test-2-red.log`):

```
Received signal 15 (SIGTERM) sent by PID 939 UID 1000
         selectingWhileASlowLoadRunsReturnsPromptly function time: 11519ms, total time: 12006ms
Thread 1 (Thread ... "NeuralAmpModelL"):
#8  std::thread::join() from libstdc++.so.6
#9  lmms::NeuralAmpEffect::setModelPath(QString const&)
#10 NeuralAmpModelLoadTest::selectingWhileASlowLoadRunsReturnsPromptly()
...
Thread 20: beforeLoad(QString const&)  <- the worker still parked in the held load
           lmms::NeuralAmpEffect::loadOne(QString const&)
           lmms::NeuralAmpEffect::workerLoop()
TEST_EXIT=143
```

So the guard's red exit is 143 (hung in `setModelPath`'s join, killed after 11.5 s in the
function). Restored and rebuilt:

```sh
git apply -R logs/red-revert.patch                                  # APPLY_REVERSE_EXIT=0
cmake --build build --target NeuralAmpModelLoadTest neuralamp -j4    # logs/build-3-restore.log  EXIT=0
cd build/tests && ctest -R NeuralAmpModelLoadTest --output-on-failure # logs/test-5-green-after-restore.log  EXIT=0
git status --short                                                   # empty
```

`ctest -R NeuralAmp` family, all eight tests:

```sh
cd build/tests && ctest -R NeuralAmp --output-on-failure   # logs/test-3-green.log   EXIT=8 (first attempt)
```

That first attempt reported seven `Not Run`: `NeuralAmpWavInputTest`,
`NeuralAmpProfileArgumentsTest`, `NeuralAmpAllocProbeArgumentsTest`,
`NeuralAmpCompareRateTest`, `NeuralAmpWavRunSampleRateWarning`,
`NeuralAmpHarnessRateValidation`, `NeuralAmpHarnessBlockCount` are `EXCLUDE_FROM_ALL`
binaries that this build tree had never built; the one registered Qt test,
`NeuralAmpModelLoadTest`, passed in that same run. Built them, then reran:

```sh
cmake --build build --target NeuralAmpWavInputTest NeuralAmpProfileArgumentsTest \
    NeuralAmpAllocProbeArgumentsTest NeuralAmpHarnessBlockCount -j4     # logs/build-4-harness.log  EXIT=0
cd build/tests && ctest -R NeuralAmp --output-on-failure                # logs/test-4-green-family.log  EXIT=0
# 100% tests passed out of 8
```

Gates:

```sh
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>   # logs/gates-1.log  EXIT=0
#   PASS  fork-sources / all-sources-reproduce / no-upstream-regression / check-namespace
#   PASS  no-tautology / doc-figures / complexity / file-length
#   RESULT: PASS
```

## Self-check

- **The regression test FAILS with your product change reverted and PASSES with it (exit
  codes).** I made no product change; the fix is `bfce7381fc`. Instead I reverted the
  fix's behaviour: with the blocking join reinstated the guard test hangs inside
  `NeuralAmpEffect::setModelPath` (`logs/test-2-red.log`, stack above), exit 143 when
  terminated after 11.5 s in the function; with the branch as committed it passes, exit 0
  (`logs/test-1.log`), and the whole `-R NeuralAmp` family passes, exit 0
  (`logs/test-4-green-family.log`).
- **The test is deterministic: no wall-clock timing, sleeps or races.** The test's
  synchronisation is the injected seam `s_testBeforeLoad`
  (`plugins/NeuralAmp/NeuralAmpEffect.h:126-132`, compiled in only for this executable via
  `ZENE_NAM_TEST_SEAM`), which runs on the worker before a file is parsed; the test closes
  the gate, waits for `holding == 1` through that seam, and releases it explicitly. The
  150 ms assertion in that pre-existing test (commit `bcfa3a83a6`) is not mine and is not
  the mechanism my red/green evidence relies on — my red run froze *before* that assertion
  (function time 11.5 s, no test result printed).
- **The fix is at the cause and complete: every caller and sibling checked.** Callers of
  `setModelPath`: `NeuralAmpControlDialog.cpp:108`, `NeuralAmpControls.cpp:51` and
  `:61-65`, and the constructor's `LMMS_NAM_MODEL` default at `NeuralAmpEffect.cpp:84` —
  all go through the non-blocking method at `NeuralAmpEffect.cpp:118-141`, none joins.
  The sibling join in the same file is the destructor's (`:93-111`), deliberately bounded
  by one parse and documented as such in the header; see Other findings.
  `NeuralAmpControls::setModelPath`/`loadSettings` only record the path and forward it.
- **No collateral damage: old projects and saved data load as before.** No product file
  changes in this job (`git status --short` empty, and the only commit is this report).
  The saved attribute is untouched: `NeuralAmpControls::saveSettings` still writes
  `model`, `loadSettings` still reads it and forwards it, including the empty case that
  clears the model (`NeuralAmpControls.cpp:40-51`), which the guard test covers in
  `loadingASettingWithoutAModelClearsTheLoadedModel`.
- **ALREADY-FIXED: the code that shows it.** `plugins/NeuralAmp/NeuralAmpEffect.cpp:126`
  returns before the join while a worker runs, and `:130-133` joins only a worker that has
  already cleared `m_workerRunning` (`:148`). Introduced by `bfce7381fc`, guarded by
  `tests/src/plugins/NeuralAmpModelLoadTest.cpp:198`.
- **Sibling commands in the command files "you changed".** This job's commit changes one
  docs file, `docs/qwen-fixes-2026-10/cluster-4297.md`; no command file, no
  `ControlCommands`, no `ControlReversibilityTable` and no registry entry is touched
  (`git show --name-only HEAD`). The `app.*` / `arrangement.*` sibling list in the
  pre-submit check comes from the branch diff against the fix base (429 files from the
  train), not from this job: `git show --stat HEAD` lists the report alone. No control
  command was added or changed, so no `ControlCommandsSnapshot` canary moves.
- **Realtime, undo, commands.** No audio-thread code is touched:
  `processImpl` (`NeuralAmpEffect.cpp:327-371`) still only stores the `m_audioActive` flag,
  loads the published pointer and runs the model — no allocation, lock, wait or log. No
  control command, no reversibility class and no undo behaviour changes.

## Other findings

- The destructor (`plugins/NeuralAmp/NeuralAmpEffect.cpp:93-111`) joins the worker, and a
  parse already under way cannot be interrupted, so deleting the effect (or closing the
  project) while a large model loads still waits for that one file on the destroying
  thread. That is a different scenario from this finding ("choose another model"), it is
  stated as intended in the destructor comment and in the header's threading note, and the
  guard test pins it (`rapidSelectionsAndDeletionMidLoadLeaveNoWorker`). Left unchanged.
- `plugins/NeuralAmp/NeuralAmpEffect.cpp:292-310` (`publishModel`) spin-waits for the audio
  thread to leave `processImpl` before freeing the replaced model. It runs on the worker
  thread only, never on the GUI thread; noted, not changed.
