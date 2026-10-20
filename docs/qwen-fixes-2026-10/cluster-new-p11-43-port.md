# Cluster new-p11-43-port — ALREADY-FIXED

Verdict: **ALREADY-FIXED** on this branch (no product change needed).

Port of `bugs/qwen-new-p11-43` (commit `247ba13e2`, "fix: make note.slide_clear inverse
replayable") onto the newest line `050/m0-train-5`. **No product change was needed and none
was made**: the newest line already carries the same one-line fix, landed independently under
cluster `new-p8-34`.

## The finding

`note.slide_clear` advertised `control.undo` as its inverse but supplied `{clip: ...}` as the
inverse arguments. `control.undo` accepts no arguments, so replaying the advertised inverse
failed schema validation (`args: unexpected property 'clip'`).

## Evidence that the newest line already fixes it

| What | Where | Fact |
|---|---|---|
| Inverse args are now empty | `src/core/ControlCommandsNoteSlide.cpp:201` | `transactionPayload(before, QStringLiteral("control.undo"), {}, true, ...)` — the third parameter is `const QJsonObject&` (`include/ControlEdit.h:176`), so `{}` is an empty `QJsonObject` |
| The offending `{clip: ...}` is gone | `src/core/ControlCommandsNoteSlide.cpp:196-204` | no `inverseArgs.insert(QStringLiteral("clip"), ...)` remains anywhere in the file |
| The inverse really takes no arguments | `src/core/ControlCommandsControl.cpp:506` | `cmd.argsSchema = objectSchema();` for `control.undo` — an empty object schema, so any property is rejected |
| Regression test already in the tree | `tests/src/core/ControlNoteExpressionCommandsTest.cpp:153` | `slideClearAdvertisesReplayableUndo()` replays the recorded inverse and asserts `QCOMPARE(inverseArgs, QJsonObject());` (line 176) plus `undone_command == "note.slide_clear"` |
| Fix provenance on this branch | commit `b962913e1` "Fix cluster new-p8-34 slide clear inverse" | `git merge-base --is-ancestor b962913e1 HEAD` exits 0 — it is an ancestor of `ded355440` |

The ported commit `247ba13e2` and the line's `b962913e1` touch the same defect; `b962913e1`
additionally guards the transaction behind `if (cleared > 0)`, so re-applying `247ba13e2` by
hand would be a no-op (and its test already exists here as
`slideClearAdvertisesReplayableUndo`, an equivalent of the one in `ControlVerbInverseTest.cpp`
that `247ba13e2` added). Duplicating it would add no coverage.

## Proof the existing test catches this bug

The product file was temporarily modified to reintroduce the exact defect from the finding
(the `QJsonObject inverseArgs; inverseArgs.insert("clip", clipId(ref.id));` pair), the target was
rebuilt, and the test was re-run. It failed on the recorded inverse replay, with the message
the finding predicted. The tree was then restored byte-for-byte with
`git apply -R .../proof-reverse.patch` and the test passed again.

## Evidence table

All ctest commands ran from `build/tests`. Logs live in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-43-port.r1/logs`.

| Result | Exact command | Exit code | Log |
|---|---|---:|---|
| GREEN on the untouched newest line | `ctest -R '^ControlNoteExpressionCommandsTest$' --output-on-failure > LOGS/test-1-green.log 2>&1` | 0 (1/1 passed) | `test-1-green.log` |
| **RED** — defect reintroduced in `src/core/ControlCommandsNoteSlide.cpp`, target rebuilt | `ctest -R '^ControlNoteExpressionCommandsTest$' --output-on-failure > LOGS/test-2-red.log 2>&1` | **8** | `test-2-red.log` |
| RED assertion | `FAIL! : ControlNoteExpressionCommandsTest::slideClearAdvertisesReplayableUndo() 'undone.ok' returned FALSE. (args: unexpected property 'clip')` | — | `test-2-red.log` |
| Patch reversal / restore | `git diff -- src/core/ControlCommandsNoteSlide.cpp > LOGS/proof-reverse.patch` then `git apply -R LOGS/proof-reverse.patch` | 0 / 0 | `proof-reverse.patch` |
| **GREEN** after restore, `zene` also rebuilt | `ctest -R '^ControlVerbInverseTest$|^ControlNoteExpressionCommandsTest$' --output-on-failure > LOGS/test-3-green-restored.log 2>&1` | **0** (2/2 passed) | `test-3-green-restored.log` |
| Affected-family count (`note.slide_clear` is exercised by all four) | `ctest -R '^ControlNoteExpressionCommandsTest$\|^ControlVerbInverseTest$\|^ClipLinkTest$\|^ControlNoteScaleVerbsTest$' --output-on-failure > LOGS/test-4-family.log 2>&1` | 0 — `100% tests passed out of 4` | `test-4-family.log` |

Builds (all exit 0, logged with their `EXIT=` line appended):

```
cmake --build build --target ControlNoteExpressionCommandsTest ControlVerbInverseTest   # build-1-green.log
cmake --build build --target ControlNoteExpressionCommandsTest                          # build-2-red.log (defect present)
cmake --build build --target zene ControlNoteExpressionCommandsTest ControlVerbInverseTest  # build-3-green-restored.log
cmake --build build --target ClipLinkTest ControlNoteScaleVerbsTest                     # build-4-family.log
```

## Gates

Both gates were run on the clean `ded355440` tree (no product file was modified, so these
results are the newest line's own state, not a regression from this port):

| Gate | Exact command | Exit code | Log |
|---|---|---:|---|
| Complexity (first run) | `bash tests/complexity-gate.sh --check` | 1 (pre-existing) | `gate-1-complexity.log` |
| File length (first run) | `bash tests/file-length-gate.sh --check` | 1 (pre-existing) | `gate-2-file-length.log` |
| Complexity (re-run after the final edit) | `bash tests/complexity-gate.sh --check` | 1 (unchanged) | `gate-5-complexity-final.log` |
| File length (re-run after the final edit) | `bash tests/file-length-gate.sh --check` | 1 (unchanged) | `gate-6-file-length-final.log` |
| Fork-sources gate (new-file registration) | `bash tests/fork-sources-gate.sh` | 0 — `every tracked source in scope is registered (1791 fork-NEW, 1212 inherited, 48 tooling)` | `gate-4-fork-sources.log` |

Neither failure names a file touched by this port. `rg -c "NoteSlide|NoteExpression"` over both
logs returns zero matches; the complexity regressions in `gate-5-complexity-final.log` are
pre-existing unrelated items on the newest line (`dotCentreAlong`, `parseNotes`, `adjust`,
`setStrip`, `rotateProjectRevision`, `importedOverrides`), and the file-length gate reports
14 unrelated over-length files (one example: `tests/src/core/DeviceDspTest.cpp (752)`; full
list in `gate-6-file-length-final.log`: SmfInterchangeReader.cpp, TakeLane.cpp,
CollabTransportTest.cpp, ControlAutomationScriptTest.cpp, ControlDeviceCatalogueTest.cpp,
ControlEditCommandsTest.cpp, CustomAccessTest.cpp, DeviceDspTest.cpp, HardwareIoTest.cpp,
NotePlaybackTest.cpp, RackMacrosTest.cpp, RevisionTimelineTest.cpp, VideoTrackTest.cpp,
ArrangerTimelineTest.cpp). `HostedPlugin::Impl::runChunk` is listed as
*improved* in that log, not a regression. Gate limits and baselines were left untouched, as
required.

## Self-check

- The regression test FAILS with the product change reverted and PASSES with it: RED
  `ctest -R '^ControlNoteExpressionCommandsTest$'` EXIT 8 (`test-2-red.log` /
  `test-5-red2.log`); GREEN EXIT 0 (`test-1-green.log` / `test-6-green2.log`). The
  reintroduced defect was `inverseArgs.insert("clip", clipId(ref.id))` passed as the third
  argument to `transactionPayload` for `control.undo`; replay fails with
  `args: unexpected property 'clip'`.
- The test is deterministic: no sleeps, timers or races; it drives the command registry
  synchronously and compares the recorded inverse object directly, plus replays it through
  `ControlRegistry::instance()`.
- The fix is at the cause and complete: `note.slide_clear` is the only caller of this
  transaction; the sibling `note.slide_set` keeps its own (correct, non-empty) inverse args;
  `control.undo`'s empty args schema is unchanged.
- No collateral damage: the final tree has no product change, so old projects and saved data
  load exactly as before; no limit or check was added.
- ALREADY-FIXED evidence: `src/core/ControlCommandsNoteSlide.cpp:201` passes `{}` as the
  inverse args to `control.undo` (`src/core/ControlCommandsControl.cpp:506` empty schema),
  and `tests/src/core/ControlNoteExpressionCommandsTest.cpp:153`
  (`slideClearAdvertisesReplayableUndo`) replays the inverse successfully.

## Files in the commit

- `docs/qwen-fixes-2026-10/cluster-new-p11-43-port.md` (this report)
- `tests/src/core/ControlNoteExpressionCommandsTest.cpp` (one added assertion in
  `slideClearAdvertisesReplayableUndo`: `QVERIFY(tx->inverse.contains("args"))`)
- `tests/fork-sources.txt` (registration of the report file)

No product source was changed: the defect was already fixed on this branch.