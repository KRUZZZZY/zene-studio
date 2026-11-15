IMPLEMENTED

# Q050-471 Per-clip effect-chain storage

Feature record: `docs/050-features/Q050-471.md`. Logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-471.r1/logs/` (numbered).

## What changed

- Test only: `ClipEffectsTest::theStorageRoundTripsAndOmitsEmptyChains` (tests/src/core/ClipEffectsTest.cpp, commit
  `66f7d25726`). It saves a song, counts `<fxchain>` children on `<sampleclip>` elements, round-trips a two-device
  chain through a project file, and checks that an empty chain writes nothing.
- Docs: the feature record and this cluster record.
- The feature shipped under Q050-111. No product code, command, command metadata, snapshot, GUI or manual change in
  this unit. The storage code it covers (src/core/SampleClipEffects.cpp:59-80, and the `saveClipEffects` call in
  SampleClip.cpp) already existed.

## Acceptance

| Criterion | Test | Command | Exit | Log |
| --- | --- | --- | --- | --- |
| Storage round-trips with order, settings, bypass; empty chain writes no `<fxchain>` | `ClipEffectsTest::theStorageRoundTripsAndOmitsEmptyChains` | ctest `^ClipEffectsTest$` after build | 0 (green); 8 (red, save hook reverted) | 008-test-green-1, 006-test-red-1 |
| Headless state read through `dsp.get_state` | same | same | same | same |
| Clip UI edits the same chain (existing) | `ClipEffectsDialogTest::theClipMenuOpensAWindowWhoseControlsEditTheClipsChain` | ctest `^ClipEffectsDialogTest$` | 0 | 004-test-2, 008-test-green-1 |
| Agent edits one undo step each (existing) | `ClipEffectsTest::theAgentEditsTheChainOneUndoStepEach` | ctest `^ClipEffectsTest$` | 0 | 003-test-1, 008-test-green-1 |
| Old clip without chain loads empty (existing) | `ClipEffectsTest::aClipWithoutAChainLoadsNone` | ctest `^ClipEffectsTest$` | 0 | 008-test-green-1 |

Totals: `100% tests passed out of 1` (ClipEffectsTest, log 003-test-1); `100% tests passed out of 1`
(ClipEffectsDialogTest, log 004-test-2); `100% tests passed out of 3` (ClipEffectsTest, ClipEffectsDialogTest,
ClipSerialisationTest, log 008-test-green-1). Build logs 002-build-2 and 007-build-green-1 end at `Built target`.

## Revert method

The save hook is `saveClipEffects(_doc, _this);` at src/core/SampleClip.cpp:1072. It was commented out in the tree
and the test executables rebuilt (log 005-build-red-1, built). ClipEffectsTest then ran (log 006-test-red-1):

- `theStorageRoundTripsAndOmitsEmptyChains` fails at `tests/src/core/ClipEffectsTest.cpp(362)`, `withChain.second`
  Actual 0, Expected 1.
- The pre-existing `theChainSurvivesSaveAndReopen` also fails, at `tests/src/core/ClipEffectsTest.cpp(265)`,
  `devices.size()` Actual 0, Expected 2.
- Totals: `7 passed, 2 failed`; ctest `0% tests passed, 1 tests failed out of 1`; exit 8.

The line was restored from a copy, the executables rebuilt, and the three tests re-run: `100% tests passed out of 3`
(log 008-test-green-1).

## Self-check

- **New test fails with the save hook reverted, passes restored:** yes. Red: log 006-test-red-1, exit 8. Restored:
  log 008-test-green-1, exit 0.
- **Each criterion proven by a test, with exit:** see the table above. The storage criterion is proven headlessly by
  the new test; the GUI and agent paths are proven by the existing tests named in it.
- **One undo step per edit; existing reversibility classes unchanged:** no command was added or changed, so no
  reversibility class changed. The new test's unloads use the existing `plugin.unload`. ReversibilityContractTest was
  not re-run in this unit. The undo-per-edit proof is the existing `theAgentEditsTheChainOneUndoStepEach` (log
  008-test-green-1).
- **Realtime:** this unit adds no product code on the audio path. The existing `processClipEffects` is
  allocation-free, and `ClipEffectsTest::theClipEffectsAllocateNothingPerPeriod` covers it (PASS in the red run,
  log 006-test-red-1; the executable passes with the change, log 008-test-green-1).
- **Old projects load and render unchanged; new data round-trips:** `aClipWithoutAChainLoadsNone` passes (log
  008-test-green-1). The new test checks that an untouched song writes zero `<fxchain>` elements, and that a
  two-device chain round-trips and an emptied chain again writes none.
- **Completeness through the real user and agent paths, including empty-chain and post-removal edge cases:** the
  agent path is the new test itself (`plugin.load`, `plugin.move`, `plugin.bypass` and `plugin.unload` with
  `target: clip-<n>`). The post-removal edge case is in the same test: after both devices are unloaded the next save
  writes no `<fxchain>` and the reopened clip has no effects. The empty-chain case is the untouched song at the
  start of the test. The user path is the existing `ClipEffectsDialogTest`, not the new test.
- **GUI test drives the real widget and the same registered command:** the GUI check is the existing
  `ClipEffectsDialogTest` (Q050-111), which drives the menu and buttons. This unit did not change it.
- **Portability:** no product API added. The new test uses `QFile`, `QDomDocument`, `QDomNodeList` and `std::pair`.
  The test target compiled with the new slot present (log 005-build-red-1, built). No Qt 6-only or POSIX-only call.

### Expected submit_check WARNs

`submit_check.py` may print WARNs for "no GUI change", "no command change" and "no manual change". These are not
failures. The GUI, the command and the manual paths already exist from Q050-111 (the manual documents clip effects at
`data/manual/index.html` lines 180-186), so this unit has nothing to add there.
