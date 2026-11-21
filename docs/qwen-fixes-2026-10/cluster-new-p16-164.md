FIXED
# Cluster new-p16-164: a non-finite peak no longer latches the peak indicator

A `+inf` (or NaN) peak sample latched the mixer's peak readout at `inf` until it was reset by hand. Both the display widget and the fader that feeds it now ignore non-finite input. Finite peaks, including over-unity ones, are handled as before.

## (1) The bug is real

- `src/gui/widgets/PeakIndicator.cpp:52-60` (before the fix): `updatePeak` stored any value greater than `m_peak`. `+inf > m_peak` is true, so `m_peak` became `inf` and the readout showed `inf`. Every later finite peak (0.5, 0.8) is smaller than `inf`, so the readout stayed `inf` until `resetPeakToMinusInf()`.
- `src/gui/widgets/Fader.cpp:495-513` (before the fix): `setPeak` set `targetPeak = inf`. Because `inf >= persistentPeak`, it set `persistentPeak = inf` and emitted `peakChanged(inf)`. `MixerChannelView.cpp:181` connects that signal to `PeakIndicator::updatePeak`. The decay step `persistentPeak = qMax(0, persistentPeak - 0.05)` leaves `inf` unchanged, so the fader re-emits `inf` on every decay tick.
- Consequence: a guard only in `PeakIndicator` cannot recover the strip, because the fader keeps re-emitting `inf`. The fader needs its own guard.
- Red evidence: `logs/test-1b.log` shows `"inf"` where `"-inf"` was expected (first test) and `"inf"` where `"-1.9"` was expected (strip test). The packet records EXIT=8 for this run.

## (2) The fix

- `src/gui/widgets/PeakIndicator.cpp:54-57`: `if (!std::isfinite(peak)) return;` at the top of `updatePeak`, before the comparison. `<cmath>` is included.
- `src/gui/widgets/Fader.cpp:497-500`: the same guard at the top of `Fader::setPeak`, before `targetPeak` and `persistentPeak` are touched. `<cmath>` is included.
- Callers, from `logs/callers-1.log`:
  - `MixerView.cpp:1035-1052` calls `setPeak_L`/`setPeak_R` with the mixer channel peaks and with the decayed value `x / fallOff`. Non-finite inputs reach `Fader::setPeak` and are dropped there.
  - `MixerViewOptions.cpp:463-464` calls `setPeak_L`/`setPeak_R` with `std::max(sample, fader->getPeak_*() / fallOff)`. The same guard applies. The grep places these lines at 463-464, inside the packet's 439-465 range.
  - `MixerChannelView.cpp:181` connects `Fader::peakChanged` to `PeakIndicator::updatePeak`. The `PeakIndicator` guard covers this path.
- Finite over-unity peaks are still accepted: the test asserts `2.0` reads `"6.0"` (see §3).

## (3) Tests

Both are in `tests/src/gui/MixerMeteringTest.cpp`.

- `:933` `anInfinitePeakDoesNotLatchTheIndicator`: calls `PeakIndicator::updatePeak` directly with `+inf`, `0.5`, NaN, then `2.0`. It expects `"-inf"`, `"-6.0"` and `"6.0"`.
- `:946` `anInfiniteChannelPeakDoesNotLatchTheStripMeter`: sets up a mixer channel and its strip. It feeds peak `0.5` through the engine channel and calls `tick()`, expecting `"-6.0"`. It then feeds `+inf`, ticks, feeds `0.8`, ticks, and expects `"-1.9"`. This drives the fader-to-indicator path.

Both are PASS in `logs/test-2-verbose.log` and `logs/test-4-verbose.log`. This session's `logs/test-5.log` shows the whole `MixerMeteringTest` entry passing (100%).

Determinism: the tests use injected `inf`/NaN and direct fader values with `tick()`. They contain no sleeps.

## (4) Command table

