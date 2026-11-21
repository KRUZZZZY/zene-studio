FIXED
new-p18-18: FIXED
new-p23-75: FIXED
new-p10-91: FIXED

Evidence and commands

new-p18-18 — REAL: `openFor()` reloaded saved clip values when it found the existing pair. It now raises that dialog without replacing pending controls (`src/gui/CrossfadeEditorDialog.cpp:209`). Regression: `tests/src/gui/CrossfadeEditorTest.cpp:169` stages 12 ticks / Exponential, reopens the same pair in reverse order, and checks the pending values.
- `cmake --build build --target zene CrossfadeEditorTest` — EXIT 0 (logs 002, 004, 007, 010).
- `./CrossfadeEditorTest reopeningAnExistingEditorPreservesPendingControls` from `build/tests` on original code — EXIT 1, actual 48 / expected 12 (003); with fix — EXIT 0 (005).
- 4b revert: `git diff -- src/gui/CrossfadeEditorDialog.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-18.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-18.r1/logs/fix.patch` — EXIT 0 (006); rebuild — EXIT 0 (007); test — EXIT 1 (008). Restore: `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-18.r1/logs/fix.patch` — EXIT 0 (009); rebuild — EXIT 0 (010); test — EXIT 0 (011).

new-p23-75 — REAL: `close()` hides the `WA_DeleteOnClose` dialog while deferred deletion is pending; `openFor()` now reuses only a visible matching dialog (`src/gui/CrossfadeEditorDialog.cpp:209`). Regression: `tests/src/gui/CrossfadeEditorTest.cpp:185` closes, confirms the old `QPointer` is still live, and immediately opens a distinct visible dialog without processing events.
- `cmake --build build --target zene CrossfadeEditorTest` — EXIT 0 (logs 013, 015, 018, 021).
- `./CrossfadeEditorTest reopeningBeforeDeferredDeleteCreatesNewEditor` from `build/tests` on original code — EXIT 1, reopened pointer matched the closing dialog (014); with fix — EXIT 0 (016).
- 4b revert: `git diff -- src/gui/CrossfadeEditorDialog.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-18.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-18.r1/logs/fix.patch` — EXIT 0 (017); rebuild — EXIT 0 (018); test — EXIT 1 (019). Restore: `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-18.r1/logs/fix.patch` — EXIT 0 (020); rebuild — EXIT 0 (021); test — EXIT 0 (022).

new-p10-91 — REAL: ramp bounds and the outgoing ramp start now use `std::int64_t` before conversion to painter coordinates (`src/gui/CrossfadeEditorDialog.cpp:118, 120, 128, 141`). Regression: `tests/src/gui/CrossfadeEditorTest.cpp:204` paints a normal span, an incoming ramp crossing `INT_MAX`, and an outgoing ramp crossing `INT_MIN`.
- `cmake --build build --target zene CrossfadeEditorTest` — EXIT 0 (logs 035, 038, 040, 043).
- 4b revert: `git diff -- src/gui/CrossfadeEditorDialog.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-18.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-18.r1/logs/fix.patch` — EXIT 0 (037); rebuild — EXIT 0 (040); `./CrossfadeEditorTest paintingUsesWideBoundsAtBothIntLimits` from `build/tests` — EXIT 1 (041), both upper and lower curves absent. Restore: `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-18.r1/logs/fix.patch` — EXIT 0 (042); rebuild — EXIT 0 (043); same test — EXIT 0 (044).

| Cluster | RED command (from `build/tests`) | GREEN command (from `build/tests`) | Family result |
|---|---|---|---|
| new-p18-18 | `./CrossfadeEditorTest reopeningAnExistingEditorPreservesPendingControls` — EXIT 1 | Same command — EXIT 0 | `ctest -R '^CrossfadeEditorTest$' --output-on-failure` — EXIT 0, 1/1 |
| new-p23-75 | `./CrossfadeEditorTest reopeningBeforeDeferredDeleteCreatesNewEditor` — EXIT 1 | Same command — EXIT 0 | `ctest -R '^CrossfadeEditorTest$' --output-on-failure` — EXIT 0, 1/1 |
| new-p10-91 | `./CrossfadeEditorTest paintingUsesWideBoundsAtBothIntLimits` — EXIT 1 | Same command — EXIT 0 | `ctest -R '^CrossfadeEditorTest$' --output-on-failure` — EXIT 0, 1/1 |

## Gates and affected family

The first feature-check run found that `tests/all-sources.txt` still contained 1,148 vendored sfizz paths excluded by its documented recipe. I regenerated only the manifest entry list from that exact recipe and recorded the inherited-file reason in `tests/upstream-modifications.txt`.

| Check | Result |
|---|---|
| CrossfadeEditorTest CTest family | PASS — 1/1 (log 045) |
| fork-sources | PASS (log 052) |
| all-sources-reproduce | PASS (log 052) |
| no-upstream-regression | PASS (log 052) |
| check-namespace | PASS (log 052) |
| no-tautology | PASS (log 052) |
| doc-figures | PASS (log 052) |
| complexity | PASS — no new item (log 052) |
| file-length | PASS — no new item (log 052) |
| `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p18-18` | EXIT 0 (log 052) |
| `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p18-18 new-p18-18` | EXIT 0 (log 054) |

## Self-check

- Regression fails with each product fix reverted and passes with it restored: p18 exits 1/0 (logs 008/011), p23 exits 1/0 (019/022), p10 exits 1/0 (041/044). Each proof rebuilt `zene` and `CrossfadeEditorTest` between changes.
- Deterministic: p18 uses explicit control values; p23 closes and reopens synchronously while a `QPointer` confirms deferred deletion has not run; p10 injects fixed normal, `INT_MAX`, and `INT_MIN` ticks and checks rendered `QImage` pixels. No sleeps, clock timing, or races.
- Cause and sibling paths checked: `openFor()` is called by `ShellMenuEntries.cpp:165`; active visible editors still reuse and raise, while clip position/length/edit signals continue to call `reload()`. `CrossfadeCurveView::setRamps()` is called by `CrossfadeEditorDialog::sync()` at `src/gui/CrossfadeEditorDialog.cpp:293`; both bound expressions and the outgoing ramp origin are widened before arithmetic, while int inputs stay within the int64 range. No header/API or audio-thread path changed.
- No collateral behavior: serialization, saved project data, commands, undo, and clip state are untouched. Reopening an active editor keeps its pending UI values; a closed dialog is replaced. Painting outside signed-int overflow bounds retains the same double-coordinate mapping. No new limit was added.
- Not applicable: all three decisions are REAL and all three statuses are FIXED; none is ALREADY-FIXED or FALSE-POSITIVE.
