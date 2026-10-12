# Cluster new-p12-37

## Verdict: FALSE-POSITIVE

### Finding

> `deletesWindow` only detects a deleted window when the deletion starts at or before `sourceIn`. If the
> deletion starts earlier and ends after `sourceOut`, the check misses it; `windowAfter` then treats the
> empty window as a reason to play the whole remaining source. Clip plays `[10, 20)`, `sample.edit_range`
> deletes `[5, 25)` -> the clip ends up playing the entire remaining buffer.
> Fix: refuse any delete where `from <= window.sourceIn` and `to >= window.sourceOut`.

### Evidence

* `src/core/ControlCommandsSampleRange.cpp:96-99` - the guard is

  ```cpp
  return op == QLatin1String("delete") && window.sourceIn >= from && window.sourceOut <= to;
  ```

  `window.sourceIn >= from` **is** `from <= sourceIn`, and `window.sourceOut <= to` **is** `to >= sourceOut`.
  The proposed fix is the condition already in the code; the finding's own scenario ("the deletion starts
  earlier [than `sourceIn`] and ends after `sourceOut`") is precisely the case it catches: window `[10, 20)`,
  delete `[5, 25)` -> `10 >= 5` and `20 <= 25` -> `editRange` returns
  `ControlErrorKind::Refused` at `src/core/ControlCommandsSampleRange.cpp:119-122`, leaving the sample
  buffer and the window untouched. Nothing is retargeted.

* `src/core/ControlCommandsSampleRange.cpp:82-93` (`windowAfter`) - the `kept.empty() ? SampleWindow::full(frames)`
  fallback is not reachable for an accepted delete either. `shifted` is monotone non-decreasing, and the
  only way `shifted(sourceIn) == shifted(sourceOut)` (an empty `kept`) is when `from <= sourceIn` and
  `sourceOut <= to`, i.e. exactly the case `deletesWindow` refuses. The clamp cannot empty it either:
  `shifted(sourceOut) - cut <= size - cut == frames`, so `kept` stays inside `[0, frames]`.

* Proof by mutation: weakening the guard to `window.sourceIn == from && window.sourceOut == to` (the
  "missed" variant the finding describes) makes the scenario succeed instead of being refused, and the
  new test catches it - see the RED row below. With the shipped condition it is refused.

### Change made

No product code was changed. `deletesWindow` already implements the requested rule.

A characterisation test was added so the refusal is covered by ctest:
`tests/src/core/AudioAnalysisTest.cpp:153-174` `aDeleteOverTheWholeWindowIsRefused()`

* refuses `delete [5, 25)` on a clip whose window is `[10, 20)` (`Refused`), with the buffer length and
  the window unchanged;
* accepts `delete [12, 18)` (frames of the window survive) and checks the window closes up over what is
  left (`[10, 14)`, buffer `size - 6`), not over the whole remaining source.

The file is already registered in `tests/fork-sources.txt:1906`; no new files were added.

### Commands run (all output in the log folder, EXIT codes as seen)

| Step | Command | EXIT |
|------|---------|------|
| Build (baseline, unmodified product) | `cmake --build build --target AudioAnalysisTest > logs/build-1.log 2>&1` | 0 |
| GREEN, unmodified product | `ctest -R AudioAnalysisTest --output-on-failure > logs/test-1.log 2>&1` (from `build/tests`) | 0 |
| Build with the guard mutated to the "missed" variant | `cmake --build build --target AudioAnalysisTest > logs/build-2.log 2>&1` | 0 |
| RED with the mutated guard (test detects the finding's failure mode) | `ctest -R AudioAnalysisTest --output-on-failure > logs/test-2.log 2>&1` | 8 |
| Build after restoring `ControlCommandsSampleRange.cpp` | `cmake --build build --target AudioAnalysisTest > logs/build-3.log 2>&1` | 0 |
| GREEN after restore | `ctest -R AudioAnalysisTest --output-on-failure > logs/test-3.log 2>&1` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > logs/gate-complexity-1.log 2>&1` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > logs/gate-filelength-1.log 2>&1` | 0 |

RED detail (`logs/test-2.log:17-20`):

```
FAIL!  : AudioAnalysisTest::aDeleteOverTheWholeWindowIsRefused() Compared values are not the same
   Actual   (whole.errorKind)          : 0
   Expected (ControlErrorKind::Refused): 5
```

Affected family: `AudioAnalysisTest` - 1 ctest test, 6 QTest functions (`./AudioAnalysisTest -functions`,
`logs/functions-1.log`), all passing.