| Step | Command | EXIT | Evidence |
|---|---|---|---|
| build-5 | `cmake --build build --target zene -j4` | 0 | quiet-run `build-5: exit 0`; log `duet/12-worker/tmp/001-build-5.log` |
| build-1b, build-2, build-3, build-4 | MixerMeteringTest build, per run | not recorded in the packet; not in the log files | `logs/build-1b.log` ... `build-4.log` each end with `Built target MixerMeteringTest` |
| test-1 | MixerMeteringTest (earlier run) | not recorded in the packet | `logs/test-1.log`: `34 passed, 2 failed`, `inf` vs `-inf` |
| test-1b | MixerMeteringTest, RED | 8 (packet record; no EXIT line in log) | `logs/test-1b.log`: actual `"inf"` vs expected `"-inf"`; actual `"inf"` vs expected `"-1.9"` |
| test-2 | MixerMeteringTest, GREEN | 0 (packet record) | `logs/test-2.log`: `100% tests passed`; `logs/test-2-verbose.log`: `Totals: 36 passed, 0 failed` |
| fix.patch | reverse and re-apply of the fix (packet's "reverse/apply") | 0 (packet record) | `logs/fix.patch` (50 lines); the exact command is not in any log |
| test-3 | MixerMeteringTest, RED | 8 (packet record) | `logs/test-3.log`: same `inf` vs `-inf` and `inf` vs `-1.9` lines |
| test-4 | MixerMeteringTest, GREEN | 0 (packet record) | `logs/test-4-verbose.log`: `Totals: 36 passed, 0 failed` |
| test-5-build | `cmake --build build --target MixerMeteringTest -j4` | 0 | quiet-run `test-5-build: exit 0`; log `logs/test-5-build.log` |
| test-5 | `ctest -R MixerMeteringTest --output-on-failure` | 0 | quiet-run `test-5: exit 0`; log `logs/test-5.log`: `100% tests passed out of 1` |
| gates-1 | feature gates | 0 (packet record) | `logs/gates-1.log`: `RESULT: PASS` |
| registration-1 | registration check | 0 (packet record) | `logs/registration-1.log`: `PASS: every tracked source in scope is registered` |
| upstream-declaration-1 | upstream declaration check | 0 (packet record) | `logs/upstream-declaration-1.log` |
| diff-check | `git diff --check` | 0 (packet record) | `logs/diff-check-2.log` and `diff-check-3.log` are empty; no EXIT line |
| screenshot-1 | `capture-peak-indicator.py build/zene logs/peak-indicator.png` | 0 | `logs/screenshot-1.log`: mixer visible `True`, `width=1600 height=1000`, `file_exists=True`, `PASS`, `EXIT=0` |
| sha256 | `sha256sum logs/peak-indicator.png` | 0 | `logs/peak-indicator.sha256`: `a03e1709148f96d0bef34a0f1e36530e59e63551edd6e5e3b1fd07e2efd1193c` |

The screenshot shows the mixer panel at 1600x1000. It does not show a readable peak-indicator reading. Only the Master strip is visible, and no instrument strip was added or played. The image is therefore evidence that the mixer panel renders, not that the readout is correct. The behaviour proof is the RED/GREEN test table above.

## Self-check

- **Reverted-product exit codes:** RED runs test-1b and test-3 record EXIT=8, with the `inf` readout in each log. GREEN runs test-2 and test-4 record EXIT=0 with 36 passed. This session's test-5 (with the fix in place) is EXIT=0.
- **Determinism:** the tests inject `+inf` and NaN directly, use direct fader values, and advance with `tick()`. There are no sleeps.
- **Completeness:** both guards are in place (`PeakIndicator.cpp:54`, `Fader.cpp:497`). The callers listed in §2 all reach one of them.
- **No collateral damage:** the test asserts `2.0` still reads `"6.0"`. The diff touches only the two widget files, the test file, the upstream-modifications list and this report. No saved format, undo path or control command is changed (`git diff --stat`: 4 files, 59 insertions).
- **ALREADY-FIXED / FALSE-POSITIVE do not apply:** the RED runs show the readout latching `inf` in this tree. The bug is not already fixed. The RED output is real behaviour, not a test artefact: the same `inf` appears in the widget and in the strip.

## Other findings

None in scope.
