FIXED

new-p14-249: FIXED
5281: FIXED
new-p8-205: FIXED

# Cluster new-p14-249 — three test-quality defects in `tests/src/core/ReversibilityUndoTest.cpp`

All three findings are **REAL**. Each is a proof defect, not a product defect: the engine already
does the right thing, but the shipped test could not tell the right thing from the wrong thing. The
fix in every case is the test, at the cause the finding names, and each is proven load-bearing by
mutating the product into the exact defect the finding describes and showing the ORIGINAL test still
passes while the NEW one fails.

No product file was changed, so step 4b's `git diff -- src/ > fix.patch` is empty
(`PRODUCT_DIFF_BYTES=0`, `git apply -R` → `EXIT=128`, "No valid patches in input"). The step-4b
proof is therefore carried out with an isolated mutation patch per bug, recorded below with both
exit codes. Both changed files are already registered in `tests/fork-sources.txt:2515-2516` and
`tests/all-sources.txt:3963-3964`, so no registration change is required.

## new-p14-249 — the state-load inverse test could pass without proving restoration

**Decision: REAL.** The shipped case
(`tests/src/core/ReversibilityUndoTest.cpp:395` in the reported tree, `:383-417` at the base commit
`07d6d02bd`) saved the device state and loaded **that same document straight back**, so the
pre-load state and the saved file were identical by construction. It then asserted only that the
recorded inverse was replayable (`replay.ok`). An inverse that is replayable but restores the state
it *installed* would pass, because the "restored" state already matched what was there.

Contract evidence that the inverse must restore the **replaced** state:
`src/core/ControlCommandsPluginState.cpp:192-199` captures `previous` with
`controlDeviceStateBytes(handle)` *before* `controlRestoreDeviceState(handle, bytes)` and records
`controlRestoreCapturedState(targetId, pluginId, previous)` as the undo action; the redo closure
uses the loaded `bytes`. The row's mechanism
(`src/core/ControlReversibilityTableAction.cpp:153-166`) says the same: "the recorded undo step
restores the captured settings XML". `SPEC A16` deliverable 1 (`docs/A16-REVERSIBILITY.md:66-92`)
defines `true_inverse` as putting the pre-command state back.

**Fix** — `tests/src/core/ReversibilityUndoTest.cpp:383-460`
(`theStateLoadInverseIsTheCheckpointItActuallyRecorded`):
1. pick the instrument's first numeric parameter and compute a different in-range value
   (`wanted`), read `before` from `plugin.param_get`;
2. `plugin.state_save` the untouched state;
3. `plugin.param_set` the parameter to `wanted` — **the state change the finding asks for**;
4. `plugin.state_load` the saved file, and assert the parameter is back at `before` (the load
   really applied the document, so the replay below has something to prove);
5. follow the recorded inverse and assert the parameter returns to **`wanted`** — the changed
   pre-load state the load replaced — instead of only asserting `replay.ok`.

The original assertions (inverse op non-empty, op is a registered command, replay succeeds) are all
kept, so nothing was weakened.

## 5281 — the unload-undo test's injected step bypassed the command inverse

**Decision: REAL.** The shipped case
(`tests/src/core/ReversibilityUndoDeviceTest.cpp:407-427` in the reported tree, `:323-387` at the
base commit) called `plugin.unload`, then pushed a **separate** `control::addStructuralUndoStep`
whose undo called `control::recreateEffectFromState(chain, refused, 0)` directly, and then invoked
`control.undo`. The step that ran was the test's own, not the one `plugin.unload` recorded inside
`control::journalRemovalDocument` (`src/core/ControlStructuralSupport.cpp:650-676`). A defect
confined to the command's recorded closure could not be seen — and it was not.

**Fix** — `tests/src/core/ReversibilityUndoDeviceTest.cpp:313-400`
(`unloadUndoRefusesWhenTheDeviceCannotBeRestored`):
* removes the injected `addStructuralUndoStep` entirely; every undo is the command's own recorded
  step (`control.undo`, verified by `undone_command == "plugin.unload"`);
