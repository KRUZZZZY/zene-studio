# Cluster new-p9-209

**Verdict: FIXED**

## Evidence

- [src/core/Keymap.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-209/src/core/Keymap.cpp:150) now skips non-element nodes and elements whose tag is not `degree`. It appends a degree only when its `value` parses as an integer.
- [tests/src/core/SessionTuningTest.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-209/tests/src/core/SessionTuningTest.cpp:296) builds a keymap containing a comment, metadata element, two valid degrees, and a malformed degree value. It expects only the two valid values.
- [tests/upstream-modifications.txt](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-209/tests/upstream-modifications.txt) records the reason for both inherited-file changes.

## Verification

All build logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-209.r1/logs`. Build commands ran from the worktree root; ctest commands ran from `build/tests`.

| Check | Command | Exit | Log / result |
|---|---|---:|---|
| Initial RED | `ctest -R '^SessionTuningTest$' --output-on-failure` | 8 | `test-1-red.log`; repeated after rebuilding `zene` in `test-2-red-confirm.log`. Regression slot failed on the original loader. The first run also had an unrelated real-instrument fixture failure because no plugin module was built yet. |
| Rebuild stale app before trusting failure | `cmake --build build --target zene -j12` | 0 | `build-2-zene.log`. |
| Rebuild the affected test and fixture module | `cmake --build build --target zene SessionTuningTest tripleoscillator -j12` | 0 | `build-4-green.log`; built the product, test, and instrument module needed by the existing fixture. |
| Reverse-patch RED | `git diff -- src/core/Keymap.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-209.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-209.r1/logs/fix.patch` | 0 | Removed only the product fix; patch saved as `fix.patch`. |
| Rebuild with product fix removed | `cmake --build build --target zene SessionTuningTest -j12` | 0 | `build-5-reverse.log`. |
| Reverse-patch RED test | `ctest -R '^SessionTuningTest$' --output-on-failure` | 8 | `test-4-reverse-red.log`; only the new keymap slot failed; Qt reported 14 passed and 1 failed. |
| Reapply the product fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-209.r1/logs/fix.patch` | 0 | Restored the exact product patch. |
| Rebuild fixed product and test | `cmake --build build --target zene SessionTuningTest -j12` | 0 | `build-6-reapply.log`. |
| GREEN | `ctest -R '^SessionTuningTest$' --output-on-failure` | 0 | `test-5-reapplied-green.log`; 1/1 registered ctest tests passed; 0 failed. |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 | `gate-3-final-complexity.log`; passed. |
| File length gate | `bash tests/file-length-gate.sh --check` | 0 | `gate-4-final-file-length.log`; passed. |

The affected ctest family contains one registered test, `SessionTuningTest`; it executed once in the final run.

## Files committed

- `src/core/Keymap.cpp`
- `tests/src/core/SessionTuningTest.cpp`
- `tests/upstream-modifications.txt`
- `tests/fork-sources.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-209.md`
