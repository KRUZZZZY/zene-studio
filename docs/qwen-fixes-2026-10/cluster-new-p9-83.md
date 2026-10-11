# Cluster new-p9-83: null controller dereference

**Verdict: FIXED**

## Evidence

- [src/core/ControllerConnection.cpp:122] now inspects the resolved `m_controller` after the null fallback. The following connection and ownership bookkeeping also uses that resolved pointer.
- [tests/src/gui/MidiControllerMenuTest.cpp:116] adds `settingNullControllerRestoresDummy`, which sets a MIDI connection to null and verifies the shared Dummy controller is installed.
- [tests/upstream-modifications.txt:1054] records the inherited production-file change and its regression test.
- The original code and the patch-reversed code both segfaulted in the new slot. The fixed code passed, and passed again after restoring the exact patch.
- The affected CTest family selects one target: `MidiControllerMenuTest`. Each RED run was 0/1; each GREEN run was 1/1.

The fff grep tool was not exposed in this environment (`command -v fff` returned no path), so I searched the required log terms with `rg` in the specified log folder.

## Commands and exit codes

Commands below were run from the worktree root unless the command is a CTest command, which was run from `build/tests`. Build and test output was redirected to the named log.

### Original-code RED

```sh
cmake --build build --target MidiControllerMenuTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-83.r1/logs/build-1.log' 2>&1; result=$?; echo EXIT=$result
EXIT=0
cmake --build build --target zene > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-83.r1/logs/build-2.log' 2>&1; result=$?; echo EXIT=$result
EXIT=0
ctest -R '^MidiControllerMenuTest$' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-83.r1/logs/test-1.log' 2>&1; result=$?; echo EXIT=$result
EXIT=8
```

CTest ran 1/1 target and reported a segmentation fault.

### Fixed-code GREEN

```sh
cmake --build build --target zene MidiControllerMenuTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-83.r1/logs/build-3.log' 2>&1; result=$?; echo EXIT=$result
EXIT=0
ctest -R '^MidiControllerMenuTest$' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-83.r1/logs/test-2.log' 2>&1; result=$?; echo EXIT=$result
EXIT=0
```

CTest passed 1/1 target.

### Patch-reversal proof

```sh
git diff -- src/core/ControllerConnection.cpp > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-83.r1/logs/fix.patch' && git apply -R '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-83.r1/logs/fix.patch'; result=$?; echo EXIT=$result
EXIT=0
cmake --build build --target zene MidiControllerMenuTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-83.r1/logs/build-4.log' 2>&1; result=$?; echo EXIT=$result
EXIT=0
ctest -R '^MidiControllerMenuTest$' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-83.r1/logs/test-3.log' 2>&1; result=$?; echo EXIT=$result
EXIT=8
git apply '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-83.r1/logs/fix.patch'; result=$?; echo EXIT=$result
EXIT=0
cmake --build build --target zene MidiControllerMenuTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-83.r1/logs/build-5.log' 2>&1; result=$?; echo EXIT=$result
EXIT=0
ctest -R '^MidiControllerMenuTest$' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-83.r1/logs/test-4.log' 2>&1; result=$?; echo EXIT=$result
EXIT=0
```

After reversal, CTest ran 1/1 target and reported a segmentation fault. After patch restoration, it passed 1/1 target.

### Required ratchet gates after the final edit

```sh
bash tests/complexity-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-83.r1/logs/gate-3.log' 2>&1; result=$?; echo EXIT=$result
EXIT=0
bash tests/file-length-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-83.r1/logs/gate-4.log' 2>&1; result=$?; echo EXIT=$result
EXIT=0
```

## Files committed

- `src/core/ControllerConnection.cpp`
- `tests/src/gui/MidiControllerMenuTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-83.md`
