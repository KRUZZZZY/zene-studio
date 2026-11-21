# cluster new-p12-144

**Verdict: BLOCKED.** The master listen bug is reproduced and fixed, and the focused regression passes with the fix. The required ratchet gates still fail on unrelated existing branch regressions, so the prescribed gates are not green.

## Evidence

- `src/core/Mixer.cpp:2257` selects the sample-exact master fader buffer; `src/core/Mixer.cpp:2264` applies it while accumulating into the output. The source buffer stays pre-fader for the listen pass at `src/core/Mixer.cpp:2276`.
- `tests/src/core/MixerListenTest.cpp:106` publishes a constant sample-exact master fader value. `tests/src/core/MixerListenTest.cpp:191` checks AFL against half the unity-fader mix, and `tests/src/core/MixerListenTest.cpp:208` checks PFL against the full mix.
- The original implementation failed that AFL assertion at `tests/src/core/MixerListenTest.cpp:203`. The same assertion failed after removing only the production patch, then passed after restoring it.
- `ctest -R '^MixerListenTest$'` executed one registered CTest target (`1/1`). Its fixture contains four functional slots: the three existing listen tests and the new master-listen regression.
- `src/core/Mixer.cpp` is declared as an inherited modification at `tests/upstream-modifications.txt:2428` with the reason for this change. The test file is already registered in `tests/fork-sources.txt`.

## Command evidence

All build and test logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-144.r1/logs`.

| Checkpoint | Exact command | Exit |
|---|---|---:|
| Initial test-target build | `cmake --build build --target MixerListenTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-144.r1/logs/build-1.log 2>&1; result=$?; echo EXIT=$result` | 0 |
| Initial zene and test rebuild | `cmake --build build --target zene MixerListenTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-144.r1/logs/build-2.log 2>&1; result=$?; echo EXIT=$result` | 0 |
| Static-fader exploratory test (not accepted as RED) | `ctest -R '^MixerListenTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-144.r1/logs/test-1.log 2>&1; result=$?; echo EXIT=$result` | 0 |
| Sample-exact regression build before fix | `cmake --build build --target zene MixerListenTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-144.r1/logs/build-3.log 2>&1; result=$?; echo EXIT=$result` | 0 |
| **RED** | `ctest -R '^MixerListenTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-144.r1/logs/test-2.log 2>&1; result=$?; echo EXIT=$result` | **8** |
| Remove production patch for QA proof | `git diff -- src/core/Mixer.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-144.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-144.r1/logs/fix.patch; result=$?; echo EXIT=$result` | 0 |
| Rebuild without production patch | `cmake --build build --target zene MixerListenTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-144.r1/logs/build-4.log 2>&1; result=$?; echo EXIT=$result` | 0 |
| Regression without production patch | `ctest -R '^MixerListenTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-144.r1/logs/test-3.log 2>&1; result=$?; echo EXIT=$result` | 8 |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-144.r1/logs/fix.patch; result=$?; echo EXIT=$result` | 0 |
| GREEN build | `cmake --build build --target zene MixerListenTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-144.r1/logs/build-5.log 2>&1; result=$?; echo EXIT=$result` | 0 |
| **GREEN** | `ctest -R '^MixerListenTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-144.r1/logs/test-4.log 2>&1; result=$?; echo EXIT=$result` | **0** |
| Complexity gate, sequential rerun | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-144.r1/logs/gate-3.log 2>&1; result=$?; echo EXIT=$result` | **1** |
| File-length gate, sequential rerun | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-144.r1/logs/gate-4.log 2>&1; result=$?; echo EXIT=$result` | **1** |

The complexity gate reports six over-target functions in `XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`, `ControlCommandsMixerTools.cpp`, `ControlCommandsMixerStrip.cpp`, `ProjectRevisions.cpp`, and `ControlCommandsShortcuts.cpp`. The file-length gate reports fourteen files over 500 lines, none among the files changed here. These checks were not re-anchored and unrelated files were left untouched.

## Files in the requested commit

- `src/core/Mixer.cpp`
- `tests/src/core/MixerListenTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p12-144.md`
