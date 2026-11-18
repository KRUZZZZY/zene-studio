# Cluster new-p8-296: Configure opens the form

**Verdict: ALREADY-FIXED.** No production change is needed.

The finding describes selection values in `CommandCall.fixed`, which would let `runCommand()` skip the form. The current palette call puts the selection in `defaults` and leaves `fixed` empty. `runCommand()` checks only `fixed` when deciding whether it can invoke immediately. The selected value therefore pre-fills an editable form field, and the primary Configure action opens that form.

## Source evidence

- `src/gui/CommandPalette.cpp:243` constructs `CommandCall{entry.command, {}, selectionContext(entry.command), entry.label, true}`: no fixed arguments, selection context as defaults, and `ask` enabled.
- `src/gui/CommandRun.cpp:104-114` tests `call.fixed` against schema properties. With the palette's empty `fixed` object, a command with a `track` property proceeds to `CommandForm`.
- `include/CommandForm.h:49-51` defines defaults as values that pre-fill editable fields; `src/gui/CommandForm.cpp:222-230` applies those defaults to the form.

## Evidence

| Check | Exact command and working directory | Exit | Result |
| --- | --- | ---: | --- |
| Initial probe build | Worktree: `cmake --build build --target zene CommandPaletteTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-296.r1/logs/01-red-build.log 2>&1; echo EXIT=$?` | 2 | Temporary test setup lacked `Song.h`; the probe was corrected before testing. |
| Rebuilt app and test target | Worktree: `cmake --build build --target zene CommandPaletteTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-296.r1/logs/02-red-build.log 2>&1; echo EXIT=$?` | 0 | Both targets built. |
| Regression attempt on existing production code | `build/tests`: `ctest -R '^CommandPaletteTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-296.r1/logs/03-red-test.log 2>&1; echo EXIT=$?` | 0 | 1/1 CTest target passed. This was not RED; the current implementation already has the expected behavior. |
| QtTest detail | `build/tests`: `./CommandPaletteTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-296.r1/logs/04-red-direct.log 2>&1; echo EXIT=$?` | 0 | 11 QtTest cases passed, including the temporary selected-track probe. The probe was removed afterward. |
| Final checked-in test suite | `build/tests`: `ctest -R '^CommandPaletteTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-296.r1/logs/08-final-test.log 2>&1; echo EXIT=$?` | 0 | 1/1 CTest target passed after removing the temporary probe. |
| Final QtTest count | `build/tests`: `./CommandPaletteTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-296.r1/logs/09-final-qttest.log 2>&1; echo EXIT=$?` | 0 | 10 passed, 0 failed, 0 skipped. |
| GREEN run | N/A | N/A | No production change was required, so there is no separate GREEN transition. |
| Affected-family count | `CommandPaletteTest` | — | Final checked-in suite: 1 CTest target and 10 QtTest cases. The temporary probe also passed in the earlier 11-case run. |
| Complexity gate | Worktree: `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-296.r1/logs/10-complexity-gate.log 2>&1; echo EXIT=$?` | 0 | PASS. |
| File-length gate | Worktree: `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-296.r1/logs/11-file-length-gate.log 2>&1; echo EXIT=$?` | 0 | PASS. |

The `fff` grep tool was not available in this session; build-log inspection used `rg` searches instead.

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p8-296.md` — this verdict and evidence report.