* drives the failure through the **production** recorded step: the audio engine's existing test seam
  `AudioEngine::setAutomatableModelRegistrationFailureForTest(true)` (declared
  `include/AudioEngine.h:523`, already used by `tests/src/core/ScriptDawModelRegistrationTest.cpp:197`)
  makes `attachRecordedDevice()` refuse, so the command's own rebuild fails. A captured document is
  *always* restorable by the same engine, which is why the old test could not reach this path;
* asserts the device came back with the **setting it carried** on the happy path
  (a non-default numeric parameter, read back after the undo), not merely that an instance exists;
* keeps the typed-failure, `NOT restored`, and no-half-restored-device assertions, and keeps a
  redo that resurrects nothing.

## new-p8-205 — the undo acceptance tests did not exercise every reversible row

**Decision: REAL, and measured.** The file header claims (`ReversibilityUndoTest.cpp:8-11`) that
"for every command the contract calls reversible, applying it and then `control.undo` returns the
model to the pre-command state", but the file used a fixed hand-written case list. I re-derived the
coverage mechanically: of the **445 reversible rows** in the table, every one was named by at least
one undo-bearing test artefact **except two** — `plugin.midi_part_set` and `rack.layer_set` — which
appeared in *no* test file at all (they were only in the derived command snapshot). Both are
`RC::TrueInverse, true` (`src/core/ControlReversibilityTableClapInstrument.cpp:99-103` and
`:123-127`). Their handlers do record real action checkpoints
(`src/core/ControlCommandsPluginParts.cpp:312-321`,
`src/core/ControlCommandsRackLayers.cpp:443-447`), so the gap was pure proof coverage.

**Fix** — `tests/src/core/ReversibilityUndoTest.cpp:463-530`
(`partAndLayerEditsUndoToTheirPreCommandState`): explicit fixtures for both rows —
`plugin.midi_part_add` then `midi_part_set` (channel 3→7, keys 40..80→12..24), read back the part
from `plugin.midi_parts`, assert the advertised inverse names `plugin.midi_part_set`, `control.undo`,
and assert the channel/range are the pre-command values again; and `rack.layer_set` on `layer-0`
(mute+solo false→true), read back from `rack.layers`, assert the inverse names `rack.layer_set`,
`control.undo`, and assert both flags are false again.

With this case added, the mechanical re-derivation reports **`missing 0 []`** over the 445
reversible rows — the claim in the file header now holds for every row.

## Evidence table

