# Cluster new-p9-327: OpulenZ emulator locking

**Verdict: FIXED.**

`updatePatch()` wrote register `0xBD` and refreshed active voice levels without `emulatorMutex`, while render and MIDI paths use that mutex. It now holds one `QMutexLocker` for the full update and calls `loadPatchLocked()` so the patch-register loop does not lock recursively. Public `loadPatch()` remains a locking wrapper for its other callers.

## File and line evidence

- [OpulenZ.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-327/plugins/OpulenZ/OpulenZ.cpp:492): `loadPatch()` takes the mutex and delegates to `loadPatchLocked()`; the helper writes the per-voice registers at line 497.
- [OpulenZ.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-327/plugins/OpulenZ/OpulenZ.cpp:538): `updatePatch()` takes the scoped mutex before building the patch, writing `0xBD` (573), refreshing active voice levels (579), and writing patch registers through the helper (589).
- [OpulenZ.h](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-327/plugins/OpulenZ/OpulenZ.h:145): declares the private, non-locking helper.
- [PluginPortsMigrationTest.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-327/tests/src/plugins/PluginPortsMigrationTest.cpp:761): adds a deterministic source-contract regression. It fails when `updatePatch()` lacks the scoped lock or calls the locking wrapper, and checks that the helper itself does not re-lock.

The configured test family covering OpulenZ is `PluginPortsMigrationTest` (CTest test #313). Its regression failed on the original source and passed with the fix. The required reverse-patch proof also failed without the product patch and passed after restoration.

## Evidence

| Check | Command and working directory | Exit | Result |
|---|---|---:|---|
| Original RED build | `cmake --build build --target PluginPortsMigrationTest` (worktree root) | 0 | `build-1.log` |
| Original RED test | `./PluginPortsMigrationTest opulenzUpdatePatchLocksEmulatorForWholeUpdate` (`build/tests`) | 1 | `test-1.log`; missing scoped lock assertion |
| Fixed GREEN build | `cmake --build build --target zene PluginPortsMigrationTest` (worktree root) | 0 | `build-2.log` |
| Fixed GREEN test | `./PluginPortsMigrationTest opulenzUpdatePatchLocksEmulatorForWholeUpdate` (`build/tests`) | 0 | `test-2.log` |
| Reverse patch capture | `git diff -- plugins/OpulenZ/OpulenZ.cpp plugins/OpulenZ/OpulenZ.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-327.r1/logs/fix.patch` (worktree root) | 0 | `PATCH_CAPTURE_EXIT=0` |
| Reverse patch apply | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-327.r1/logs/fix.patch` (worktree root) | 0 | `REVERSE_APPLY_EXIT=0` |
| Reverted RED build | `cmake --build build --target zene PluginPortsMigrationTest` (worktree root) | 0 | `build-3.log` |
| Reverted RED test | `./PluginPortsMigrationTest opulenzUpdatePatchLocksEmulatorForWholeUpdate` (`build/tests`) | 1 | `test-3.log`; same missing scoped lock assertion |
| Restore patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-327.r1/logs/fix.patch` (worktree root) | 0 | `RESTORE_APPLY_EXIT=0` |
| Restored GREEN build | `cmake --build build --target zene PluginPortsMigrationTest` (worktree root) | 0 | `build-4.log` |
| Restored GREEN test | `./PluginPortsMigrationTest opulenzUpdatePatchLocksEmulatorForWholeUpdate` (`build/tests`) | 0 | `test-4.log` |
| Affected-family CTest | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` (`build/tests`) | 0 | 1/1 CTest tests passed; `test-5.log` |
| Complexity gate | `bash tests/complexity-gate.sh --check` (worktree root) | 0 | `gate-1.log` |
| File-length gate | `bash tests/file-length-gate.sh --check` (worktree root) | 0 | `gate-2.log` |

Build, test, and gate logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-327.r1/logs`. The `fff grep` tool was not available in this session, so log searches used `rg` for `error`, `FAILED`, `Assertion`, and the expected RED `FAIL!` assertion.

## Files committed

- `plugins/OpulenZ/OpulenZ.cpp`
- `plugins/OpulenZ/OpulenZ.h`
- `tests/src/plugins/PluginPortsMigrationTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-327.md`
