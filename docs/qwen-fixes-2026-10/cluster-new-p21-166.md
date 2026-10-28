Verdict: FIXED

new-p21-166: wheelEvent dereferences model() without a null check - FIXED (REAL defect; latent in the product today)

## Evidence

Pre-fix line numbers are marked "(pre-fix)"; all other line numbers are on this branch.

- **Defect.** `src/gui/widgets/FloatModelEditorBase.cpp:347` `wheelEvent` had no null check. It reached `nudge()`, which reads `model()` unguarded: `model()->value()` at 412 (pre-fix; `nudge` is at 383 pre-fix), `model()->incValue` at 417 (pre-fix). The same handler also reads `model()->value()` at `emit sliderMoved` (379 pre-fix), and `showTextFloat()` (108 pre-fix) reaches `currentValueToText()` → `model()->getRoundedValue()` (620 pre-fix).
- **Siblings that already guard.** `paintEvent` at `FloatModelEditorBase.cpp:326`, `keyPressEvent` at 425 (post-fix; 423 pre-fix), which falls through to `QWidget::keyPressEvent` for unhandled keys, and `friendlyUpdate` at 596. The fix copies the keyPressEvent pattern.
- **Reachability.** `Knob`, `BarModelEditor` and `Draggable` inherit this `wheelEvent`; none overrides it (`grep -n wheelEvent src/gui/widgets/` lists `Fader.cpp:304` as the only FloatModelEditorBase-family override). A null model is produced by `setModel(nullptr)`, e.g. `src/gui/instrument/InstrumentTrackWindow.cpp:373`, which leaves the widget enabled (`watchModelLifetime` returns early at `FloatModelEditorBase.cpp:84`). The pitch knob is hidden in that same branch (`InstrumentTrackWindow.cpp:371`, `m_pitchKnob->hide()`), so a wheel cannot reach it in the product today. The destroyed-model path does not trigger this, because `watchModelLifetime` (82-88) disables the widget.
- **Correction to my own premise.** A default-constructed editor is not model-less: the constructor binds a dummy `FloatModel` (`FloatModelEditorBase.cpp` constructor, `new FloatModel(0, 0, 0, 1, ...)`). My first test version assumed otherwise and failed on that assertion (see test-1.log below). The test now reaches the null state with `setModel(nullptr)`, as production does.
- **Fix.** `src/gui/widgets/FloatModelEditorBase.cpp:349`:
  `if (model() == nullptr) { QWidget::wheelEvent(we); return; }`.
  It runs before `updateInteractionState` (352), so a dead control's interaction state does not change. `QWidget::wheelEvent` ignores the event, so it passes on to the parent, as unhandled keys already do in `keyPressEvent`.
- **Regression.** `tests/src/gui/VcaStripLifetimeTest.cpp:262` `anUnboundEditorIgnoresWheelAndPassesItOn` (the RED test) and `:278` `aLiveEditorStillTakesWheelInput` (guards normal input).
- **Registration.** `tests/upstream-modifications.txt:1061` (existing line; the new-p21-166 reason is appended).

