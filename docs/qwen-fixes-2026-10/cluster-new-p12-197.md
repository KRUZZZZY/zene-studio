# cluster new-p12-197 — BUG 2: Retired message ID 0 is treated as "none"

**Verdict: FALSE-POSITIVE**

## Finding

> `m_pendingRetiredId` uses 0 as its empty sentinel, but message IDs can start at 0. A retired
> message with ID 0 is indistinguishable from no pending message.

## Why it cannot happen

`m_pendingRetiredId` (`include/RemotePlugin.h:298`, declared as the deferred
`//! Retired message id a client sent during a period (0 = none)`, `include/RemotePlugin.h:297`) has
exactly **one writer** in the whole tree (repository-wide `rg -n m_pendingRetiredId` outside
`build/` returns only `include/RemotePlugin.h:298` and `src/core/RemotePlugin.cpp:700,702,705,1016`):

- `src/core/RemotePlugin.cpp:1016` — `m_pendingRetiredId = _m.id;`

That write sits inside a `switch ( _m.id )` (`src/core/RemotePlugin.cpp:977`) whose relevant labels
are `case IdChangeInputCount:` / `case IdChangeOutputCount:` (`src/core/RemotePlugin.cpp:998-999`).
Control can only reach line 1016 when `_m.id` equals one of those two labels, so the stored value is
always one of two compile-time enum constants, never a client-chosen or dynamically allocated id.

Both constants are non-zero, by construction of `enum RemoteMessageIDs`
(`include/RemotePluginBase.h:374`): counting the enumerators from
`IdUndefined = 0, IdHostInfoGotten = 1, IdInitDone = 2, IdQuit = 3, IdSyncKey = 4,
IdSampleRateInformation = 5, IdBufferSizeInformation = 6, IdInformationUpdated = 7, IdMidiEvent = 8,
IdStartProcessing = 9, IdProcessingDone = 10, IdChangeSharedMemoryKey = 11` gives
`IdChangeInputCount = 12` and `IdChangeOutputCount = 13`. The in-tree mirror of the wire protocol in
the test fake spells the same numbers out: `IdChangeInputCount = 12, //!< retired (#589)` and
`IdChangeOutputCount = 13, //!< retired (#589)` (`tests/src/plugins/FakeRemotePluginClient.cpp:74-75`).

So the sentinel `0` at `src/core/RemotePlugin.cpp:700` (`if (m_pendingRetiredId != 0)`) can never
collide with a stored value; the finding's premise — that "message IDs can start at 0" — does not
hold for this field, which only ever holds ids 12 and 13. The finding's own suggested trigger ("the
client sends a retired-message notification for ID 0 during processing") is not reachable: only those
two ids are recognised as retired, and every other id (including 0/`IdUndefined`) is handled by a
different branch — `IdUndefined` returns early at `src/core/RemotePlugin.cpp:979-983` and never
touches `m_pendingRetiredId`.

The existing test `removedCountMessagesAreRefused()`
(`tests/src/core/RemotePluginAudioPortsTest.cpp:461`) already pins the observable contract of this
path — a client sending `IdChangeInputCount` leaves the shared block untouched and makes `process()`
render silence — and it passes on this branch.

No production change and no regression test are appropriate for an unreachable state, so the
RED/GREEN and inverse-patch proof runs are N/A.

## Evidence table

| Check | Exact command (run from) | Result |
|---|---|---|
| RED regression | N/A — the reported state is unreachable (only `IdChangeInputCount`/`IdChangeOutputCount` = 12/13 can be stored) | N/A |
| GREEN regression | N/A — no fix and no regression test added | N/A |
| Inverse-patch proof | N/A — no product file was modified, so there is no fix patch to reverse | N/A |
| Affected-family build | Worktree root: `cmake --build build --target RemotePluginAudioPortsTest RemotePluginClientE2ETest FakeRemotePluginClient > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-197.r1/logs/build-1.log 2>&1; echo EXIT=$?` | `EXIT=0` |
| Affected-family test | `build/tests`: `ctest -R '^RemotePluginAudioPortsTest$\|^RemotePluginClientE2ETest$\|^FakeRemotePluginClient$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-197.r1/logs/test-1.log 2>&1; echo EXIT=$?` | `EXIT=0`; 2/2 registered CTest targets passed (`RemotePluginAudioPortsTest`, `RemotePluginClientE2ETest`; `FakeRemotePluginClient` is not a CTest target) |
| Complexity gate | Worktree root: `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-197.r1/logs/gate-1.log 2>&1; echo EXIT=$?` | `EXIT=1` — **pre-existing at HEAD, unrelated to this finding**; the tree was clean and no product file was touched. Reported regressions are all in unrelated files (`tests/src/gui/XyPadPairTest.cpp`, `src/core/DawProjectReadTracks.cpp`, `src/core/ControlCommandsMixerTools.cpp`, `src/core/ControlCommandsMixerStrip.cpp`, `src/core/ProjectRevisions.cpp`, `src/core/ControlCommandsShortcuts.cpp`); no `RemotePlugin*` entry appears in the gate log |
| File-length gate | Worktree root: `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-197.r1/logs/gate-2.log 2>&1; echo EXIT=$?` | `EXIT=1` — **pre-existing at HEAD, unrelated to this finding**; over-length files are `src/core/SmfInterchangeReader.cpp`, `src/core/TakeLane.cpp` and several `tests/src/**` files; neither `include/RemotePlugin.h` nor `src/core/RemotePlugin.cpp` appears in the gate log |

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p12-197.md` (this report only)

## Note for future patrols

The `0 = none` sentinel is sound here only because the field is written under two non-zero `case`
labels of a closed enum. A future retired-id case with the value 0 (or a dynamic id) would need the
pending flag to be tracked separately, e.g. a `bool m_pendingRetired` next to the id.