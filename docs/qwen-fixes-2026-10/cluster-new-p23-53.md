Verdict: FALSE-POSITIVE

new-p23-53: `isEngineReady()` does not test `Engine::audioEngine()`, but the state the finding needs (audio engine null while song and mixer exist) cannot be reached in this code. No product change is needed.

## Evidence

**The claimed predicate is as described.** `isEngineReady()` returns `s_ready && Engine::getSong() != nullptr && Engine::mixer() != nullptr` ([src/core/ControlRegistry.cpp:205-208](src/core/ControlRegistry.cpp:205)). It does not test the audio engine. `invoke()` gates engine commands on it at [src/core/ControlRegistry.cpp:283](src/core/ControlRegistry.cpp:283).

**Startup cannot leave the audio engine null while song and mixer exist.** `Engine::init()` assigns the singletons in this order: audio engine, then song, then mixer ([src/core/Engine.cpp:71-73](src/core/Engine.cpp:71)). The audio engine is constructed unconditionally first. The only assignments of `s_audioEngine`, `s_song` and `s_mixer` anywhere in `src/` or `tests/` are those three lines, so if the audio engine is null, song and mixer are null too.

**Shutdown clears the mixer before the audio engine.** `Engine::destroy()` deletes the mixer ([src/core/Engine.cpp:112](src/core/Engine.cpp:112)) and then the audio engine ([src/core/Engine.cpp:113](src/core/Engine.cpp:113)). The song goes last ([src/core/Engine.cpp:123](src/core/Engine.cpp:123)). In the window where the audio engine is already null, the mixer is null too, so `isEngineReady()` is already false.

**A device failure keeps the audio engine.** `AudioEngine::initDevices()` assigns the device returned by `tryAudioDevices()` ([src/core/AudioEngine.cpp:785](src/core/AudioEngine.cpp:785)). That function always returns a device: when the configured one fails it sets `m_audioDevStartFailed = true` ([src/core/AudioEngine.cpp:1952](src/core/AudioEngine.cpp:1952)) and returns a new `AudioDummy` on the same `AudioEngine` ([src/core/AudioEngine.cpp:1965-1967](src/core/AudioEngine.cpp:1965)). The engine pointer is never nulled. The device failure is already reported by `readinessReport()` as `audio_device_failed`. That function guards every audio-engine read with a null check ([src/core/ControlSession.cpp:56-57](src/core/ControlSession.cpp:56), [src/core/ControlSession.cpp:88-89](src/core/ControlSession.cpp:88)).

**Conclusion.** In every reachable state, `Engine::mixer() != nullptr` implies `Engine::audioEngine() != nullptr`. Adding the audio check to `isEngineReady()` would change nothing in reachable states, so it would not fix any observable failure. The finding's "when" clause (startup or device failure leaves the audio engine null) does not hold.

**Not run:** no build and no ctest for this cluster, because no product code changed and no fix was made. No RED/GREEN regression exists because there is nothing to prove. The `isEngineReady` predicate was checked by reading the code, not by running it.

## Choice for the owner

The finding's suggested fix (add `audioEngine() != nullptr` to `isEngineReady()` and make `readinessReport()` describe a missing engine) is defence in depth. It would only affect states the code cannot currently produce. If you want that anyway, the minimal version would also need a branch in `readinessReport()` for a null audio engine, because the current `audio_device_failed` branch would otherwise format a null pointer. I did not make that change: the procedure says a not-real verdict stops at the evidence.

## Commands run (worktree `zene-qwen-new-p23-53`, branch `bugs/qwen-new-p23-53`)

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-53.r1/logs/`.

| Step | Command | EXIT |
| --- | --- | --- |
| Feature gate script as specified | `python3 .../qwen/feature_check.py <worktree> > logs/001-feature-check-baseline.log 2>&1` | 1 |
| fork-sources | `bash tests/fork-sources-gate.sh > logs/010-fork-sources.log 2>&1` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh > logs/011-all-sources-reproduce.log 2>&1` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh > logs/012-no-upstream-regression.log 2>&1` | 0 |
| check-namespace | `tests/scripted/check-namespace > logs/013-check-namespace.log 2>&1` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh > logs/014-no-tautology.log 2>&1` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py > logs/015-doc-figures.log 2>&1` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check > logs/016-complexity.log 2>&1` | 1 |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check > logs/017-file-length.log 2>&1` | 1 |

`feature_check.py` exited 1 before running any check. It tries to create `<worktree parent>/<worktree name>-feature-check` and got `PermissionError` on the parent directory, which this lane cannot write. I ran the same gate commands it lists, directly, from the worktree.

The complexity and file-length ratchets report 7 and 22 `REGRESSION` lines (`logs/016`, `logs/017`). Both are the expected train-wide reds. They do not count against this lane, because the branch changes no code file: `git diff --name-only <merge-base>..HEAD` is empty before this commit, and this commit adds only this report. The script's pass rule excludes items outside changed files, so both rows pass by that rule.

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p23-53.md` (this report). No product, test or registration file changed.

## Self-check

Run before this commit: `python3 .../qwen/submit_check.py <worktree> new-p23-53 > logs/020-submit-check.log 2>&1`, EXIT=1. Its items:

- FAIL commits on the branch: nothing committed yet. This commit closes it.
- FAIL working tree clean: only this report was untracked. This commit closes it.
- PASS report present.
- PASS tests: the closure is claim-only, so no test is needed. Evidence is the code citations above; independent QA should verify them.
- FAIL gates (feature_check.py): the script exits 1 before running any check. It writes its logs to `<worktree parent>/<name>-feature-check`, and this lane cannot write that parent directory. The same gate commands were run directly; all six simple gates exit 0 and the two ratchets exit 1 with expected train-wide regressions in files this branch does not change (see "Commands run"). This row is not a PASS from the script's own table.

Answers to the "?" items:

- Regression test (fail reverted, pass with fix): not applicable. The verdict is FALSE-POSITIVE and no product code changed, so there is no fix to revert and no regression test.
- Deterministic: not applicable, no test added.
- Fix at the cause and complete: not applicable, no fix. The callers of `isEngineReady()` are `invoke()` ([src/core/ControlRegistry.cpp:283](src/core/ControlRegistry.cpp:283)), `readinessReport()` ([src/core/ControlSession.cpp:64](src/core/ControlSession.cpp:64)), and `SafeStartTest` ([tests/src/core/SafeStartTest.cpp:196](tests/src/core/SafeStartTest.cpp:196)). None of them depends on the audio check, because it is unreachable as shown above.
- No collateral damage: no product or saved-data behaviour changed.
- FALSE-POSITIVE evidence: the code citations are in the Evidence section above.
