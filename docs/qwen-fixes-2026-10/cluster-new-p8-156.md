# Cluster new-p8-156: stale deferred MIDI learn

Verdict: **FIXED**

`setFocusTarget()` advances a focus generation. A MIDI-thread request records that generation, and `applyPendingBinding()` drops the request when focus changed before the drain. The GUI disarm path clears focus in `MidiLearnGui::syncArmedState()`, so a pending CC from the earlier learn cannot bind a newly focused control.

Evidence in the source:

- [`include/MidiLearn.h`](../../include/MidiLearn.h:125) declares the request generation and atomic slot field.
- [`src/core/MidiLearn.cpp`](../../src/core/MidiLearn.cpp:88) advances the generation; lines 180-203 capture and queue it; lines 209-228 compare it before binding.
- [`src/gui/MidiLearnGui.cpp`](../../src/gui/MidiLearnGui.cpp:84) clears focus on disarm.
- [`tests/src/core/MidiLearnThreadTest.cpp`](../../tests/src/core/MidiLearnThreadTest.cpp:243) queues a MIDI-thread CC, disarms, rearms with a new target, and verifies the stale request does not bind.

## Verification

All build logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-156.r1/logs/`. Commands below ran from the worktree unless a test command says `build/tests`.

| Check | Exact command | Exit | Evidence |
| --- | --- | ---: | --- |
| Initial test build | `cmake --build build --target MidiLearnThreadTest` | 0 | `build-1.log` |
| RED on original code | `ctest -R '^MidiLearnThreadTest$' --output-on-failure` (`build/tests`) | 8 | `test-1.log`; regression assertion failed because the stale request bound the new target. |
| Fixed build | `cmake --build build --target zene MidiLearnThreadTest` | 0 | `build-2.log` |
| GREEN after fix | `ctest -R '^MidiLearnThreadTest$' --output-on-failure` (`build/tests`) | 0 | `test-2.log` |
| Reverse production patch | `git diff -- include/MidiLearn.h src/core/MidiLearn.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-156.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-156.r1/logs/fix.patch` | 0 | The exact required reverse proof command. |
| Reversed build | `cmake --build build --target zene MidiLearnThreadTest` | 0 | `build-3.log` |
| RED with production patch reversed | `ctest -R '^MidiLearnThreadTest$' --output-on-failure` (`build/tests`) | 8 | `test-3.log`; the same regression assertion failed. |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-156.r1/logs/fix.patch` | 0 | Production diff restored. |
| Restored build | `cmake --build build --target zene MidiLearnThreadTest` | 0 | `build-4.log` |
| GREEN after restore | `ctest -R '^MidiLearnThreadTest$' --output-on-failure` (`build/tests`) | 0 | `test-4.log` |
| Family count | `ctest -V -R '^MidiLearnThreadTest$'` (`build/tests`) | 0 | `test-5.log`: 5 QtTest cases passed, 0 failed; CTest ran 1/1 registered test. |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 | `gate-1.log` |
| File length gate | `bash tests/file-length-gate.sh --check` | 0 | `gate-2.log` |
| Whitespace check | `git diff --check` | 0 | No output. |

The final tree was also checked with both required gates after this report was written; both exited 0.

## Files committed

- `include/MidiLearn.h`
- `src/core/MidiLearn.cpp`
- `tests/src/core/MidiLearnThreadTest.cpp`
- `tests/fork-sources.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p8-156.md`
