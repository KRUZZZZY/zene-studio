# Cluster 5459 — VST3 host batch (5459, 2613, 5460)

Verdict: FIXED

5459: ALREADY-FIXED
2613: ALREADY-FIXED
5460: FIXED

This batch touches `plugins/Vst3Effect/`. The first two findings were fixed earlier on this
branch; the third was still live and is fixed here. The scale/tail contract in `Vst3Host.h` now
states that a MIDI event offset is always in `[0, chunk)`: a boundary event is carried into the
next request at offset 0 instead of being handed to the plug-in at `sampleOffset == numSamples`.

## 5459 — Reprepare can clear buffers while the audio thread uses them: ALREADY-FIXED

Evidence on this branch:

- `plugins/Vst3Effect/Vst3Host.h:84-136` declares `AudioProcessGate`: `tryEnterAudioProcess()`
  admits an audio call only while no reconfiguration is pending, `beginReconfigure()` closes
  admission and waits for every admitted call to leave, and `endReconfigure()` reopens it after
  `prepare()`/`cacheLatency()`.
- `plugins/Vst3Effect/Vst3Effect.cpp:147` calls `m_processGate.beginReconfigure()` before
  `m_plugin.prepare(...)`, and `:161` calls `endReconfigure()` after it. The audio path takes the
  gate at `:205` (`tryEnterAudioProcess()`), passes through while it is closed (`:207-208`), and
  leaves it on every return (`:217`, `:250`). `plugins/Vst3Instrument/Vst3Instrument.cpp` uses the
  same gate for its `reprepare()` and process path.
- The fix landed in commits `51b528f7c` ("quiesce processing during re-prepare (cluster 5459)") and
  `309e3ce83` ("prove wrapper quiescence during re-prepare (cluster 5459)").
- Regression: `tests/src/plugins/Vst3EffectIntegrationTest.cpp:206`
  `testWrapperProcessingIsQuiescedDuringReprepare()` installs the `m_reprepareTestHook` pause
  after admission closes, then drives `chain.processAudioBuffer()` from a second thread while
  `reprepare()` is in progress. The wrapper must pass the dry signal through (0.5), not let the
  plug-in process (0.25). `tests/src/plugins/Vst3HostTest.cpp:528`
  `testReprepareGateQuiescesAllActiveAudioCalls()` proves the gate itself (two admitted calls,
  a waiting reconfigure, refusal during reconfiguration, no allocation).

RED proof (the gate is what the regression catches): the two gate calls in
`Vst3Effect::reprepare()` were temporarily commented out, then the test was rebuilt and run:

```
CCACHE_TEMPDIR=/tmp/ccache-tmp CCACHE_READONLY=1 cmake --build build --target Vst3EffectIntegrationTest > /tmp/5459-logs/build-red-5459-1.log 2>&1   # EXIT=0
cd build/tests && ctest -R '^Vst3EffectIntegrationTest$' --output-on-failure > /tmp/5459-logs/test-red-5459-1.log 2>&1                                  # EXIT=8
```

`testWrapperProcessingIsQuiescedDuringReprepare` failed with
`wrapper processed plugin audio during re-prepare: 0.25` (`Totals: 6 passed, 1 failed`).
Restoring the two calls and rerunning:

```
CCACHE_TEMPDIR=/tmp/ccache-tmp CCACHE_READONLY=1 cmake --build build --target Vst3EffectIntegrationTest > /tmp/5459-logs/build-green-5459-1.log 2>&1   # EXIT=0
cd build/tests && ctest -R '^Vst3EffectIntegrationTest$' --output-on-failure > /tmp/5459-logs/test-green-5459-1.log 2>&1                                  # EXIT=0
```

No product code changed for 5459 in this batch; the branch already carries the fix and its
regression.

## 2613 — MIDI drop counters race: ALREADY-FIXED

Evidence on this branch:

- `plugins/Vst3Effect/Vst3HostInternals.h:91` declares `droppedMidi` as
  `std::atomic<std::uint64_t>{0}`; `:107` declares `droppedOutput` the same way.
