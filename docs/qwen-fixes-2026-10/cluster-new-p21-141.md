FIXED

# new-p21-141: redundant Editor toolbar allocation

## Verdict and evidence

The finding is **REAL**. In `src/gui/editors/Editor.cpp:101`, `Editor` previously initialized `m_toolBar` with `new DropToolBar(this)`. The constructor then replaces that pointer at line 108 with the result of `addDropToolBarToTop()`. That helper creates another `DropToolBar` parented to the editor at line 61, so the first toolbar remains an unused child. The initializer now uses `nullptr`; the installed transport toolbar remains the one returned by the helper.

The regression `SampleEditorDialogTest::editorOwnsOnlyItsTransportToolbar` at `tests/src/gui/SampleEditorDialogTest.cpp:182` checks that a fresh editor has exactly one direct `DropToolBar` child.

## Verification

All build and test output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-141.r1/logs/`.

| Stage | Exact command | Exit | Result |
|---|---|---:|---|
| Build before fix (`build-1.log`) | `cmake --build build --target zene SampleEditorDialogTest` | 0 | `zene` and the test target built. |
| Original code, RED (`test-1.log`) | `ctest -R '^SampleEditorDialogTest$' --output-on-failure` (from `build/tests`) | 8 | 1/1 CTest target ran; the new assertion saw 2 direct toolbars instead of 1. |
| Build with fix (`build-2.log`) | `cmake --build build --target zene SampleEditorDialogTest` | 0 | Rebuilt both targets. |
| Fixed code, GREEN (`test-2.log`) | `ctest -R '^SampleEditorDialogTest$' --output-on-failure` (from `build/tests`) | 0 | 1/1 CTest target passed. |
| Reverse-patch proof | `git diff -- src/gui/editors/Editor.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-141.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-141.r1/logs/fix.patch` | 0 | Removed only the production fix. |
| Rebuild without fix (`build-3.log`) | `cmake --build build --target zene SampleEditorDialogTest` | 0 | Rebuilt both targets before the proof run. |
| Reverted code, RED (`test-3.log`) | `ctest -R '^SampleEditorDialogTest$' --output-on-failure` (from `build/tests`) | 8 | 1/1 CTest target ran; the new assertion failed again. |
| Restore patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-141.r1/logs/fix.patch` | 0 | Restored the production fix. |
| Rebuild restored code (`build-4.log`) | `cmake --build build --target zene SampleEditorDialogTest` | 0 | Rebuilt both targets. |
| Restored code, GREEN (`test-4.log`) | `ctest -R '^SampleEditorDialogTest$' --output-on-failure` (from `build/tests`) | 0 | 1/1 CTest target passed. |
| Feature gates (`gate-2.log`) | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-141` | 0 | All eight gates passed. |
| Completion check (`submit-1.log`) | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-141 new-p21-141` | 0 | The committed report, clean tree, tests, and feature gates passed; the Self-check answers the ASK items. |

The first feature gate run (`gate-1.log`, exit 1) found that `tests/all-sources.txt` still contained vendored sfizz paths excluded by its own checked-in recipe. I regenerated that manifest from the recipe. The subsequent gate run passed `fork-sources`, `all-sources-reproduce`, and the other gates; complexity and file-length reported no new items in changed files.

## Scope checks

- `m_toolBar` is initialized and assigned only in the `Editor` constructor; the helper creates the installed toolbar before any subsequent use. All editor subclasses share this base constructor. No other member initialization or toolbar creation path needed changing.
- The regression counts direct child toolbars without timing, sleeps, or event races.
- The change only removes the unused GUI child. It does not touch project serialization, saved data, undo, commands, or audio-thread code.
- The verdict is REAL, so the ALREADY-FIXED/FALSE-POSITIVE evidence item does not apply.

## Files included in the fix commit

- `src/gui/editors/Editor.cpp`
- `tests/src/gui/SampleEditorDialogTest.cpp`
- `tests/upstream-modifications.txt`
- `tests/all-sources.txt` (regenerated to match its declared recipe)
- `docs/qwen-fixes-2026-10/cluster-new-p21-141.md`

## Self-check

- **Regression proof:** The test fails with the original initializer and with the reverse patch (both CTest exits 8), then passes with the fix restored (CTest exit 0). See the four test logs above.
- **Determinism:** The test directly counts owned `DropToolBar` children; it uses no wall-clock timing, sleeps, or races.
- **Cause and completeness:** The `m_toolBar` search found the redundant initializer, the assignment from `addDropToolBarToTop()`, later uses, and the single member declaration. Every `Editor` goes through this constructor, and the toolbar helper still creates the sole installed toolbar.
- **Collateral behavior:** Only the redundant widget allocation is removed. Project load/save paths and serialized formats are untouched, and the transport toolbar/actions remain initialized as before.
- **Not-real verdict evidence:** Not applicable; this is a confirmed real bug.

The fix commit is `7f31add44` (`Fix new-p21-141 redundant Editor toolbar`).
