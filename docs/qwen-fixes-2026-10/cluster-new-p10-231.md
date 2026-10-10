# Cluster new-p10-231

**Verdict: FIXED**

## Finding and change

The finding is real. In [LocalZynAddSubFx.cpp](../../plugins/ZynAddSubFx/LocalZynAddSubFx.cpp:246), the note-off handler now reads the prior count, sends Zyn a note-off only when that count is one, and clamps the stored count at zero. An unmatched note-off therefore cannot make the per-key count negative. The note-on and matched note-off sequence is covered by [ZynNoteOffTest.cpp](../../tests/src/plugins/ZynNoteOffTest.cpp:35).

The regression ran against the original handler and failed with `runningNotesFor(60) == -1`. Reversing only the production patch reproduced the same failure; restoring the patch made it pass.

## Evidence

Commands below were run from `build/tests` for ctest and the worktree root for build and gate commands. Build and test output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-231.r1/logs/`.

| Check | Exact command | Exit / result |
|---|---|---|
| RED on original code (`test-1.log`) | `ctest -R ZynNoteOffTest --output-on-failure` | 8; 1/1 failed, actual count `-1` |
| GREEN after fix (`test-2.log`) | `ctest -R ZynNoteOffTest --output-on-failure` | 0; 1/1 passed |
| Remove production patch | `git diff -- plugins/ZynAddSubFx/LocalZynAddSubFx.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-231.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-231.r1/logs/fix.patch` | 0 |
| RED with patch removed (`test-4.log`) | `ctest -R ZynNoteOffTest --output-on-failure` | 8; 1/1 failed, actual count `-1` |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-231.r1/logs/fix.patch` | 0 |
| GREEN after restore (`test-5.log`) | `ctest -R ZynNoteOffTest --output-on-failure` | 0; 1/1 passed |
| Zyn family (`test-6.log`) | `ctest -R Zyn --output-on-failure` | 0; 2/2 passed (`ZynSeparateProcessTest`, `ZynNoteOffTest`) |
| Complexity gate (`gate-1.log`) | `bash tests/complexity-gate.sh --check` | 0; PASS |
| File-length gate (`gate-2.log`) | `bash tests/file-length-gate.sh --check` | 0; PASS |
| Source manifest (`manifest-1.log`) | `bash tests/all-sources-reproduce.sh` | 0; REPRODUCES |

The ctest commands wrote their output to the named log files and appended the observed `EXIT` code. A focused family run before the proof cycle also passed 2/2 (`test-3.log`). The initial direct test-target build had no generated target (`build-1.log`, exit 2); `cmake --build build --target zene -j2` completed with exit 0 (`build-3.log`) and refreshed the generated target list. The first production build after the edit exposed a missing switch-case scope around the local variable (`build-5.log`, exit 2); after adding the scope, `cmake --build build --target zene zynaddsubfx ZynNoteOffTest -j2` passed (`build-6.log`, exit 0). The same build command passed after patch removal and restoration (`build-8.log` and `build-9.log`, exit 0). `cmake --build build --target ZynNoteOffTest -j2` passed before the RED run (`build-4.log`, exit 0), and `cmake --build build --target ZynSeparateProcessTest -j2` passed before the family run (`build-7.log`, exit 0).

The `fff grep` tool was not available in this session; logs were searched with `rg` for `error`, `FAILED`, and `Assertion`.

## Files committed

- `plugins/ZynAddSubFx/LocalZynAddSubFx.cpp`
- `tests/src/plugins/ZynNoteOffTest.cpp`
- `tests/CMakeLists.txt`
- `tests/all-sources.txt`
- `tests/fork-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-231.md`
