# Cluster new-p12-139: `m_quit` data race

**Verdict: BLOCKED.** The reported race is fixed and the regression is verified RED/GREEN, including a patch reversal. The required complexity and file-length gates both exit 1 on regressions in files outside this fix. The working-tree diff contains only the JACK fix, its regression, upstream ledger entries, and this evidence report; no gate baseline was changed.

## Finding and fix evidence

- `include/MidiJack.h:119` declared `m_quit` as `volatile bool`.
- `src/core/midi/MidiJack.cpp:149` wrote it in the destructor while `src/core/midi/MidiJack.cpp:268` read it in `run()`.
- The field is now `std::atomic<bool>`. The destructor uses `store(true, std::memory_order_relaxed)` and the worker uses `load(std::memory_order_relaxed)`. The flag only communicates shutdown, so no other data needs ordering.
- `tests/src/core/MidiJackOutScheduleTest.cpp:79` checks the actual field type. It fails on the original declaration and after reversing the production patch.
- `tests/upstream-modifications.txt` records the changes to both inherited production files. The existing test is already registered in `tests/fork-sources.txt`.

## Validation evidence

The affected CTest family ran **1 test** on every invocation (never zero). The QtTest RED output reported 5 passed and 1 failed; the failing assertion was `quitFlagIsAtomic()`. GREEN reported 100% passed.

| Stage | Exact command | Exit |
| --- | --- | ---: |
| Original test target build | `cmake --build build --target MidiJackOutScheduleTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-139.r1/logs/build-2.log 2>&1; echo EXIT=$?` | 0 |
| Original-source RED, cwd `build/tests` | `QT_QPA_PLATFORM=offscreen ctest -R '^MidiJackOutScheduleTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-139.r1/logs/test-red-1.log 2>&1; echo EXIT=$?` | 8 |
| Fixed build (`zene` and test) | `cmake --build build --target zene MidiJackOutScheduleTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-139.r1/logs/build-3.log 2>&1; echo EXIT=$?` | 0 |
| Fixed-source GREEN, cwd `build/tests` | `QT_QPA_PLATFORM=offscreen ctest -R '^MidiJackOutScheduleTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-139.r1/logs/test-green-1.log 2>&1; echo EXIT=$?` | 0 |
| Remove production patch | `git diff -- include/MidiJack.h src/core/midi/MidiJack.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-139.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-139.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Reversed-source rebuild | `cmake --build build --target zene MidiJackOutScheduleTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-139.r1/logs/build-4.log 2>&1; echo EXIT=$?` | 0 |
| Reversed-source RED, cwd `build/tests` | `QT_QPA_PLATFORM=offscreen ctest -R '^MidiJackOutScheduleTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-139.r1/logs/test-red-2.log 2>&1; echo EXIT=$?` | 8 |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-139.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Final rebuild (`zene` and test) | `cmake --build build --target zene MidiJackOutScheduleTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-139.r1/logs/build-5.log 2>&1; echo EXIT=$?` | 0 |
| Final GREEN, cwd `build/tests` | `QT_QPA_PLATFORM=offscreen ctest -R '^MidiJackOutScheduleTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-139.r1/logs/test-green-2.log 2>&1; echo EXIT=$?` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-139.r1/logs/gate-1.log 2>&1; echo EXIT=$?` | 1 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-139.r1/logs/gate-2.log 2>&1; echo EXIT=$?` | 1 |

The complexity gate reports six new over-limit functions in `tests/src/gui/XyPadPairTest.cpp`, `src/core/DawProjectReadTracks.cpp`, `src/core/ControlCommandsMixerTools.cpp`, `src/core/ControlCommandsMixerStrip.cpp`, `src/core/ProjectRevisions.cpp`, and `src/gui/ControlCommandsShortcuts.cpp`. The file-length gate reports 14 new over-500-line files, including `src/core/SmfInterchangeReader.cpp`, `src/core/TakeLane.cpp`, and tests under `tests/src/core/` and `tests/src/gui/`. None is in the changed-file list for this fix. These failures need separate work; their limits and baselines were left untouched.

## Files in this fix commit

- `include/MidiJack.h`
- `src/core/midi/MidiJack.cpp`
- `tests/src/core/MidiJackOutScheduleTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p12-139.md`
