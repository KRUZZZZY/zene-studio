# Cluster new-p12-263

**Verdict: FIXED**

`TempoSyncKnob::modelChanged()` warned `"no TempoSyncKnobModel has been set!"` when the attached model was not a `TempoSyncKnobModel` (or was `nullptr`), then continued to dereference it in `updateDescAndIcon()`. A detach (`setModel(nullptr)` / `unsetModel()`) crashed the widget. The method now returns right after the warning, matching the early-return-on-null convention already used by the sibling model views (`ComboBox.cpp:143`, `FloatModelEditorBase.cpp:594`, `Fader.cpp:600`).

## Evidence

- [src/gui/widgets/TempoSyncKnob.cpp:87-92](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-263/src/gui/widgets/TempoSyncKnob.cpp): `if (model() == nullptr) { qWarning(...); return; }` before `m_custom`, the two `connect(model(), ...)` calls and `updateDescAndIcon()`.
- [src/gui/widgets/TempoSyncKnob.cpp:184](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-263/src/gui/widgets/TempoSyncKnob.cpp): `updateDescAndIcon()` dereferences `model()->m_tempoSyncMode` — the crash site.
- [tests/src/gui/KnobKeyboardTest.cpp:169-184](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-263/tests/src/gui/KnobKeyboardTest.cpp): `aTempoSyncKnobStopsAfterWarningAboutADetachedModel` attaches a real `TempoSyncKnobModel`, detaches it, and asserts the warning fired and the widget survived.
- [tests/upstream-modifications.txt](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-263/tests/upstream-modifications.txt): declares the inherited-file change.

The failure was a hard segfault, not just a warning: the RED stack trace shows

```
Received signal 11 (SIGSEGV), code SEGV_MAPERR, ... accessing address 0x0000000000000354
#5 lmms::gui::TempoSyncKnob::updateDescAndIcon()
#7 KnobKeyboardTest::aTempoSyncKnobStopsAfterWarningAboutADetachedModel()
```

`0x354` is the offset of `m_tempoSyncMode` read off the null `model()`.

## Test evidence

Affected-family count: `ctest -R '^(KnobKeyboardTest|DelayControlsTest|DelayLfoAutomationTest|XyPadPairTest)$'` runs **4** CTest tests (the four in-tree targets that instantiate `TempoSyncKnob` / its dialog); each run reported `100% tests passed out of 4`. The regression slot is one of the ~30 QtTest slots inside `KnobKeyboardTest`.

| Run | Exact command | Exit |
| --- | --- | ---: |
| RED, original code (ctest) | `cmake --build build --target KnobKeyboardTest -j12 > .../logs/build-2-red.log 2>&1; echo EXIT=$?` | 0 |
| RED, original code | `cd build/tests && ctest -R '^KnobKeyboardTest$' --output-on-failure > .../logs/test-red-1.log 2>&1; echo EXIT=$?` | 8 |
| GREEN, fixed code | `cmake --build build --target zene KnobKeyboardTest -j12 > .../logs/build-3-green.log 2>&1; echo EXIT=$?` | 0 |
| GREEN, fixed code | `cd build/tests && ctest -R '^KnobKeyboardTest$' --output-on-failure > .../logs/test-green-1.log 2>&1; echo EXIT=$?` | 0 |
| Reverse proof, fix removed | `git apply -R .../logs/fix.patch; cmake --build build --target KnobKeyboardTest -j12 > .../logs/build-4-reverted.log 2>&1; echo EXIT=$?` | 0 |
| Reverse proof, fix removed (run) | `QT_QPA_PLATFORM=offscreen ./build/tests/KnobKeyboardTest aTempoSyncKnobStopsAfterWarningAboutADetachedModel > .../logs/test-red-2.log 2>&1; echo EXIT=$?` | 139 |
| Fix restored | `git apply .../logs/fix.patch; cmake --build build --target KnobKeyboardTest -j12 > .../logs/build-5-reapplied.log 2>&1; echo EXIT=$?` | 0 |
| Fix restored (run) | `QT_QPA_PLATFORM=offscreen ./build/tests/KnobKeyboardTest aTempoSyncKnobStopsAfterWarningAboutADetachedModel > .../logs/test-green-2.log 2>&1; echo EXIT=$?` | 0 |
| Affected family | `cmake --build build --target DelayControlsTest DelayLfoAutomationTest XyPadPairTest KnobKeyboardTest -j12 > .../logs/build-6-family.log 2>&1; echo EXIT=$?` | 0 |
| Affected family | `cd build/tests && ctest -R '^(KnobKeyboardTest|DelayControlsTest|DelayLfoAutomationTest|XyPadPairTest)$' --output-on-failure > .../logs/test-family-1.log 2>&1; echo EXIT=$?` | 0 |

`EXIT=139` is `128+11` (SIGSEGV). The reversal used the required product-only patch:

```sh
git diff -- src/gui/widgets/TempoSyncKnob.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-263.r1/logs/fix.patch
EXIT=0
git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-263.r1/logs/fix.patch
EXIT=0
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-263.r1/logs/fix.patch
EXIT=0
```

## Builds and gates

All build/test output was written to `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-263.r1/logs/` and searched with the `fff` grep tool.

| Exact command | Exit |
| --- | ---: |
| `bash tests/complexity-gate.sh --check > .../logs/gate-complexity.log 2>&1; echo EXIT=$?` | 1 |
| `bash tests/file-length-gate.sh --check > .../logs/gate-file-length.log 2>&1; echo EXIT=$?` | 1 |
| `bash tests/fork-sources-gate.sh > .../logs/gate-fork-sources.log 2>&1; echo EXIT=$?` | 0 |
| `bash tests/no-upstream-regression-gate.sh > .../logs/gate-upstream.log 2>&1; echo EXIT=$?` | 0 |

**The two ratchet gates fail on this branch independently of this change, and are left untouched.** This worktree's HEAD already fails both before the fix: a clean `git archive HEAD` copy at `/tmp/opencode/p12-263-head` produced the same failures (`COMPLEXITY_EXIT=1`, `FILELEN_EXIT=1`), and `diff` of the REGRESSION/FAIL lines between HEAD and the fixed tree is empty for both gates. The reported regressions name only files this change does not touch (e.g. `dotCentreAlong@tests/src/gui/XyPadPairTest.cpp CCN 16`, `new file over 500 lines: tests/src/core/VideoTrackTest.cpp (589)`); the baseline TSVs are stale relative to the train. This change adds **zero** new gate regressions (its own new function has CCN 1, and `KnobKeyboardTest.cpp` is not in the over-500 list), so the failures are pre-existing baseline drift. Re-anchoring a gate is forbidden, so no baseline was written.

## Files in the commit

- `src/gui/widgets/TempoSyncKnob.cpp`
- `tests/src/gui/KnobKeyboardTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p12-263.md`