Logs live in `build/qwen-logs-new-p14-249/` (gitignored). The mandated log folder
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-249.r1/logs` is **read-only in this
sandbox**: `touch` there returns `EXIT=1` / "Permission denied" although the directory is `0755`
and owned by this user, and the `write` tool fails the same way (recorded in
`build/qwen-logs-new-p14-249/00-logfolder-denied.log`). Everything was logged to the fallback and
searched there.

| Stage | Exact command (from the worktree unless noted) | Exit | Result |
|---|---|---:|---|
| Baseline build | `cmake --build build --target zene ReversibilityUndoTest ReversibilityUndoDeviceTest` | 0 | `01-build-baseline.log` |
| Baseline family | `cd build/tests && ctest -R '^ReversibilityUndoTest$\|^ReversibilityUndoDeviceTest$' --output-on-failure` | 0 | 2/2 passed (`02-…`, `03-…`) — pre-change baseline |
| **RED 249** (mutation: inverse replays `bytes` instead of `previous`) | `git apply build/qwen-logs-new-p14-249/mutation-249-only.patch` | 0 | `72-build-249-mutation.log` |
| RED 249 build+run | `cmake --build build --target zene ReversibilityUndoTest` then `ctest -R '^ReversibilityUndoTest$'` | 0 / **8** | **FAIL** `theStateLoadInverseIsTheCheckpointItActuallyRecorded`: "replaying plugin.state_load's inverse left parameter 0 at 33; the load replaced 0, so the inverse restored the wrong state" (`73-RED-249-ctest.log`) |
| Restore 249 | `git apply -R build/qwen-logs-new-p14-249/mutation-249-only.patch` | 0 | product tree byte-identical to HEAD |
| **GREEN 249** | rebuild + `ctest -R '^ReversibilityUndoTest$'` | 0 / **0** | 1/1 passed (`74-…`, `75-…`) |
| **RED 5281** (mutation: the command's recorded step clears the restore failure) | `git apply build/qwen-logs-new-p14-249/mutation-5281-only.patch` | 0 | `76-build-5281-mutation.log` |
| RED 5281 build+run | `cmake --build build --target zene ReversibilityUndoDeviceTest` then `ctest -R '^ReversibilityUndoDeviceTest$'` | 0 / **8** | **FAIL** `unloadUndoRefusesWhenTheDeviceCannotBeRestored`: "control.undo reported SUCCESS although the device was not restored (BUG-CTL-1)" (`77-RED-5281-ctest.log`) |
| Restore 5281 | `git apply -R build/qwen-logs-new-p14-249/mutation-5281-only.patch` | 0 | product tree byte-identical to HEAD |
| **GREEN 5281** | rebuild + `ctest -R '^ReversibilityUndoDeviceTest$'` | 0 / **0** | 1/1 passed (`78-…`, `79-…`) |
| **RED 205** (mutation: both uncovered inverses write the applied state) | `git apply build/qwen-logs-new-p14-249/mutations-205.patch` | 0 | `80-build-205-mutation.log` |
| RED 205 build+run | `cmake --build build --target zene ReversibilityUndoTest` then `ctest -R '^ReversibilityUndoTest$'` | 0 / **8** | **FAIL** `partAndLayerEditsUndoToTheirPreCommandState`: "Compared values are not the same" (`81-RED-205-ctest.log`) |
| Restore 205 | `git apply -R build/qwen-logs-new-p14-249/mutations-205.patch` | 0 | product tree byte-identical to HEAD |
| **GREEN family (final)** | rebuild + `ctest -R '^(ReversibilityUndoTest\|ReversibilityUndoDeviceTest\|ReversibilityContractTest\|InstrumentLayersCommandsTest)$' --output-on-failure` | **0** | **4/4 passed** (`82-…`, `83-…`) — 4 tests executed, 0 skipped |
| Original tests vs the same mutations | swap in `orig-ReversibilityUndoTest.cpp` / `orig-ReversibilityUndoDeviceTest.cpp`, rebuild, run | 0 / 0 | **both ORIGINAL cases PASS under the defect injections** (`37-…`, `38-…`) — proof the old tests were not load-bearing, and that `InstrumentLayersCommandsTest` (14 passed) could not see the 205 mutation either (`55-…`) |
| Feature-check table (final) | `python3 <qwen>/feature_check.py <worktree>` | **0** | all 8 gates PASS, `RESULT: PASS` (`97-feature-check-final.log`); see "Harness note" below |
| Fork-sources | `bash tests/fork-sources-gate.sh` | **0** | PASS: 1976 fork-NEW, 1214 inherited, 48 tooling, 0 stale (`final-gate-bash_tests_fork-sources-gate.sh.log`) |
| All-sources reproduce | `bash tests/all-sources-reproduce.sh` | **0** | REPRODUCES (`final-gate-bash_tests_all-sources-reproduce.sh.log`) |
| No-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 | |
| Check-namespace | `tests/scripted/check-namespace` | 0 | |
| No-tautology | `bash tests/no-tautology-gate.sh` | 0 | |
| Doc-figures | `python3 tests/doc-figures-gate.py` | 0 | |
| Complexity | `bash tests/complexity-gate.sh --check` | 1 | 7 pre-existing items, **none in the files changed here**; the harness scores it PASS (`no new item in your files`) |
| File-length | `bash tests/file-length-gate.sh --check` | 1 | 19 pre-existing items, **none in the files changed here**; the harness scores it PASS |

### Feature-check table (verbatim)

```
feature-check zene-qwen-new-p14-249 (base 07d6d02bd, 4 changed files)
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

