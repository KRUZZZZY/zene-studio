# Cluster new-p9-346

Verdict: **FIXED**

`PatchesDialog::setBankProg()` returned without updating the bank and program models. Those models drive `GigInstrument::updatePatch()` through the existing connections in `plugins/GigPlayer/GigPlayer.cpp:98-99,238`. The dialog now updates both models during preview. It snapshots the original values at `plugins/GigPlayer/PatchesDialog.cpp:125-126` and restores them on Cancel at line 275.

The regression in `tests/src/plugins/PluginPortsMigrationTest.cpp:737` opens the real GIG patch dialog, selects a program row, checks that preview reaches the instrument models, then checks that Cancel restores the original values. The configured tree has no standalone `PatchesDialog` CTest; the affected family is `PluginPortsMigrationTest`, CTest #313.

## Evidence

Build and test output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-346.r1/logs`. The exact command is shown below; each command's output was redirected to the named log and followed by an `EXIT=<status>` line. The `fff` grep tool was not exposed in this session, so I searched the captured logs with `rg -n` using the requested terms.

| Phase | Command | Log | EXIT |
|---|---|---|---:|
| Initial test build (compile issue in test harness, corrected) | `cmake --build build --target zene gigplayer PluginPortsMigrationTest` | `build-red-1.log` | 2 |
| Test build after the compile correction | `cmake --build build --target zene gigplayer PluginPortsMigrationTest` | `build-red-2.log` | 0 |
| Initial CTest attempt (test harness view-parent crash, corrected) | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | `test-red-1.log` | 8 |
| Test build after the view-parent correction | `cmake --build build --target zene gigplayer PluginPortsMigrationTest` | `build-red-3.log` | 0 |
| **RED:** baseline code leaves the model unchanged; `previewApplied` assertion fails | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | `test-red-2.log` | **8** |
| Build with fix | `cmake --build build --target zene gigplayer partc_ref_gigplayer PluginPortsMigrationTest` | `build-green-1.log` | 0 |
| **GREEN:** CTest executes 1/1 affected-family target | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | `test-green-1.log` | **0** |
| Reverse product patch for negative control | `git diff -- plugins/GigPlayer/PatchesDialog.cpp plugins/GigPlayer/PatchesDialog.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-346.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-346.r1/logs/fix.patch` | `proof-reverse-apply.log` | 0 |
| Rebuild without fix | `cmake --build build --target zene gigplayer partc_ref_gigplayer PluginPortsMigrationTest` | `build-reverse-1.log` | 0 |
| **Negative control:** the same preview assertion fails without the product patch | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | `test-reverse-1.log` | **8** |
| Restore product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-346.r1/logs/fix.patch` | `proof-restore-apply.log` | 0 |
| Rebuild restored fix | `cmake --build build --target zene gigplayer partc_ref_gigplayer PluginPortsMigrationTest` | `build-restore-1.log` | 0 |
| **Restored GREEN:** same CTest target passes 1/1 | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | `test-restore-1.log` | **0** |
| Complexity gate | `bash tests/complexity-gate.sh --check` | `gate-complexity-3.log` | **0** |
| File-length gate | `bash tests/file-length-gate.sh --check` | `gate-file-length-3.log` | **0** |

## Files committed

- `plugins/GigPlayer/PatchesDialog.cpp`
- `plugins/GigPlayer/PatchesDialog.h`
- `tests/src/plugins/PluginPortsMigrationTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-346.md`
