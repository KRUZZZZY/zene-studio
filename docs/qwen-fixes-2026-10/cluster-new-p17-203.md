ALREADY-FIXED

# Cluster new-p17-203: Watsyn wavetable edit and note-start race

## Evidence and decision

This finding is already fixed by ancestor commit `899ae8ea7` on this branch. `WatsynWave` is the immutable four-table snapshot with an atomic holder count ([Watsyn.h](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p17-203/plugins/Watsyn/Watsyn.h:87)). Each `updateWaveA1/A2/B1/B2()` rebuilds its staging table and calls `publishWave()` ([Watsyn.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p17-203/plugins/Watsyn/Watsyn.cpp:666)); publication copies all four tables into a fresh snapshot before atomically exchanging the published pointer, then retires the old snapshot ([Watsyn.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p17-203/plugins/Watsyn/Watsyn.cpp:289)). `playNoteImpl()` pins the published snapshot when a note starts ([Watsyn.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p17-203/plugins/Watsyn/Watsyn.cpp:455)), and rendering reads through that pinned snapshot. `recycleWaves()` returns a snapshot to the GUI-side free list only after both the pin count and in-flight claimer count reach zero ([Watsyn.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p17-203/plugins/Watsyn/Watsyn.cpp:338)). Voice release only decrements its atomic pin; it does not free memory on the audio thread ([Watsyn.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p17-203/plugins/Watsyn/Watsyn.cpp:89)).

The regression test deterministically poisons half of the staging table without publishing it, starts a note, then verifies that note renders the last complete snapshot. Its control edit proves that a completed graph edit changes a new note's output ([WatsynWaveSnapshotTest.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p17-203/tests/src/plugins/WatsynWaveSnapshotTest.cpp:176)). Against the pre-snapshot implementation, the test fails specifically at `sameFrames(late.buffer(), published)`: the historical code copied the mutable `A1_wave` etc. arrays into each synth in `Watsyn.cpp:97-102` and initialized new notes from those arrays in `Watsyn.cpp:422-424`.

No product source file is changed in this cluster because the fix is already in the branch. Therefore the reverse-patch step for a new product diff does not apply; the test was instead run against the exact pre-snapshot `899ae8ea7^` Watsyn source and header, then rebuilt and rerun against the restored current source.

## Test and gate evidence

| Check | Exact command and working directory | Exit | Result |
|---|---|---:|---|
| RED build, pre-snapshot Watsyn sources | From the worktree: `cmake --build build --target WatsynWaveSnapshotTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-203.r3/logs/build-red-1.log 2>&1` | 0 | Historical source compiled with the regression test. |
| RED test, pre-snapshot Watsyn sources | From `build/tests`: `ctest -R '^WatsynWaveSnapshotTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-203.r3/logs/test-red-1.log 2>&1` | 8 | One CTest failed; Qt reported `sameFrames(late.buffer(), published)` false. |
| GREEN build, current sources | From the worktree: `cmake --build build --target zene WatsynWaveSnapshotTest WatsynSelectedGraphTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-203.r3/logs/build-2.log 2>&1` | 0 | Application and both registered Watsyn tests rebuilt. |
| GREEN Watsyn family | From `build/tests`: `ctest -R '^Watsyn' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-203.r3/logs/test-2.log 2>&1` | 0 | 2/2 tests passed: `WatsynSelectedGraphTest` and `WatsynWaveSnapshotTest`. |
| Feature and registration gates | From the worktree: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p17-203 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-203.r3/logs/gate-1.log 2>&1` | 0 | PASS, including fork registration, all-sources reproduction, namespace, no-tautology, docs, complexity, and file length. |

The new test is registered in `tests/CMakeLists.txt` and `tests/fork-sources.txt`. `tests/all-sources.txt` was re-derived from its documented recipe because the train-12 base snapshot included excluded vendored sfizz files; the registration gate now reproduces the manifest.

## Self-check

- **Regression fails without the snapshot implementation and passes with it:** confirmed by building and running the same test against `899ae8ea7^` (build exit 0, CTest exit 8 with the expected assertion), then rebuilding current sources (build exit 0, Watsyn family CTest exit 0; 2 tests passed). The comparison used the historical pre-snapshot source because this cluster has no product-source diff to reverse.
- **Deterministic test:** `aNoteStartedWhileTheWavetableIsMidEditRendersThePublishedWave` injects a fixed half-table poison through a private-state test seam. It uses no timing, sleeps, threads, or race scheduling.
- **Cause and sibling paths:** all four `updateWave*()` functions call `publishWave()`; the only note-start path in `playNoteImpl()` calls `claimWave()`; `deleteNotePluginData()` destroys the inline voice and releases its pin. Publication and recycling are on the graph-edit/control path. The audio path only loads/pins a pointer and decrements its atomic pin on release.
- **Collateral behavior and saved data:** graph serialization, project formats, oscillator limits, commands, and undo behavior were not changed. Existing graph data still populates the same staging arrays before snapshot publication; no new limit or validation was added.
- **Already-fixed evidence:** current `Watsyn.cpp:289-355` publishes complete immutable snapshots and reclaims retired ones only after their users are gone; `Watsyn.cpp:455-490` pins and releases them on the existing note path. The pre-snapshot ancestor source demonstrates the mutable per-note copy that the regression catches.

## Explicit files committed

- `docs/qwen-fixes-2026-10/cluster-new-p17-203.md`
- `tests/CMakeLists.txt`
- `tests/all-sources.txt`
- `tests/fork-sources.txt`
- `tests/src/plugins/WatsynWaveSnapshotTest.cpp`
- `tests/upstream-modifications.txt`