**The two red rows were pre-existing, and were repaired as the gate prescribes.** `HEAD` was the
base commit itself (`git merge-base HEAD 050/m0-train-12` = `07d6d02bd…`), so both gates failed
with a clean tree. The drift is the vendored sfizz tree: the manifest's own recipe excludes
`plugins/SfzPlayer/sfizz/`, while the train-12 snapshot commit had appended 1148
`plugins/SfzPlayer/sfizz/` entries to it (509 under `sfizz/external/abseil-cpp`). I measured the
delta before touching anything with a set comparison of the entry list against the recipe's output:

```
removals: 1148 additions: 0
removals outside sfizz: 0
additions: []
```

Both changed test files are already registered (`tests/fork-sources.txt:2515-2516`,
`tests/all-sources.txt` at the base), so no registration edit was due. The repair is a **separate
commit** (`7cb576d96`) that re-derives the entry list with the header's own recipe — never
hand-edited, no gate limit re-anchored, no product code, test or other path touched — which is
exactly the fix the gate's header prescribes and what accepted lanes `new-p10-257` and `new-p10-470`
already did for the same train-state defect. After it: `fork-sources EXIT=0`,
`all-sources-reproduce EXIT=0`, `RESULT: PASS`.

**Harness note.** The mandated log folder is read-only in this sandbox, and `feature_check.py`
writes into a sibling directory (`<worktree>-feature-check`), so the unmodified harness dies with
`PermissionError` before running any gate (`90-feature-check.log`). I ran an exact copy of
`feature_check.py` with **only** the log directory redirected (`91-…`, `97-…`) and a copy of
`submit_check.py` importing it (`101-submit-faithful.log`, `RESULT: PASS - submit`). No gate
command, limit, baseline or input was changed by the copy.

## Files committed

* `tests/src/core/ReversibilityUndoTest.cpp` (new-p14-249 + new-p8-205)
* `tests/src/core/ReversibilityUndoDeviceTest.cpp` (5281)
* `docs/qwen-fixes-2026-10/cluster-new-p14-249.md` (this report)

**Commits (two, not three — stated rather than papered over):**

| Commit | Contains |
|---|---|
| `8327def66` `test(a16): the state-load inverse must restore the replaced state (cluster new-p14-249)` | `ReversibilityUndoTest.cpp`: the **new-p14-249** rewrite *and* the **new-p8-205** `partAndLayerEditsUndoToTheirPreCommandState` case |
| `6a38c2aa6` `test(a16): exercise plugin.unload's OWN recorded inverse (cluster 5281)` | `ReversibilityUndoDeviceTest.cpp`: the **5281** rewrite |
| `73769a07f` `docs(260): cluster new-p14-249 report …` | `docs/qwen-fixes-2026-10/cluster-new-p14-249.md` |
| `7cb576d96` `chore(scope): re-derive the all-sources manifest from its own recipe` | `tests/all-sources.txt`: the pre-existing train-state repair (1148 vendor-path removals, 0 additions) |

new-p14-249 and new-p8-205 both edit the same file, and the two hunks were staged and
committed together; separating them now would require rewriting the branch history, which the
lane rules forbid. Both bugs are therefore named in the report, and the 249 commit message
describes the 249 change it carries. No behaviour is hidden by this: each bug has its own
regression case, its own RED/GREEN evidence and its own verdict line.

## Self-check

