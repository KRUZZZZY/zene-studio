FIXED
# Cluster new-p16-164: a non-finite peak no longer latches the peak indicator

A `+inf` (or NaN) peak sample latched the mixer's peak readout at `inf` until it was reset by hand. Both the display widget and the fader that feeds it now ignore non-finite input. Finite peaks, including over-unity ones, are handled as before.

## (1) The bug is real

- `src/gui/widgets/PeakIndicator.cpp:52-60` (before the fix): `updatePeak` stored any value greater than `m_peak`. `+inf > m_peak` is true, so `m_peak` became `inf` and the readout showed `inf`. Every later finite peak (0.5, 0.8) is smaller than `inf`, so the readout stayed `inf` until `resetPeakToMinusInf()`.
- `src/gui/widgets/Fader.cpp:495-513` (before the fix): `setPeak` set `targetPeak = inf`. Because `inf >= persistentPeak`, it set `persistentPeak = inf` and emitted `peakChanged(inf)`. `MixerChannelView.cpp:181` connects that signal to `PeakIndicator::updatePeak`. The decay step `persistentPeak = qMax(0, persistentPeak - 0.05)` leaves `inf` unchanged, so the fader re-emits `inf` on every decay tick.
- Consequence: a guard only in `PeakIndicator` cannot recover the strip, because the fader keeps re-emitting `inf`. The fader needs its own guard.
- Red evidence: `logs/test-red.log` (EXIT=8, fix reversed) shows `"inf"` where `"-inf"` was expected and `"inf"` where `"-1.9"` was expected.

## (2) The fix

- `src/gui/widgets/PeakIndicator.cpp:54-57`: `if (!std::isfinite(peak)) return;` at the top of `updatePeak`, before the comparison. `<cmath>` is included.
- `src/gui/widgets/Fader.cpp:497-500`: the same guard at the top of `Fader::setPeak`, before `targetPeak` and `persistentPeak` are touched. `<cmath>` is included.
- Callers, from `logs/callers-1.log`:
  - `MixerView.cpp:1035-1052` calls `setPeak_L`/`setPeak_R` with the mixer channel peaks and with the decayed value `x / fallOff`. Non-finite inputs reach `Fader::setPeak` and are dropped there.
  - `MixerViewOptions.cpp:463-464` calls `setPeak_L`/`setPeak_R` with `std::max(sample, fader->getPeak_*() / fallOff)`. The same guard applies.
  - `MixerChannelView.cpp:181` connects `Fader::peakChanged` to `PeakIndicator::updatePeak`. The `PeakIndicator` guard covers this path.
- Finite over-unity peaks are still accepted: the test asserts `2.0` reads `"6.0"` (see §3).
- Upstream declarations, `tests/upstream-modifications.txt:5991-5992`: one entry per widget file (`src/gui/widgets/Fader.cpp` and `src/gui/widgets/PeakIndicator.cpp`), each naming `cluster new-p16-164` and the held-by test `tests/src/gui/MixerMeteringTest.cpp`.

## (3) Tests

Both are in `tests/src/gui/MixerMeteringTest.cpp`.

- `:933` `anInfinitePeakDoesNotLatchTheIndicator`: calls `PeakIndicator::updatePeak` directly with `+inf`, `0.5`, NaN, then `2.0`. It expects `"-inf"`, `"-6.0"` and `"6.0"`.
- `:946` `anInfiniteChannelPeakDoesNotLatchTheStripMeter`: sets up a mixer channel and its strip. It feeds peak `0.5` through the engine channel and calls `tick()`, expecting `"-6.0"`. It then feeds `+inf`, ticks, feeds `0.8`, ticks, and expects `"-1.9"`. This drives the fader-to-indicator path.

With the fix applied, `logs/test-green.log` (EXIT=0) shows `Totals: 36 passed, 0 failed` for `MixerMeteringTest`. With the fix reversed, `logs/test-red.log` (EXIT=8) shows the two `inf` failures listed in §1.

Determinism: the tests use injected `inf`/NaN and direct fader values with `tick()`. They contain no sleeps.

## (4) Command table

Commands run from the worktree root. `logs/` is the run's log folder, outside the worktree. Each command ran through the quiet runner with the same redirects, so each log holds the full output and ends with its `EXIT=` line.

