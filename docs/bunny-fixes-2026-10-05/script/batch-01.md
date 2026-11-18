# Space Bunny fixer lane `script` — batch-01 report

Worktree `zene-bunny-script`, branch `bugs/bunny-script-2026-10-05`. Six findings, two fixed,
one already fixed, one false positive, one skipped, one real but untestable in this build tree.

### Cluster 5201 — ONNX errors can escape the separator

Verdict: REAL-UNTESTED

Evidence: `src/core/OnnxRuntimeStemSeparator.cpp:176` constructs `Ort::Session` and
`src/core/OnnxRuntimeStemSeparator.cpp:241` calls `session.Run` with no `catch`; `rg -n 'catch|Ort::Exception'
src/core/OnnxRuntimeStemSeparator.cpp` returns nothing, so `Ort::Exception` really does escape `separate()`.
It cannot be exercised here: `build/CMakeCache.txt` has `ONNXRuntime_LIBRARY:FILEPATH=ONNXRuntime_LIBRARY-NOTFOUND`,
`CMakeLists.txt:171-176` only defines `LMMS_HAVE_ONNXRUNTIME` when the library is found, and
`src/CMakeLists.txt:427` compiles `OnnxRuntimeStemSeparator.cpp` only under that flag
(`cmake --build build --target help` lists no onnx target). No deterministic headless failing test is possible
in this configuration, and rule "RED before product change" forbids touching the code without one.

Commit: none
Test: none
Red/green: none. Classification only:
`rg -n -l 'Ort::Exception|catch' src tests` → no match in `src/core/OnnxRuntimeStemSeparator.cpp`.

### Cluster 5206 — ONNX output tensors are read without validating their runtime shapes

Verdict: ALREADY-FIXED

Evidence: `outputsHoldSegment()` at `src/core/OnnxRuntimeStemSeparator.cpp:99-114` rejects `index >= outputs.size()`
and any output whose `GetElementCount()` is below `stemStride * s + 2 * segment`; it is called at line 243, before
the tensor reads at lines 247-260. `resolveStemOutputs()` at lines 77-94 validates the output count and the stacked
layout. `git log -S outputsHoldSegment` attributes it to commit `57980beef`, already on this branch.

Commit: none
Test: none (pre-existing coverage; the ONNX target cannot be built here, see cluster 5201)
Red/green: none

### Cluster 4445 — Lua renames silently truncate long UTF-8 names

Verdict: FIXED

Evidence: `src/core/ScriptBindings.cpp:207` and `src/core/ScriptBindings.cpp:792` copied the name into the fixed
256-byte `ScriptCommand::text` with `std::strncpy` and no length check, and `src/core/ScriptDawEdit.cpp:83-88`
(`namedDawCommand`) did the same for mixer-channel and effect names. A longer name was stored truncated, and a cut
landing mid-character leaves a broken UTF-8 sequence. Enlarging `ScriptCommandTextSize`
(`include/ScriptEngine.h:54`) would change the command payload schema, so overlong names are now refused:
`setCommandText()` (`include/ScriptEngine.h:110`, defined in `src/core/ScriptDawEdit.cpp:64`) returns false and the
bindings throw `std::runtime_error`, while the DAW path reports `ControlErrorKind::InvalidArgs` through the
existing `logRefusal` channel and leaves the queue without a reply.

Commit: 4508fc221
Test: ScriptBindingsTest::anOverlongNameIsRefusedRatherThanTruncated
Red/green:
- red: `cmake --build build --target zene ScriptBindingsTest > build/bunny-b.log 2>&1` → EXIT=0;
  `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^ScriptBindingsTest$' --output-on-failure > build/bunny-t.log 2>&1` → EXIT=8,
  `FAIL! : ScriptBindingsTest::anOverlongNameIsRefusedRatherThanTruncated() 'throws([&] { track.setName(longName); }, &message)' returned FALSE.`
- green: same build → EXIT=0; same ctest → EXIT=0, `100% tests passed, 0 tests failed out of 1`
  (25 ScriptBindingsTest cases pass, including the DAW trio `ScriptDawBindingTest`, `ScriptCommandBridgeTest`,
  `ScriptStabilisationTest`, all three green).

