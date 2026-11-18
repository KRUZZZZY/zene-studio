# Cluster new-p11-37: empty note.follow_chords selection

Verdict: **FIXED**

## Evidence

- [src/core/ControlCommandsNoteHarmony.cpp](../../src/core/ControlCommandsNoteHarmony.cpp#L81): followChords now returns Refused when scope is selection and the selected note list is empty. The guard runs before chord matching or note edits.
- [tests/src/core/NoteToolsTest.cpp](../../tests/src/core/NoteToolsTest.cpp#L188): followingChordsRefusesAnEmptySelection creates a nonempty chord track and a clip with one unselected note, expects Refused, and verifies the note key is unchanged.
- Both changed C++ files were already registered in tests/fork-sources.txt.

## Regression evidence

Commands below ran from build/tests. CTest ran one registered test, NoteToolsTest (1/1). The original and reverted-fix runs each reported 22 Qt test cases passed and the new case failed.

| State | Exact command | Exit |
|---|---|---:|
| Original code, RED (test-1.log) | ctest -R '^NoteToolsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-37.r1/logs/test-1.log 2>&1; echo EXIT=$? | 8 |
| Fixed code, GREEN (test-2.log) | ctest -R '^NoteToolsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-37.r1/logs/test-2.log 2>&1; echo EXIT=$? | 0 |
| Reversed production patch, RED (test-3.log) | ctest -R '^NoteToolsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-37.r1/logs/test-3.log 2>&1; result=$?; echo EXIT=$result | 8 |
| Reapplied production patch, GREEN (test-4.log) | ctest -R '^NoteToolsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-37.r1/logs/test-4.log 2>&1; result=$?; echo EXIT=$result | 0 |

The affected-family count was 1 CTest executable per run (1/1); the regression is the failing Qt test case within it. The final CTest run passed.

## Rebuilds and gates

| Check | Exact command | Exit |
|---|---|---:|
| Baseline build (build-1.log) | cmake --build build --target zene NoteToolsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-37.r1/logs/build-1.log 2>&1; echo EXIT=$? | 0 |
| Fixed-code build used before test-2 (build-3.log) | cmake --build build --target zene NoteToolsTest -- -j8 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-37.r1/logs/build-3.log 2>&1; result=$?; echo EXIT=$result | 0 |
| Reversed-fix build before test-3 (build-4.log) | cmake --build build --target zene NoteToolsTest -- -j8 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-37.r1/logs/build-4.log 2>&1; result=$?; echo EXIT=$result | 0 |
| Restored-fix build before test-4 (build-5.log) | cmake --build build --target zene NoteToolsTest -- -j8 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-37.r1/logs/build-5.log 2>&1; result=$?; echo EXIT=$result | 0 |
| Complexity gate (gate-4.log) | bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-37.r1/logs/gate-4.log 2>&1; result=$?; echo EXIT=$result | 0 |
| File-length gate (gate-5.log) | bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-37.r1/logs/gate-5.log 2>&1; result=$?; echo EXIT=$result | 0 |

One intermediate build invocation (build-2.log) and the first complexity-gate invocation (gate-1.log) yielded without surfacing their exit codes. The captured rebuild and gate reruns listed above are the results used as evidence.

## Files committed

- src/core/ControlCommandsNoteHarmony.cpp
- tests/src/core/NoteToolsTest.cpp
- docs/qwen-fixes-2026-10/cluster-new-p11-37.md