| Step | Command | EXIT | Evidence |
|---|---|---|---|
| fix patch | `git diff HEAD^ HEAD -- src/gui/widgets/PeakIndicator.cpp src/gui/widgets/Fader.cpp > logs/fix-red.patch; echo EXIT=$? > logs/fix-red-apply.log` | 0 | `logs/fix-red-apply.log` line 1: `EXIT=0` |
| reverse fix | `git apply -R logs/fix-red.patch; echo EXIT=$? >> logs/fix-red-apply.log` | 0 | `logs/fix-red-apply.log` line 2: `EXIT=0` |
| build-red | `cmake --build build --target MixerMeteringTest -j4 > logs/build-red.log 2>&1; echo EXIT=$? >> logs/build-red.log` | 0 | `logs/build-red.log`: `[100%] Built target MixerMeteringTest`, last line `EXIT=0` |
| test-red | `ctest -R '^MixerMeteringTest$' --output-on-failure > logs/test-red.log 2>&1; echo EXIT=$? >> logs/test-red.log` (in `build/tests`) | 8 | `logs/test-red.log`: `"inf"` vs `"-inf"` and `"inf"` vs `"-1.9"`; last line `EXIT=8` |
| reapply fix | `git apply logs/fix-red.patch; echo EXIT=$? >> logs/fix-red-apply.log` | 0 | `logs/fix-red-apply.log` line 3: `EXIT=0`; `git status --short` empty afterwards |
| build-green | `cmake --build build --target MixerMeteringTest -j4 > logs/build-green.log 2>&1; echo EXIT=$? >> logs/build-green.log` | 0 | `logs/build-green.log`: last line `EXIT=0` |
| test-green | `ctest -R '^MixerMeteringTest$' -V > logs/test-green.log 2>&1; echo EXIT=$? >> logs/test-green.log` (in `build/tests`) | 0 | `logs/test-green.log`: `Totals: 36 passed, 0 failed`; last line `EXIT=0` |
| feature gate | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p16-164 > logs/gates-2.log 2>&1; echo EXIT=$? >> logs/gates-2.log` | 0 | `logs/gates-2.log`: `RESULT: PASS`; last line `EXIT=0` |
| registration gate | `bash tests/fork-sources-gate.sh > logs/registration-2.log 2>&1; echo EXIT=$? >> logs/registration-2.log` | 0 | `logs/registration-2.log`: `PASS: every tracked source in scope is registered`; last line `EXIT=0` |
| upstream declaration gate | `bash tests/no-upstream-regression-gate.sh > logs/upstream-declaration-2.log 2>&1; echo EXIT=$? >> logs/upstream-declaration-2.log` | 0 | `logs/upstream-declaration-2.log`: last line `EXIT=0` |
| diff check | `git diff --check > logs/diff-check-4.log 2>&1; echo EXIT=$? >> logs/diff-check-4.log` | 0 | `logs/diff-check-4.log`: only `EXIT=0` |
| screenshot | `capture-peak-indicator.py build/zene logs/peak-indicator.png` | 0 | `logs/screenshot-1.log`: `mixer panel visible: True`, `width=1600 height=1000`, `file_exists=True`, `PASS`, `EXIT=0` |
| sha256 | `sha256sum logs/peak-indicator.png` | 0 | `logs/peak-indicator.sha256`: `a03e1709148f96d0bef34a0f1e36530e59e63551edd6e5e3b1fd07e2efd1193c`, `EXIT=0` |

The screenshot shows the mixer panel at 1600x1000. It does not show a readable peak-indicator reading. Only the Master strip is visible, and no instrument strip was added or played. The image is therefore evidence that the mixer panel renders, not that the readout is correct. The behaviour proof is the RED/GREEN test table above.

## Self-check

- **Reverted-product exit codes:** with the fix reversed (`logs/fix-red-apply.log` line 2, EXIT=0), `logs/test-red.log` ends `EXIT=8` and shows the `inf` readout in both places. With the fix reapplied (`logs/fix-red-apply.log` line 3, EXIT=0), `logs/test-green.log` ends `EXIT=0` with 36 passed.
- **Determinism:** the tests inject `+inf` and NaN directly, use direct fader values, and advance with `tick()`. There are no sleeps.
- **Completeness:** both guards are in place (`PeakIndicator.cpp:54`, `Fader.cpp:497`). The callers listed in §2 all reach one of them.
- **No collateral damage:** the test asserts `2.0` still reads `"6.0"`. The commit changes the two widget files, the test file, the upstream-modifications list and this report. No saved format, undo path or control command is changed.
- **ALREADY-FIXED / FALSE-POSITIVE do not apply:** the RED runs show the readout latching `inf` in this tree. The bug is not already fixed. The RED output is real behaviour, not a test artefact: the same `inf` appears in the widget and in the strip.

## Other findings

None in scope.
