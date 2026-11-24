ALREADY-FIXED

# cluster new-p9-536: the analyzer queries GUI visibility on the audio thread

Duplicate of cluster new-p12-279, whose fix is already on this branch. `isViewVisible()` no longer
reads a plain `bool` shared with the GUI thread: the flag is a `std::atomic<bool>`, so the audio
callback performs one atomic load and touches no Qt state and no widget.

## The finding, checked against the tree

`plugins/SpectrumAnalyzer/Analyzer.cpp:96` (historical line 96, unchanged):

```cpp
	// Skip processing if the controls dialog isn't visible, it would only waste CPU cycles.
	if (m_controls.isViewVisible())
```

That is the only `m_controls` access in the whole file (line 61/62 are constructor initialisers,
not the callback), so the finding named the right call site. The callback body is otherwise just
`m_inputBuffer.write(...)` plus, under `SA_DEBUG` only, a lock-free `TimingCollector::record()`.

The definition it calls, `include/EffectControls.h:81`:

```cpp
	std::atomic<bool> m_viewVisible;
```

read at `include/EffectControls.h:68-71` (`isViewVisible()`) and written at `include/EffectControls.h:63-66`
(`setViewVisible()`) by its only writer, the GUI thread, in `src/gui/EffectView.cpp:128` and
`src/gui/EffectView.cpp:133` when an effect control dialog opens or closes. Both accessors are the
implicit `std::atomic<bool>` load/store, so the query is a single lock-free atomic read and never
touches a widget, a `QObject` or any other GUI state. The race the finding describes ("the callback
can race with GUI state changes") is closed; "touch Qt from the audio thread" never happened —
`isViewVisible()` reads one member and nothing else.

Provenance: `git log --oneline -6 -- include/EffectControls.h` reports
`9b5d984c14 fix(rt): EffectControls visibility flag is atomic so the audio thread can read it safely`,
which is in the ancestry of this branch's HEAD (`6e277efd30`). That commit changed
`bool m_viewVisible` to `std::atomic<bool> m_viewVisible`, added `<atomic>`/`<type_traits>`, added
the `LMMS_TESTING` seam `viewVisibleStorageIsAtomic()` (`include/EffectControls.h:83-89`) and added
the regression test. Its own report, `docs/qwen-fixes-2026-10/cluster-new-p12-279.md`, records the
same root cause, including this plugin by name.

Every sibling of the call site reads the same atomic, so nothing is left inconsistent:
`plugins/Eq/EqEffect.cpp:184`, `plugins/Eq/EqEffect.cpp:294`, `plugins/Vectorscope/Vectorscope.cpp:64`
and `plugins/SpectrumAnalyzer/Analyzer.cpp:96`. All four are `processImpl()` readers of one
`EffectControls` flag; either all are fixed by the header change or none are.

## Regression test

`tests/src/core/EffectControlsVisibilityTest.cpp` is that test, registered at `tests/CMakeLists.txt:110`
and as ctest target `EffectControlsVisibilityTest`:

- `viewVisibleStorageMustBeAtomic` (lines 55-62) asserts the storage really is `std::atomic<bool>`
  through the `LMMS_TESTING` seam, failing at line 58 with the message naming `processImpl()`.
- `setAndGetViewVisibleRoundTrips` (lines 64-72) asserts the normal get/set path still round-trips.

I re-ran it here, and I reverted the fix by hand to prove the test still catches this exact bug
(the negative control edits one line, `std::atomic<bool> m_viewVisible;` → `bool m_viewVisible;`,
and `git status --porcelain` is empty again afterwards).

## Commands and exit codes (this session, logs in the run's `logs/` folder)

| # | Command | Exit | Log |
|---|---------|------|-----|
| 1 | `cmake --build build --target EffectControlsVisibilityTest -j4` | 0 | `build-1.log` |
| 2 | `ctest -R EffectControlsVisibilityTest --output-on-failure` (cwd `build/tests`) | 0 | `test-1.log` |
| 3 | `cmake --build build --target analyzer -j4` (the finding's plugin compiles against the atomic header) | 0 | `build-2.log` |
| 4 | Revert `include/EffectControls.h:81` to `bool`, then `cmake --build build --target EffectControlsVisibilityTest -j4` | 0 | `build-3-negative-control.log` |
| 5 | `ctest -R EffectControlsVisibilityTest --output-on-failure` (cwd `build/tests`) with the revert in place | 8 | `test-2-negative-control.log` |
| 6 | Restore `std::atomic<bool>` (verified by empty `git status --porcelain`), `cmake --build build --target EffectControlsVisibilityTest analyzer -j4` | 0 | `build-4-restored.log` |
| 7 | `ctest -R EffectControlsVisibilityTest --output-on-failure` (cwd `build/tests`) restored | 0 | `test-3-restored.log` |
| 8 | `python3 .../qwen/feature_check.py <worktree>` | 0 | `feature-check-1.log` |

The negative control's failure text (`test-2-negative-control.log`):

```
FAIL!  : EffectControlsVisibilityTest::viewVisibleStorageMustBeAtomic() 'controls.viewVisibleStorageIsAtomic()' returned FALSE. (EffectControls::m_viewVisible must be std::atomic<bool> because processImpl() reads it on the audio thread while the GUI thread writes it via setViewVisible())
   Loc: [../../tests/src/core/EffectControlsVisibilityTest.cpp(58)]
Totals: 3 passed, 1 failed, 0 skipped, 0 blacklisted, 0ms
0% tests passed, 1 tests failed out of 1
```

Gate table (`feature-check-1.log`), unpiped exit code 0:

```
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

## Why no product change

The verdict is ALREADY-FIXED, so this job ships no product hunk: a second `std::atomic` conversion
would be a no-op diff, and re-touching `include/EffectControls.h` would only churn the
`tests/upstream-modifications.txt` entry for an inherited file. Nothing here adds an allocation,
lock, wait, log or command: the audio path is byte-identical to the code that is already tested.

## Self-check

1. **The regression test FAILS with the product change reverted and PASSES with it (exit codes).**
   Reverted `include/EffectControls.h:81` to `bool m_viewVisible;`: rebuild exit 0
   (`build-3-negative-control.log`), `ctest` exit **8**, `viewVisibleStorageMustBeAtomic` FAILS at
   `tests/src/core/EffectControlsVisibilityTest.cpp(58)` (`test-2-negative-control.log`). Restored
   `std::atomic<bool>`: rebuild exit 0 (`build-4-restored.log`), `ctest` exit **0**, 1/1 passed
   (`test-3-restored.log`). Because I shipped no product hunk, the "patch" in the manifest is the
   fix commit already on the branch, `9b5d984c14`; its inverse is exactly the one-line revert above.

2. **The test is deterministic.** It uses no wall clock, sleep, thread or timeout. It reads one
   compile-time fact through a test seam, `viewVisibleStorageIsAtomic()`
   (`include/EffectControls.h:83-89`, guarded by `LMMS_TESTING`), which returns
   `std::is_same_v<decltype(m_viewVisible), std::atomic<bool>>`, plus a single-threaded
   set/get round-trip. There is no race to lose and no timing to flake: the assertion is a constant
   expression evaluated before `main()` runs.

3. **The fix is at the cause and complete; every caller and sibling checked.**
   The cause is the shared storage, not the call site, and it lives in the base class
   `EffectControls`, so the header change covers all four audio-thread readers —
   `plugins/SpectrumAnalyzer/Analyzer.cpp:96`, `plugins/Eq/EqEffect.cpp:184`,
   `plugins/Eq/EqEffect.cpp:294`, `plugins/Vectorscope/Vectorscope.cpp:64` — with no per-plugin
   divergence. The only writer, `setViewVisible()`, has one caller family in `src/gui/EffectView.cpp:128,133`
   (GUI thread). The wider type is `bool`: implicit `operator bool()` on load and `operator=(bool)`
   on store keep `isViewVisible()`/`setViewVisible()` signatures and semantics identical, so no
   caller had to change and none did. The analyzer plugin rebuilds clean against the atomic header
   (`build-2.log`, exit 0).

4. **No collateral damage.** `EffectControls`' member layout is the only thing touched by the fix
   commit and `m_viewVisible` is private with no serializer: `SaControls::saveSettings`/
   `loadSettings` (`plugins/SpectrumAnalyzer/SaControls.cpp:133-156` and `:157-182`) persist 18 named models and
   never the visibility flag, and visibility is not a saved or undoable value in any effect. Old
   projects therefore load byte-identically; `no-upstream-regression` and `all-sources-reproduce`
   both PASS above. The fix adds no limit or check, so no caller can be newly rejected.

5. **Evidence for ALREADY-FIXED.** `include/EffectControls.h:81` is
   `std::atomic<bool> m_viewVisible;`, read at `include/EffectControls.h:68-71` and written at
   `include/EffectControls.h:63-66`, introduced by commit `9b5d984c14` which is in this branch's
   history, and covered by the passing `EffectControlsVisibilityTest` (ctest exit 0) whose negative
   control I ran myself (ctest exit 8). The audio callback at `plugins/SpectrumAnalyzer/Analyzer.cpp:96`
   is therefore a lock-free atomic read with no Qt access, which is exactly the fix the finding asks
   for ("Publish visibility through an audio-safe atomic").

## Other findings

- `plugins/SpectrumAnalyzer/SaProcessor.cpp` reads `SaControls` models from the analysis worker
  thread, not the audio thread — e.g. `SaProcessor.cpp:119,121,291,365,368,399` inside
  `SaProcessor::analyze()` (started on `m_processorThread`). That is a GUI/worker race on
  `FloatModel`/`BoolModel`/`ComboBoxModel` values and is a different concern from this finding
  (visibility on the audio thread). Not touched here; it needs its own job and a different design
  (a published snapshot handed to the worker), not a one-line change.
- Local TSan does not run on this box, so the absence of a race is argued from the design
  (`std::atomic<bool>` load/store, one writer on the GUI thread) and from the compile-time
  assertion in the test, not from a dynamic race detector.
