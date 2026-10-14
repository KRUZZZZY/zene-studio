# Cluster new-p10-466

Verdict: **FIXED**

`XpressiveView::expressionChanged()` stored every edit before parsing, but its parse-failure and empty-text branches did not mark the song modified. It now detects whether the selected stored expression actually changed and marks the song modified in those two branches. Existing valid-expression handling is unchanged. The expression editor regression covers invalid input and clearing a valid expression.

Evidence:

- [Xpressive.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-466/plugins/Xpressive/Xpressive.cpp:525) selects and compares the stored expression; line 611 marks changed invalid text and line 620 marks changed empty text.
- [PluginPortsMigrationTest.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-466/tests/src/plugins/PluginPortsMigrationTest.cpp:211) defines both regression rows; line 264 checks `Song::isModified()`.
- This configured tree has no `Xpressive` CTest entry (`ctest -N -R '^Xpressive$'` reported zero tests). The regression runs in `PluginPortsMigrationTest`, which loads the Xpressive plugin module.

| Check | Command and result | Evidence log |
|---|---|---|
| RED, original code | From `build/tests`: `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` — EXIT=8. Both expression rows failed the dirty-state assertion; the other eight QtTest cases passed. | `test-2.log` |
| RED, inverse proof | `git apply -R .../fix.patch` — EXIT=0; `cmake --build build --target zene PluginPortsMigrationTest` — EXIT=0; same ctest command — EXIT=8, with both rows failing again. | `build-6.log`, `test-4.log` |
| GREEN, restored fix | `git apply .../fix.patch` — EXIT=0; `cmake --build build --target zene PluginPortsMigrationTest` — EXIT=0; same ctest command — EXIT=0. | `build-7.log`, `test-5.log` |
| Final family count | From `build/tests`: `ctest -V -R '^PluginPortsMigrationTest$' --output-on-failure` — EXIT=0; one CTest target ran, with 10 QtTest cases passed, 0 failed, 0 skipped. | `test-6.log` |
| Complexity gate | `bash tests/complexity-gate.sh --check` — EXIT=0. | `gate-1.log` |
| File-length gate | `bash tests/file-length-gate.sh --check` — EXIT=0. | `gate-2.log` |

The build command before the original-code RED run also passed: `cmake --build build --target zene PluginPortsMigrationTest` — EXIT=0 (`build-4.log`). Initial harness iterations are retained in `build-1.log` through `build-3.log` and `test-1.log`; their access and parent setup errors were corrected before the assertion-level RED run.

Files committed:

- `plugins/Xpressive/Xpressive.cpp`
- `tests/src/plugins/PluginPortsMigrationTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-466.md`