### Cluster 5331 — Lua model writes do not create undo checkpoints

Verdict: FALSE-POSITIVE

Evidence: `src/core/AutomatableModel.cpp:315-332` — `AutomatableModel::setValue` already calls
`addJournalCheckPoint()` itself at line 324, before mutating, so a write queued by
`LuaFloatModel::setValue` (`src/core/ScriptBindings.cpp:555`) is journalled when the apply side runs it.
A test written to prove this passed on the unmodified tree (3 passed, EXIT=0), so the suggested extra checkpoint
would only double the undo history; it was removed rather than committed.

Commit: none
Test: none (the probe test was removed after it passed pre-fix)
Red/green: none — classification only.

### Cluster 5338 — Several getters can return stale values after a queued write

Verdict: FIXED

Evidence: `LuaPatternClip::lengthTicks` (`src/core/ScriptBindings.cpp:797`), `LuaSong::tempo`
(`src/core/ScriptBindings.cpp:998`), `LuaSong::masterVolume` (`src/core/ScriptBindings.cpp:1012`) and
`LuaEffect::enabled` (`src/core/ScriptDawEffects.cpp:141`) sampled engine state that a queued write had not reached
yet, so a script that wrote then immediately read got the pre-write value. Each now calls
`ScriptEngine::flushCommandsForRead()` first, matching the neighbouring getters that already did.

Commit: a5de51c1b
Test: ScriptBindingsTest::aGetterRightAfterAQueuedWriteSeesThatWrite
Red/green:
- red: `git checkout -- src/core/ScriptBindings.cpp src/core/ScriptDawEffects.cpp`;
  `cmake --build build --target zene ScriptBindingsTest > build/bunny-b6.log 2>&1` → EXIT=0;
  `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^ScriptBindingsTest$' --output-on-failure > build/bunny-t6.log 2>&1` → EXIT=8,
  `FAIL! : ScriptBindingsTest::aGetterRightAfterAQueuedWriteSeesThatWrite() Compared values are not the same`
- green: `cmake --build build --target zene ScriptBindingsTest > build/bunny-b7.log 2>&1` → EXIT=0; same ctest → EXIT=0,
  `100% tests passed, 0 tests failed out of 1`

### Cluster 1659 — `addPattern()` stops adding patterns after the first

Verdict: SKIPPED-SCOPE

Evidence: `LuaPatternStore::addPattern()` (`src/core/ScriptBindings.cpp:938-964`) documents that adding the first
track makes LMMS create pattern 0 implicitly, so it adds a pattern track only when `patternCount() == 0` and then
returns the newest pattern's cell. `tests/src/core/ScriptBindingsTest.cpp` `patternStoreWrapper()` explicitly pins
that with `secondClip.patternIndex() == 0` and `trackIndex() == 1`. The behaviour is deliberate and locked by a test;
changing it is a design change, not a defect fix.

Commit: none
Test: none
Red/green: none

## Gates

`bash tests/complexity-gate.sh --check` → EXIT=0 and `bash tests/file-length-gate.sh --check` → EXIT=0 before both
commits. The first attempt at the 4445/5338 changes grew `src/core/ScriptBindings.cpp` 930 → 940 and
`src/core/ScriptEngine.cpp` 662 → 673 (file-length ratchet regression, EXIT=1); the payload helper was moved to
`src/core/ScriptDawEdit.cpp` and the call sites compacted so both files return to their baselines. No baseline was
re-anchored.

Leftovers: cluster 5201 is a real missing exception guard in `src/core/OnnxRuntimeStemSeparator.cpp` (session
construction at line 176, `session.Run` at line 241) but is untestable while ONNX Runtime is absent from this build
tree; it needs a build configured with `LMMS_HAVE_ONNXRUNTIME`. `ScriptEngineTest::testInstrumentParameterReadWrite()`
fails identically on a clean checkout (`Actual "dummy" vs Expected "TripleOscillator"`,
`tests/src/core/ScriptEngineTest.cpp:531`) — a pre-existing environment issue, verified by reverting all six files
and re-running; not a regression from this batch.