- `plugins/Vst3Effect/Vst3Host.cpp:377` increments `droppedMidi` on the producer side
  (`++d.droppedMidi`), `:552` resets it in `prepare()`, and `:383` returns it from
  `droppedMidiEvents()`.
- `plugins/Vst3Effect/Vst3HostReports.cpp:57-59` reads `droppedOutput` with a relaxed `load`;
  `plugins/Vst3Effect/Vst3Host.cpp:838` increments it with a relaxed `fetch_add`.
- The output half was fixed in commit `8e7ac2a15` ("cluster new-p8-266 VST3 output counter
  race"); `droppedMidi` was already atomic when the finding was written.
- Regression added here: `tests/src/plugins/Vst3HostTest.cpp:592`
  `testDroppedMidiCounterUsesAtomicAccess()` checks the declaration, the increment, the reset and
  the getter. The existing `testDroppedOutputCounterUsesAtomicAccess()` (`:571`) covers the
  output counter.

RED proof (the declaration is what the test catches): `droppedMidi` was temporarily changed to a
plain `std::uint64_t` and the test rerun:

```
CCACHE_TEMPDIR=/tmp/ccache-tmp CCACHE_READONLY=1 cmake --build build --target Vst3HostTest > /tmp/5459-logs/build-red-2613-1.log 2>&1   # EXIT=0
cd build/tests && ctest -R '^Vst3HostTest$' --output-on-failure > /tmp/5459-logs/test-red-2613-1.log 2>&1                                  # EXIT=8
```

`testDroppedMidiCounterUsesAtomicAccess` failed on the atomic declaration assertion
(`Totals: 19 passed, 1 failed`). Restoring the atomic and rerunning:

```
CCACHE_TEMPDIR=/tmp/ccache-tmp CCACHE_READONLY=1 cmake --build build --target Vst3HostTest > /tmp/5459-logs/build-green-2613-2.log 2>&1   # EXIT=0
cd build/tests && ctest -R '^Vst3HostTest$' --output-on-failure > /tmp/5459-logs/test-green-2613-2.log 2>&1                                  # EXIT=0
```

## 5460 — Boundary MIDI events are sent past the end of the VST3 block: FIXED

Cause: `sliceEvents()` used `event.sampleOffset = limit` for an event at or beyond the request's
end, then subtracted the chunk offset. On the last chunk that produced
`sampleOffset == chunk length`, outside every VST3 event offset's valid `[0, numSamples)` range.

Fix:

- `plugins/Vst3Effect/Vst3HostInternals.h:101` adds a pre-allocated `EventList deferredEvents`
  (sized once with the other host event lists; no audio-thread allocation).
- `plugins/Vst3Effect/Vst3Host.cpp:779-782` puts a boundary event into `deferredEvents` at offset
  0 instead of into the current chunk at the chunk's end; `:687` and `:712-724` insert those
  carried events at the start of the next request's `inputEvents` before draining new MIDI, so they are
  seen exactly once, at a valid offset 0, and a boundary note-off still releases its note.
- `plugins/Vst3Effect/Vst3Host.cpp:616` clears the carry in `release()` so no stale event survives
  a re-prepare or project reload.
- `plugins/Vst3Effect/Vst3MidiEvent.h:84-91` / `:159-165` add a `maxEvents` cap to
  `drainMidiIntoEventList()`. The caller passes the event list's remaining room
  (`kMaxMidiEventsPerBlock - deferred`), so carried events can never push a newly queued event
  out of the bounded list; older callers keep the default (`-1` = the scratch capacity).
- The contract comment in `plugins/Vst3Effect/Vst3Host.h:266-271` now states the carried-at-0
  rule. No control command, save format, or undo path changed.

Regression: `tests/src/plugins/Vst3InstrumentTest.cpp:423`
`testBoundaryMidiEventsAreCarriedIntoTheNextBlock()` uses the host's test-only chunk witness
(`plugins/Vst3Effect/Vst3Host.cpp:862-869`, declared under `LMMS_TESTING` in `Vst3Host.h`) to
assert the event list the plug-in actually received: a note-on at `frameOffset == 512` is absent
from the 512-frame request (count 0) and appears in the next request with `sampleOffset == 0`; the
same for a note-off at the boundary, which leaves the note held for the whole request that ends
and releases from the start of the next one.

RED (witness added, carry fix not yet present):

```
CCACHE_TEMPDIR=/tmp/ccache-tmp CCACHE_READONLY=1 cmake --build build --target Vst3InstrumentTest > /tmp/5459-logs/build-red-5460-1.log 2>&1   # EXIT=0
cd build/tests && ctest -R '^Vst3InstrumentTest$' --output-on-failure > /tmp/5459-logs/test-red-5460-1.log 2>&1                                  # EXIT=8
```

`testBoundaryMidiEventsAreCarriedIntoTheNextBlock` failed:
`Actual (m_plugin.lastChunkEventCount()): 1`, `Expected (0): 0`.

GREEN after the fix:

```
CCACHE_TEMPDIR=/tmp/ccache-tmp CCACHE_READONLY=1 cmake --build build --target Vst3InstrumentTest > /tmp/5459-logs/build-green-5460-1.log 2>&1   # EXIT=0
cd build/tests && ctest -R '^Vst3InstrumentTest$' --output-on-failure > /tmp/5459-logs/test-green-5460-1.log 2>&1                                  # EXIT=0
```

### 4b — reverse/restore proof for 5460

Only the carry hunks (`Vst3Host.cpp`, `Vst3HostInternals.h`) were reverted temporarily; the test
seam stayed. `/tmp/5459-logs/fix-5460.patch` is the resulting `git diff -R` (no-fix -> fix).

```
git apply /tmp/5459-logs/fix-5460.patch      # EXIT=0
git apply -R /tmp/5459-logs/fix-5460.patch   # EXIT=0
CCACHE_TEMPDIR=/tmp/ccache-tmp CCACHE_READONLY=1 cmake --build build --target Vst3InstrumentTest > /tmp/5459-logs/proof-red-5460-build-1.log 2>&1   # EXIT=0
cd build/tests && ctest -R '^Vst3InstrumentTest$' --output-on-failure > /tmp/5459-logs/proof-red-5460-test-1.log 2>&1                                  # EXIT=8
git apply /tmp/5459-logs/fix-5460.patch      # EXIT=0
CCACHE_TEMPDIR=/tmp/ccache-tmp CCACHE_READONLY=1 cmake --build build --target Vst3InstrumentTest > /tmp/5459-logs/proof-green-5460-build-1.log 2>&1   # EXIT=0
cd build/tests && ctest -R '^Vst3InstrumentTest$' --output-on-failure > /tmp/5459-logs/proof-green-5460-test-1.log 2>&1                                  # EXIT=0
```

The reversed state failed again with `lastChunkEventCount` 1 vs 0; the restored state passed.
`git status --short` was empty after restoring.

## Affected-family run

```
CCACHE_TEMPDIR=/tmp/ccache-tmp CCACHE_READONLY=1 cmake --build build --target zene Vst3BusMapTest Vst3HostTest Vst3ChunkProbeTest Vst3EffectIntegrationTest Vst3InstrumentFixtureProbe Vst3InstrumentTest Vst3InstrumentIntegrationTest > /tmp/5459-logs/build-family-final2.log 2>&1   # EXIT=0
cd build/tests && ctest -R 'Vst3' --output-on-failure > /tmp/5459-logs/test-family-final2.log 2>&1                                                                                                  # EXIT=0
```

Seven CTest targets ran, seven passed (`Vst3BusMapTest`, `Vst3HostTest`, `Vst3ChunkProbeTest`,
`Vst3EffectIntegrationTest`, `Vst3InstrumentFixtureProbe`, `Vst3InstrumentTest`,
`Vst3InstrumentIntegrationTest`). The pre-change baseline for the three directly affected targets
was also 3/3 green (`/tmp/5459-logs/test-baseline-1.log`, EXIT=0).

## Gates and environment note

The designated log directory
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5459.r1/logs` is not writable by this
agent session (owner write bits are set, but every create returns `EACCES`; the worktree is the
only writable root). Logs therefore live in `/tmp/5459-logs`. `feature_check.py` writes a
sibling directory of the worktree, which is also outside that root, so the gate was run against a
synchronized copy of the same commit (`/tmp/zene-qwen-5459-check`, source-only copy; nested
submodule gitfiles removed) with the same branch/HEAD:

```
GIT_OPTIONAL_LOCKS=0 python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /tmp/zene-qwen-5459-check > /tmp/5459-logs/gate-refactor.log 2>&1   # EXIT=0
```

All eight rows passed: fork-sources, all-sources-reproduce, no-upstream-regression,
check-namespace, no-tautology, doc-figures, complexity, file-length. The direct worktree run
fails before any gate with `PermissionError` creating
`.../zene-studio-program/zene-qwen-5459-feature-check` (environmental, not a gate failure).

The completion check on the synchronized copy is PASS:

```
GIT_OPTIONAL_LOCKS=0 python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /tmp/zene-qwen-5459-check 5459 > /tmp/5459-logs/submit-check-final2.log 2>&1   # EXIT=0
```

The same check on the real worktree reports only the gate row as FAIL
(`/tmp/5459-logs/submit-check-real.log`, EXIT=1) for the same log-directory permission reason;
every other row (commits, clean tree, report, tests) passes, and both paths use the same git
HEAD and the same commit set.

## Files committed

- `15756b30d` — `test(vst3): prove the MIDI drop counter is atomic (cluster 2613)`:
  `tests/src/plugins/Vst3HostTest.cpp`.
- `9523cf9d8` — `fix(vst3): carry boundary MIDI events into the next block (cluster 5460)`:
  `plugins/Vst3Effect/Vst3Host.cpp`, `Vst3Host.h`, `Vst3HostInternals.h`,
  `Vst3MidiEvent.cpp`, `Vst3MidiEvent.h`, `tests/CMakeLists.txt`,
  `tests/src/plugins/Vst3InstrumentTest.cpp`.
- `98d56c9f1` — `refactor(vst3): keep the boundary carry out of process()'s complexity
  (cluster 5460)`: `Vst3Host.cpp`, `Vst3HostInternals.h` move the carry insertion into
  `Impl::prependDeferredEvents()`, so `process()` stays within the complexity target.
- This report, committed with the batch.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.** 5460:
  reverse/restore proof above, RED `EXIT=8`, GREEN `EXIT=0`. 2613: plain-counter proof above,
  RED `EXIT=8`, GREEN `EXIT=0`. 5459: gate-removal proof above, RED `EXIT=8`, GREEN `EXIT=0`.
- **The tests are deterministic.** 5460 uses the host's in-process chunk witness and fixed
  frame offsets (512), no clock or sleeps. 2613 checks the declaration and access forms in the
  source, because a plain-integer race cannot be scheduled deterministically. 5459 uses the
  `m_reprepareTestHook` pause point and an atomic handshake, not wall-clock timing.
- **The fix is at the cause and complete.** 5460 changed the one mapping that produced the
  invalid offset (`sliceEvents`), added the carry at the one place that rebuilds the event list
  (`process`), cleared it in `release`, and capped the drain to the event list's remaining room.
  The other callers of `drainMidiIntoEventList` keep the old default capacity. 2613's two
  counters and their reset/getter paths were all checked. 5459's effect and instrument wrappers
  both use the same gate.
- **No collateral damage.** No save format, parameter id, bus layout, control command or undo
  path changed. The event-list size, queue capacity and block-size limits are unchanged; the
  `maxEvents` cap only reserves room for carried events. The full VST3 family (7/7) and the zene
  target build pass.
- **ALREADY-FIXED evidence.** 5459: `Vst3Host.h:84-136`, `Vst3Effect.cpp:147/161/205/217/250`
  and `Vst3EffectIntegrationTest.cpp:206`. 2613: `Vst3HostInternals.h:91/107`,
  `Vst3Host.cpp:377/383/552/838` and `Vst3HostReports.cpp:57-59`.