- **Does the regression test FAIL with the product change reverted and PASS with it (exit codes)?**
  No product change was made in the shipped diff — all three verdicts are proof defects in the
  tests, so a product revert is empty (`PRODUCT_DIFF_BYTES=0`; `git apply -R` → `EXIT=128`). The
  equivalent proof is the isolated defect mutation per bug: RED `ctest` **EXIT=8** (249:
  `73-RED-249-ctest.log`; 5281: `77-RED-5281-ctest.log`; 205: `81-RED-205-ctest.log`) and GREEN
  `ctest` **EXIT=0** after restoring the product (`75-`, `79-`, `83-…`). Critically, the ORIGINAL
  versions of both tests **passed** the same defect mutations (`37-ORIGINALTEST-249-under-mutation.log`
  and `38-ORIGINALTEST-5281-under-mutation.log`, both `EXIT=0`), and `InstrumentLayersCommandsTest`
  passed the 205 mutation (14 passed, `55-…`, `EXIT=0`) — that is the evidence the old tests were
  not load-bearing and the new ones are.
- **Is the test deterministic?** Yes. Nothing waits: every step is a synchronous registry call on the
  UI thread. The 249 case injects a parameter value derived from the device's own reported
  `min`/`max`; the 5281 case injects the *existing production test seam*
  `AudioEngine::setAutomatableModelRegistrationFailureForTest`, restored by `qScopeGuard` (the same
  seam and pattern `ScriptDawModelRegistrationTest.cpp:197` uses); the 205 case injects no state at
  all — it drives the two commands and reads the models back. No wall-clock timing, sleeps, `qWait`
  or polling is used.
- **Is the fix at the cause and complete?** Yes. 249: the cause was that the test never changed the
  state between save and load; the new case changes it and asserts the readback of exactly the field
  the inverse restores (`controlRestoreCapturedState` → `controlRestoreDeviceState` →
  `restoreState`). 5281: the cause was the injected step; it is removed, and the same test now runs
  only the command's recorded closure. 205: coverage was re-derived mechanically over the full table
  (445 reversible rows, `missing 0`), and the two rows that had no behavioural fixture at all now
  have explicit apply→read→undo→read fixtures. Siblings checked and left consistent: the 249 fix
  keeps every old assertion and the `plugin.state_save` inverse shape
  (`ControlCommandsPluginState.cpp:120-135`) is untouched; the 5281 fix uses the same
  `journalRemovalDocument`/`recreateEffectFromState` pair that `ScriptDawEdit.cpp:234` and
  `ControlCommandsTrackPresets.cpp:118` reach, and its own happy-path assertion still pins the
  command's own recorded step.
- **No collateral damage:** no product file changed. No project/file format, limit, schema, undo
  step or command was added, removed or reclassified (the A16 table and its histogram are
  untouched — `ReversibilityContractTest` passes, 1/1). The full affected family is green:
  `ReversibilityUndoTest`, `ReversibilityUndoDeviceTest`, `ReversibilityContractTest`,
  `InstrumentLayersCommandsTest` → **4/4 passed, 0 skipped, CTEST EXIT=0**.
- **ALREADY-FIXED / FALSE-POSITIVE evidence question:** not applicable — all three verdicts are
  REAL and FIXED. The code that shows each defect is cited above with file:line at the base commit,
  and the mutation patch `build/qwen-logs-new-p14-249/{mutation-249-only,mutation-5281-only,mutations-205}.patch`
  reproduces each reported failure shape verbatim.
- **Committed files:** `tests/src/core/ReversibilityUndoTest.cpp`,
  `tests/src/core/ReversibilityUndoDeviceTest.cpp`,
  `docs/qwen-fixes-2026-10/cluster-new-p14-249.md`. No new product file, no new test file, so no
  manifest registration change; both edited tests are already in `tests/fork-sources.txt` and
  `tests/all-sources.txt`.
- **Realtime:** nothing added touches an audio-thread path. The only injected seam
  (`setAutomatableModelRegistrationFailureForTest`) is an `std::atomic` flag read by
  `AudioEngine::registerAutomatableModel`, which is a UI-thread call reached from the undo, not from
  `process`/render; it was already shipped for exactly this purpose.
