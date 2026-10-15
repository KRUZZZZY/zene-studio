# Cluster new-p12-225

Verdict: **FIXED**

## Finding and change

The finding was real. `samplerange::frameFromText` read the suffix with an `||` whose left side was the colon test, so
any text containing `:` short-circuited before `stripWord(&body, secondWords)` ran; the trailing `s`/`sec` then
reached `timeFrom()`, whose clock regex anchors `$`, so the text was rejected and the typed value was refused.

Contract evidence that a clock and its suffix word must combine: `include/SampleRangeFields.h:69` documents the
typed text as `"1:02.500" or "62.5 s" as a time ... a suffix names its unit`, and
`tests/src/gui/SampleEditorDialogTest.cpp:248` already pins "a suffix names its unit" for `"0.25 s"`.

Evidence lines: the buggy short-circuit was at `src/gui/SampleRangeFields.cpp:203`; it is now
`src/gui/SampleRangeFields.cpp:204` (suffix word off first, then the colon test). The unchanged rejection path is
`timeFrom` at `src/gui/SampleRangeFields.cpp:116` (the anchored `^(?:(\d+):)?(?:(\d+):)?(\d+(?:\.\d*)?)$`).

The fix swaps the two operands of the existing `||`; `stripWord` is the mutating one and must run first, because
after it the body no longer carries the word. Every input the old order accepted is still accepted: only inputs
containing a colon *and* ending in `s`/`sec` change, and those all failed before (`"1:75"`, `"soon"` and the
existing unit fallbacks are untouched).

The regression is `tests/src/gui/SampleEditorDialogTest.cpp::textReadsAndTypesInEachUnit`
(`tests/src/gui/SampleEditorDialogTest.cpp:239`), extended with `"1:02.500 s"`, `"1:02 sec"` and `"0:30 s"`; no new
test file was needed, that case already covers the range fields' text. The inherited-source reason is registered as
the last line of `tests/upstream-modifications.txt`; `src/gui/SampleRangeFields.cpp` is a fork source already listed
in `tests/fork-sources.txt:1956`, so no new registration was required.

## Verification evidence

The affected CTest family is `SampleEditorDialogTest`, widened with the two other tests that drive the sample
editor's typed range through the control commands: **3 CTest tests executed (3/3)**. No test was weakened.

RED (regression written first, before any product edit), cwd `build/tests`:

```
FAIL!  : SampleEditorDialogTest::textReadsAndTypesInEachUnit() 'samplerange::frameFromText(QStringLiteral("1:02.500 s"), ctx, Unit::Time, &frame)' returned FALSE. ()
0% tests passed, 1 tests failed out of 1
```

| Stage | Exact command and working directory | Exit |
| --- | --- | ---: |
| Regression build before the product change (cwd: worktree) | `cmake --build build --target SampleEditorDialogTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-225.r1/logs/build-red-1.log 2>&1; echo EXIT=$?` | 0 |
| **RED** on the original code (cwd: `build/tests`) | `ctest -R SampleEditorDialogTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-225.r1/logs/test-red-1.log 2>&1; echo EXIT=$?` | 8 |
| Build after the fix (cwd: worktree) | `cmake --build build --target zene SampleEditorDialogTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-225.r1/logs/build-green-1.log 2>&1; echo BUILD_EXIT=$?` | 0 |
| **GREEN** after the fix (cwd: `build/tests`) | `ctest -R SampleEditorDialogTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-225.r1/logs/test-green-1.log 2>&1; echo EXIT=$?` | 0 (1/1 test) |
| Create and reverse only the product patch (cwd: worktree) | `git diff -- src/gui/SampleRangeFields.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-225.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-225.r1/logs/fix.patch` | 0 |
| Build with the product patch reversed (cwd: worktree) | `cmake --build build --target zene SampleEditorDialogTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-225.r1/logs/build-proof-red.log 2>&1; echo BUILD_EXIT=$?` | 0 |
| **RED with the fix reversed** (cwd: `build/tests`) | `ctest -R SampleEditorDialogTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-225.r1/logs/test-proof-red.log 2>&1; echo TEST_EXIT=$?` | 8 |
| Restore the product patch (cwd: worktree) | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-225.r1/logs/fix.patch` | 0 |
| Build after restoring the patch (cwd: worktree) | `cmake --build build --target zene SampleEditorDialogTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-225.r1/logs/build-proof-green.log 2>&1; echo BUILD_EXIT=$?` | 0 |
| **GREEN after restoring the patch** (cwd: `build/tests`) | `ctest -R SampleEditorDialogTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-225.r1/logs/test-proof-green.log 2>&1; echo TEST_EXIT=$?` | 0 (1/1 test) |
| Control-path test build (cwd: worktree) | `cmake --build build --target ControlSuccessPathsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-225.r1/logs/build-family-3.log 2>&1; echo BUILD_EXIT=$?` | 0 |
| Affected-family run (cwd: `build/tests`) | `ctest -R "SampleEditorDialogTest\|ControlSuccessPathsTest\|ControlAudioEditing" --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-225.r1/logs/test-family-2.log 2>&1; echo TEST_EXIT=$?` | 0 (3/3 passed: ControlSuccessPathsTest, SampleEditorDialogTest, ControlAudioEditing) |
| Complexity ratchet, final code (cwd: worktree) | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-225.r1/logs/gate-complexity-1.log 2>&1; echo COMPLEXITY_EXIT=$?` | 1 |
| File-length ratchet, final code (cwd: worktree) | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-225.r1/logs/gate-filelength-1.log 2>&1; echo FILELENGTH_EXIT=$?` | 1 |
| Complexity ratchet, whole change reversed (cwd: worktree) | `git diff > .../full.patch && git apply -R .../full.patch; bash tests/complexity-gate.sh --check > .../gate-complexity-base.log 2>&1; echo BASE_COMPLEXITY_EXIT=$?; git apply .../full.patch` | 1 |
| File-length ratchet, whole change reversed (cwd: worktree) | same reversal; `bash tests/file-length-gate.sh --check > .../gate-filelength-base.log 2>&1; echo BASE_FILELENGTH_EXIT=$?` | 1 |

### Ratchet gates: honest note

Both gates exit 1 on this branch, **with and without my change**: the `REGRESSION:` lines in
`gate-complexity-1.log` / `gate-complexity-base.log` are identical (diff of the two `REGRESSION:` sets is empty),
and likewise for the file-length pair. Neither log mentions `SampleRangeFields.cpp` or
`SampleEditorDialogTest.cpp`, and the changed function `frameFromText` is not listed over CCN 10 and neither file
crosses 500 lines (264 and 474). The failures are pre-existing branch-wide ratchet debt (`ControlExportPresetSupport.cpp`
CCN 14, `DawProjectReadTracks.cpp::parseNotes` CCN 13, twelve test files over 500 lines, ...). No gate limit or
baseline was touched.

The wider `ctest -R Sample` sweep (`test-family-1.log`) was also inspected: 8 of its 10 entries report
`Unable to find executable` because those test binaries are not built in this tree, and
`NeuralAmpWavRunSampleRateWarning` fails on a missing neuralamp executable inside
`tests/neuralamp-wav-run-sample-rate-warning.py`; neither touches the sample editor's range fields. The built and
relevant family members (above) all pass.

## Files in the commit

- `src/gui/SampleRangeFields.cpp`
- `tests/src/gui/SampleEditorDialogTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p12-225.md`