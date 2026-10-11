# Cluster new-p8-246: journal restore erases visibility-set definitions

Verdict: **FIXED**

## Evidence

The finding was real. [`TrackContainer::loadSettings()`](../../src/core/TrackContainer.cpp:142) identifies a journal restore at line 144 and clears/reloads the tracks at line 164. [`clearAllTracks()`](../../src/core/TrackContainer.cpp:355) calls `clearVisibilitySets()` at line 365, clearing the visibility-set vector and active name. Those definitions are saved beside the track container, so a track checkpoint does not reload them.

The journal-only branch now preserves the visibility-set definitions and active name around that clear at [`TrackContainer.cpp:147`](../../src/core/TrackContainer.cpp:147) and [`TrackContainer.cpp:165`](../../src/core/TrackContainer.cpp:165). Ordinary project clearing still resets them. The regression at [`TrackFolderTest.cpp:348`](../../tests/src/core/TrackFolderTest.cpp:348) creates and activates a named set, checkpoints through `transport.set_tempo`, then undoes through `control.undo`; it checks the set members, active name, restored tracks, and visibility flags.

The affected CTest family is `TrackFolderTest`: one CTest test ran in each invocation (1 executed, not zero). The original code failed at `restored != nullptr` with `journal restore erased the named visibility set`; the fixed code passed.

## Commands and results

The fff grep tool was unavailable in this environment, so log checks used `rg` against the specified log folder. Build and test output was redirected to new numbered log files there.

Initial RED on the original production code:

```sh
cmake --build build --target zene TrackFolderTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-246.r1/logs/build-1.log 2>&1; build_rc=$?; echo EXIT=$build_rc
# EXIT=0
ctest -R '^TrackFolderTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-246.r1/logs/test-1.log 2>&1; test_rc=$?; echo EXIT=$test_rc
# EXIT=8; intended assertion failed: restored != nullptr
```

Initial GREEN after the fix:

```sh
cmake --build build --target zene TrackFolderTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-246.r1/logs/build-2.log 2>&1; build_rc=$?; echo EXIT=$build_rc
# EXIT=0
ctest -R '^TrackFolderTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-246.r1/logs/test-2.log 2>&1; test_rc=$?; echo EXIT=$test_rc
# EXIT=0; 100% tests passed, 0 failed out of 1
```

Patch-reversal proof:

```sh
git diff -- src/core/TrackContainer.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-246.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-246.r1/logs/fix.patch
patch_rc=$?
echo EXIT=$patch_rc
# EXIT=0
cmake --build build --target zene TrackFolderTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-246.r1/logs/build-3.log 2>&1; build_rc=$?; echo EXIT=$build_rc
# EXIT=0
ctest -R '^TrackFolderTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-246.r1/logs/test-3.log 2>&1; test_rc=$?; echo EXIT=$test_rc
# EXIT=8; the same intended assertion failed
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-246.r1/logs/fix.patch
patch_rc=$?
echo EXIT=$patch_rc
# EXIT=0
cmake --build build --target zene TrackFolderTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-246.r1/logs/build-4.log 2>&1; build_rc=$?; echo EXIT=$build_rc
# EXIT=0
ctest -R '^TrackFolderTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-246.r1/logs/test-4.log 2>&1; test_rc=$?; echo EXIT=$test_rc
# EXIT=0; 100% tests passed, 0 failed out of 1
```

Ratchet gates:

```sh
bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-246.r1/logs/gate-1.log 2>&1; gate_rc=$?; echo EXIT=$gate_rc
# EXIT=0
bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-246.r1/logs/gate-2.log 2>&1; gate_rc=$?; echo EXIT=$gate_rc
# EXIT=0
```

## Files committed

- `src/core/TrackContainer.cpp`
- `tests/src/core/TrackFolderTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p8-246.md`
