# Cluster new-p12-228

Verdict: **ALREADY-FIXED**

Finding (Luna patrol, BUG 7): "Lua wrappers read live engine objects from the script
worker" — getters such as `LuaTrack::name()` (`src/core/ScriptBindings.cpp:671-677`, the
flush at :675) read live LMMS objects on the script worker thread after
`ScriptEngine::flushCommandsForRead()`; the claim is that the flush applies queued writes
but "does not make the subsequent read safe against UI-thread edits", with the suggested
fix "perform reads on the control thread and return snapshots to the worker".

That hazard describes the pre-`1030a5d7c` design ("ScriptEngine: Apply queued commands on
the apply side"), which this branch already replaced. On the current tree the claimed
scenario cannot occur, and the invariant is already guarded by passing tests:

1. **Mutations never run on the worker.** `flushCommandsForRead()` on the worker thread
   during a run does not apply anything itself: it signals `m_applyRequest` and blocks on
   `m_applyDone` while the apply-side thread drains the queue
   (`src/core/ScriptEngine.cpp:727-747`); every applied command records its applying
   thread (`src/core/ScriptEngine.cpp:509-511`), and the documented contract is "engine
   state is only ever mutated by the apply side, never by the worker (spec section 4 /
   section 10)" (`include/ScriptEngine.h:274-277`, pump members :363-368).
2. **A read cannot overlap a mutation.** `runOnWorker()` dispatches the Lua run to the
   worker and then keeps the *calling* thread inside the apply pump
   `while (!finished) { if (m_applyRequest.tryAcquire(1, 2)) { applyCommands(...);
   m_applyDone.release(); } }` for the entire run (`src/core/ScriptEngine.cpp:417-429`).
   That thread mutates the engine only while serving a worker flush request — at which
   moment the worker is parked on `m_applyDone` and is not reading. When the worker's
   `m_applyDone.acquire()` returns (release/acquire ordering), the apply side goes back to
   spinning on `tryAcquire` and touches nothing until the *next* request, which only the
   worker raises from inside a flush. Reads and mutations are mutually exclusive.
3. **UI-thread edits cannot be concurrent with a script at all.** Every product path that
   starts a run starts it on the control (UI) thread, which is then occupied by the pump
   for the whole run — so it processes no events, no timers and no user edits: the menu
   action (`src/gui/MainWindow.cpp:350-354`), the `script.run` / `script.run_string`
   handlers (`src/core/ControlCommandsScript.cpp:202,208`) reached through
   `ControlRegistry::runOnUiThread` blocking-dispatch (`src/core/ControlRegistry.cpp:223-234`,
   :270-275), the CLI `--script` (`src/core/main.cpp:1467-1468`), and the live-code clock,
   whose contract is explicit: "the transport is OBSERVED from the control thread ... on a
   QTimer, and each fire runs through ScriptEngine::runString ... on the worker thread,
   with the engine apply side pumped on the calling (control) thread"
   (`include/ScriptClock.h:13-22`, :148-152). No Lua binding invokes `run`/`runFile`/
   `runString` (grep of `ScriptBindings.cpp`, `ScriptDawBindings.cpp`, `ScriptDawEdit.cpp`,
   `ScriptDawEffects.cpp`, `ScriptCommandBridge.cpp`: none), so the worker-side inline
   fallback is unreachable from a script, and the `m_running` CAS (`ScriptEngine.cpp:375-385`)
   rejects nested runs as Busy regardless.
4. **The suggested fix would not fix a demonstrable bug.** Moving each of the ~20 read
   sites (`ScriptBindings.cpp:553,580,603,620,675,746,762,778,784,889`,
   `ScriptDawBindings.cpp:228,582`, `ScriptDawEdit.cpp:325,394,406`,
   `ScriptDawEffects.cpp:158`, `ScriptCommandBridge.cpp:291`) to round-trip snapshots
   through the control thread changes no observable behaviour (reads are already
   serialized against mutations by the handshake), and would not even address an
   audio-thread writer, because a control-thread read touches the same live model.

## Proof the existing guards catch the bug class (they are not vacuous)

The regression guards for this invariant already live in the tree:
`ScriptEngineTest::testWorkerThreadNotAudioThread` (`tests/src/core/ScriptEngineTest.cpp:542`),
`testApplyRunsOnApplySideThread` (:556-573, asserts `lastApplyThread() == caller` and
`!= workerThread()`), `testModelWritesAreQueuedNotDirect` (:682), and
`tests/src/core/ScriptStabilisationTest.cpp:351`.

Experiment (temporary, reverted; the tree is byte-identical to HEAD now — `git diff
-- src/core/ScriptEngine.cpp` is empty): `flushCommandsForRead()` was edited to the
pre-`1030a5d7c` behaviour (unconditional `applyCommands()` on the calling thread, i.e.
worker-thread mutation during a run). Rebuilt and ran the family — the guard fails exactly
as designed:

- RED (old, buggy flush): `FAIL! : ScriptEngineTest::testApplyRunsOnApplySideThread()
  Compared QObject pointers are not the same — Actual (engine->lastApplyThread()):
  QThread/"zene-lua-script-worker" Expected (QThread::currentThread()):
  QThread/"Qt mainThread"` (test-3-experiment-red.log, ctest exit 8).
- GREEN (current handshake restored): ctest exit 0, all three binaries pass.

No product file is changed by this report, and no new test file was added (the invariant
is owned by the existing guarded tests above), so `tests/fork-sources.txt` and
`tests/upstream-modifications.txt` need no new rows.

## Evidence (exact commands and EXIT codes, logs in /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-228.r1/logs)

| Step | Command | Exit |
|---|---|---|
| Build test binaries | `cmake --build build --target ScriptEngineTest ScriptStabilisationTest ScriptBindingsTest > LOGS/build-1.log 2>&1` | 0 |
| Family run (1) | `cd build/tests && ctest -R '^ScriptEngineTest$\|^ScriptStabilisationTest$\|^ScriptBindingsTest$' --output-on-failure > LOGS/test-1.log 2>&1` | 8 — stale binary: `tripleoscillator.so` not yet built in this tree, so `testInstrumentParameterReadWrite` failed at `ScriptEngineTest.cpp:608` ("plugin wasn't found"); unrelated to this finding |
| Build missing plugin + zene | `cmake --build build --target tripleoscillator zene > LOGS/build-2.log 2>&1` | 0 |
| Family run (2) | same ctest as above > LOGS/test-2.log | 0 |
| Experiment: old flush build | `cmake --build build --target zene ScriptEngineTest > LOGS/build-3-experiment-oldbehavior.log 2>&1` | 0 |
| Experiment RED run | same ctest as above > LOGS/test-3-experiment-red.log | 8 (`testApplyRunsOnApplySideThread` failed: lastApplyThread = "zene-lua-script-worker") |
| Restore: rebuild | `cmake --build build --target zene ScriptEngineTest ScriptStabilisationTest ScriptBindingsTest > LOGS/build-4-restored.log 2>&1` | 0 |
| GREEN run | same ctest as above > LOGS/test-4-green.log | 0 |
| Affected-family counts | `ctest -R '^ScriptEngineTest$\|^ScriptStabilisationTest$\|^ScriptBindingsTest$' -V > LOGS/test-5-family-counts.log 2>&1` | 0 — 3 tests executed: ScriptBindingsTest 26 slots, ScriptEngineTest 27 slots, ScriptStabilisationTest 12 slots (65 total, 0 failed) |
| Complexity gate | `bash tests/complexity-gate.sh --check > LOGS/gate-complexity.log 2>&1` | 1 — **pre-existing**, see below |
| File-length gate | `bash tests/file-length-gate.sh --check > LOGS/gate-filelength.log 2>&1` | 1 — **pre-existing**, see below |

## Gate status on this branch (recorded honestly, NOT re-anchored, NOT caused by this cluster)

Both ratchet gates exit 1 on a **pristine tree**: they were run with `git status` empty and
`git diff` empty, i.e. at exactly HEAD `de5a85a96`, and every flagged function/file belongs
to other lanes, not to this cluster (this report changes no measured source):

- complexity `--check` REGRESSIONs: `parseNotes@src/core/DawProjectReadTracks.cpp (CCN 13)`,
  `dotCentreAlong@tests/src/gui/XyPadPairTest.cpp (CCN 16)`, `lmms::adjust@src/core/ControlCommandsMixerTools.cpp
  (CCN 12)`, `setStrip@src/core/ControlCommandsMixerStrip.cpp (CCN 11)`,
  `rotateProjectRevision@src/core/ProjectRevisions.cpp (CCN 11)`,
  `importedOverrides@src/gui/ControlCommandsShortcuts.cpp (CCN 11)` — verified independently
  with `lizard 1.24.0` (measurements match).
- file-length `--check` REGRESSIONs: `src/core/SmfInterchangeReader.cpp (504)`,
  `src/core/TakeLane.cpp (520)`, plus 8+ test files over 500 code lines
  (`CollabTransportTest`, `DeviceDspTest (752)`, `NotePlaybackTest (624)`, ...).

These trace to the 050/m0-train-5 merges of Oct 13-15 (`729fb9709`, `8ecd33cf8`,
`f91ea87b2`, `055805739`, `7a61ab54d`) landing after the last anchors of the two ratchet
baselines (`tests/complexity-baseline.tsv` last touched by `40104e236`, Oct 9;
`tests/file-length-baseline.tsv` by `ba66cdb87`, Sep 30). Fixing them would mean rewriting
unrelated product/test code or re-anchoring gates — both forbidden by the task rules
("Change only the buggy behaviour", "Never re-anchor a gate"). This commit touches none of
the measured files, so the gates' exit codes are byte-identical before and after it.

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p12-228.md` (this report only)