## Commands and exit codes

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-166.r1/logs`.
ccache could not write its temp files under the shared bunny-fix directory (`build-1.log`: "failed to create temporary file ... Permission denied"), so every build below uses `CCACHE_DISABLE=1`. This only disables the cache; the build configuration is unchanged.

| Step | Command (run from the worktree or `build/tests` as noted) | Result |
| --- | --- | --- |
| Build, first attempt (ccache) | `cmake --build build --target VcaStripLifetimeTest -j2 > build-1.log 2>&1` | EXIT=2 (ccache permission error) |
| Build, with ccache off (test-first version) | `CCACHE_DISABLE=1 cmake --build build --target VcaStripLifetimeTest -j2 > build-2.log 2>&1` | EXIT=0 |
| Superseded RED (wrong premise) | `cd build/tests && ctest -R '^VcaStripLifetimeTest$' --output-on-failure > test-1.log 2>&1` | EXIT=8: `'editor.model() == nullptr' returned FALSE` at test line 267; the premise was wrong, so the test was corrected |
| **RED, current test, unfixed code** | `CCACHE_DISABLE=1 cmake --build build --target VcaStripLifetimeTest -j2 > build-3.log 2>&1` | EXIT=0 |
| RED ctest family | `cd build/tests && ctest -R '^VcaStripLifetimeTest$' --output-on-failure > test-3.log 2>&1` | EXIT=8 (`SegFault`, test #426 fails) |
| RED single function | `cd build/tests && ./VcaStripLifetimeTest anUnboundEditorIgnoresWheelAndPassesItOn > test-4.log 2>&1` | EXIT=139: `Received signal 11 (SIGSEGV), code SEGV_MAPERR, at ... address 0x0000000000000094` |
| Build with fix | `CCACHE_DISABLE=1 cmake --build build --target zene VcaStripLifetimeTest -j2 > build-4.log 2>&1` | EXIT=0 |
| **GREEN ctest family** | `cd build/tests && ctest -R '^VcaStripLifetimeTest$' --output-on-failure > test-5.log 2>&1` | EXIT=0 (100% passed, 1/1, test #426) |
| GREEN single function | `cd build/tests && ./VcaStripLifetimeTest anUnboundEditorIgnoresWheelAndPassesItOn > test-6.log 2>&1` | EXIT=0 (`Totals: 3 passed, 0 failed`; the three are the init and cleanup slots plus the test) |
| Full binary, with fix | `cd build/tests && ./VcaStripLifetimeTest > test-11.log 2>&1` | EXIT=0 (`Totals: 15 passed, 0 failed`) |
| **Step 4b: save product diff** | `git diff -- src/gui/widgets/FloatModelEditorBase.cpp > logs/fix.patch` | EXIT=0 |
| 4b: reverse the fix | `git apply -R logs/fix.patch` | EXIT=0 |
| 4b: build without fix | `CCACHE_DISABLE=1 cmake --build build --target zene VcaStripLifetimeTest -j2 > build-5.log 2>&1` | EXIT=0 |
| 4b: RED ctest without fix | `cd build/tests && ctest -R '^VcaStripLifetimeTest$' --output-on-failure > test-7.log 2>&1` | EXIT=8 (`SegFault`) |
| 4b: RED single without fix | `cd build/tests && ./VcaStripLifetimeTest anUnboundEditorIgnoresWheelAndPassesItOn > test-8.log 2>&1` | EXIT=139 (SIGSEGV) |
| 4b: reapply the fix | `git apply logs/fix.patch` | EXIT=0 |
| 4b: build with fix | `CCACHE_DISABLE=1 cmake --build build --target zene VcaStripLifetimeTest -j2 > build-6.log 2>&1` | EXIT=0 |
| 4b: GREEN ctest with fix | `cd build/tests && ctest -R '^VcaStripLifetimeTest$' --output-on-failure > test-9.log 2>&1` | EXIT=0 |
| 4b: GREEN single with fix | `cd build/tests && ./VcaStripLifetimeTest anUnboundEditorIgnoresWheelAndPassesItOn > test-10.log 2>&1` | EXIT=0 |

**Affected family:** `ctest -R '^VcaStripLifetimeTest$'` runs 1 ctest test (#426), which runs 15 QTest functions (test-11.log).

**Gate table (`feature_check.py`)** could not run in this write-jail. `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` exits 1 before any check: `PermissionError: [Errno 13] ... 'zene-qwen-new-p21-166-feature-check'` (`feature-1.log`). The gate writes its logs to a sibling directory in the parent projects folder, which this run may not write to. I did not edit the shared script. I ran the same commands it runs, from the worktree:

| Gate | Command | EXIT |
| --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh > gate-fork-sources.log 2>&1` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh > gate-all-sources-reproduce.log 2>&1` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh > gate-no-upstream-regression.log 2>&1` | 0 |
| check-namespace | `tests/scripted/check-namespace > gate-check-namespace.log 2>&1` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh > gate-no-tautology.log 2>&1` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py > gate-doc-figures.log 2>&1` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check > gate-complexity.log 2>&1` | 1 (6 `REGRESSION:` lines, none in my changed files) |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check > gate-file-length.log 2>&1` | 1 (23 `REGRESSION:` lines, none in my changed files) |

The two ratchets are red on the train on purpose. The check for "none in my files" is a grep for `FloatModelEditorBase|VcaStripLifetimeTest` across the `REGRESSION:` lines, which returned 0. I did not get the base-listing comparison the gate performs, so this is a manual reading, not the gate's own PASS.

**Completion check** (`submit_check.py`, before this commit): EXIT=1 (`submit-1.log`). Its failures were the commit, the report, the self-check, the test-changed item (now satisfied), and the gate item above.

## Self-check

1. **RED then GREEN, with exit codes.** Yes. RED: `build-3.log` EXIT=0, `test-3.log` ctest EXIT=8 (SegFault), `test-4.log` single EXIT=139 (SIGSEGV at 0x94, a null model dereference). GREEN: `build-4.log` EXIT=0, `test-5.log` ctest EXIT=0, `test-6.log` single EXIT=0. The 4b reversal-and-reapply runs are in the table above (`test-7` to `test-10`).
2. **Deterministic.** Yes. No sleeps, timers or wall-clock assertions are added. Each wheel event is built from fixed coordinates and a fixed `QPoint{0, 120}` delta. The live test asserts `model.value() > 1.f` on a fixed starting value. Showing the floating text starts a timer, but the test does not wait for it.
3. **Fix at the cause; callers and siblings.** The wheel path enters `nudge()` only through `wheelEvent`, which is now guarded at the top. The other caller of `nudge()` is `keyPressEvent`, which was already guarded (425). `showTextFloat` and `updateInteractionState` are reached from the wheel only after the guard. **Not changed, and open:** the mouse handlers (`mousePressEvent` reads `model()` at 225, `mouseReleaseEvent` at 277), `mouseDoubleClickEvent` → `enterValue` (571), and `contextMenuEvent` (`contextMenu(model()->displayName(), this)` at 175) read `model()` unguarded. They are the same latent class and the same reachability (the hidden pitch knob), but they are a separate input path and outside this finding, so I did not widen the fix. They need their own finding.
4. **No collateral damage.** The guard changes behaviour only when the model is null, which previously crashed. Saved data and projects are not touched. Live editors are covered by `aLiveEditorStillTakesWheelInput` (PASS, `test-6.log`). No limit or check is added, so no other caller can be hit by one.
5. **Verdict basis.** FIXED, not ALREADY-FIXED: the unguarded path existed on this branch before my change (the crash was reproduced at `test-4.log`). It is latent in the product today, because the one production null-model path (`InstrumentTrackWindow.cpp:371-373`) hides the knob.

Realtime: GUI-thread only. No audio path, no allocation, lock or log is added. Undo: no change. Commands: no new control commands. Registration: `tests/upstream-modifications.txt:1061` extended in the same commit.
