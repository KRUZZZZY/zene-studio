# `session.*` per-id proof table — task #642 (030/session-api-proof)

**Deliverable:** one MEASURED row per id for the group's whole surface, the one-line limitations statement
for the absent grid UI, and the naming of any id that is only a refusal stub.

**Verdict up front: no id in the `session.*` group is a refusal stub.** All eleven are registered, all eleven
answer `ok:true` for a legal call, all eleven change what they claim to change, and all eleven also refuse a
typed `invalid_args` the illegal one. Nothing below is read off the source alone: every row's observed
behaviour is a line in the committed socket transcript.

| | |
|---|---|
| Branch | `030/session-api-proof`, cut from `release/0.3.0` @ `a74749d15` |
| Binary under test | `.proof-bin/zene-a74749d15` (a copy of the wave-4 train's linked build at `zene-030/build/zene`) |
| Binary sha256 | `2eac83506be5bc6c6a691174caa19ec284ed5905d4a5af239279a0b42364c946` |
| Build switch | `WANT_SESSION_VIEW=ON`, `LMMS_HAVE_SESSION_VIEW` defined (`zene-030/build/lmmsconfig.h:42`) |
| Committed transcript | `docs/reports/SESSION-API-PROOF-transcript-2026-09-15.txt` (135 158 bytes) |
| Registered ctest | `ControlSessionApiProof` — `tests/control-session-api-proof.py`; measured as **Test #144 of 183** after a configure, and **Passed** when ctest was run (3/3 for the `^ControlSession` set) |

## The measuring command

```bash
cd zene-030/wsesp/tests
export LD_LIBRARY_PATH=$PWD/../../third_party/wasmtime/lib
export PYTHONPATH=$PWD
QT_QPA_PLATFORM=offscreen python3 control-session-api-proof.py ../.proof-bin/zene-a74749d15 \
    --out ../docs/reports/SESSION-API-PROOF-transcript-2026-09-15.txt > /tmp/proof.log 2>&1; echo EXIT=$?
# EXIT=0     (PASS: session.* per-id proof table (every one of the eleven ids measured))
```

The script starts the real binary with `--control-socket` through the project's shared harness
(`tests/control_socket_harness.py`), so it adds no second launch path. It prints the raw request/response
transcript, and writes the same text to `--out`. The full transcript is committed as the proof artefact;
this file is the table it produces, with the registered reference each row was checked against.

## How the proof is registered, and the run that proves it runs

Registration was **measured by configuring the tree**, not asserted from the diff:

```bash
cmake -S . -B .proof-ctest -DCMAKE_BUILD_TYPE=Release -DWANT_QT6=ON > /tmp/cmake-configure2.log 2>&1; echo CMAKE_EXIT=$?
# CMAKE_EXIT=0        (configure only: no compile, no 25 GB build tree)

ctest --test-dir .proof-ctest/tests -N | tail -3
# Total Tests: 183
ctest --test-dir .proof-ctest/tests -N | grep -A1 ControlSession
#   Test #142: ControlSessionLaunch
#   Test #143: ControlSessionLifecycleTranscript
#   Test #144: ControlSessionApiProof
#   Test #145: ControlExportSettings
grep -n LMMS_HAVE_SESSION_VIEW .proof-ctest/lmmsconfig.h
# 42:#define LMMS_HAVE_SESSION_VIEW
```

and the generated entry, from `.proof-ctest/tests/CTestTestfile.cmake`:

```
add_test(ControlSessionApiProof "…/python3" "…/tests/control-session-api-proof.py" "…/.proof-ctest/zene")
set_tests_properties(ControlSessionApiProof PROPERTIES  ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    SKIP_RETURN_CODE "77" TIMEOUT "300" _BACKTRACE_TRIPLES "…/tests/CMakeLists.txt;1791;add_test;…")
```

The ctest was then **run** in that tree, against the same copied binary placed at the `$<TARGET_FILE:zene>`
path the generator resolves:

```bash
ln -sf "$PWD/.proof-bin/zene-a74749d15" .proof-ctest/zene
export LD_LIBRARY_PATH=$PWD/../third_party/wasmtime/lib      # zene-030/third_party/wasmtime/lib
ctest --test-dir .proof-ctest/tests -R '^ControlSession' > /tmp/ctest-session3.log 2>&1; echo EXIT=$?
# EXIT=0
#     Start 142: ControlSessionLaunch
# 1/3 Test #142: ControlSessionLaunch ................   Passed    5.37 sec
#     Start 143: ControlSessionLifecycleTranscript
# 2/3 Test #143: ControlSessionLifecycleTranscript ...   Passed    6.02 sec
#     Start 144: ControlSessionApiProof
# 3/3 Test #144: ControlSessionApiProof ..............   Passed    4.79 sec
# 100% tests passed, 0 tests failed out of 3
```

`ctest` was run from `<build>/tests`, never from the top-level build directory, and `-N` reported **183**
tests rather than 0. The two sibling ctests passing in the same run is the regression evidence that this
branch changed nothing about the group's existing behaviour: it added one entry and one script.

## Per-id table

`registered reference` = the handler that implements the id, and its SPEC A16 row, each verified by line
against this tree. `A16 (live)` = the class the RUNNING registry reported back through `control.transactions`
after the row's call — a measurement, not the source's word.

| id | registered reference (handler + A16 row) | measuring command | measured result | A16 (live) | verdict |
|---|---|---|---|---|---|
| `session.get_state` | `registerSessionGetState` — `src/core/ControlCommandsSession.cpp:187-225` (id at 190); A16 `ControlReversibilityTablePassive.cpp:340` `R("session.get_state", RC::NotMutating, false, "reads the model and the launch engine's atomics", "no write", "")` | `session.get_state {}` | `grid=0x0 clips=0 quantisation=bar scenes=0 launch.completed=0` on a fresh instance; all five declared fields present (`grid`, `quantisation`, `slots`, `scenes`, `launch`) | `not_mutating` (no record) | **MEASURED** |
| `session.set_grid` | `registerSessionSetGrid` — `ControlCommandsSession.cpp:228-260` (id at 231); A16 `ControlReversibilityTableAction.cpp:256` `R("session.set_grid", RC::TrueInverse, true, …)` | `session.set_grid {"tracks":5,"scenes":2}` | `grid 0x0 -> 5x2`; read-back `grid={"clips":0,"scenes":2,"tracks":5}` | `true_inverse`, `reversible=true` | **MEASURED** |
| `session.set_quantisation` | `registerSessionSetQuantisation` — `ControlCommandsSession.cpp:263-295` (id at 266); A16 `ControlReversibilityTableAction.cpp:258` `R("session.set_quantisation", RC::TrueInverse, true, "one field of the <session> block (launchquantisation)", …)` | `session.set_quantisation {"quantisation":"none"}` then `{"quantisation":"bar"}` | `quantisation 'none'` read back, then `'bar'` | `true_inverse`, `reversible=true` | **MEASURED** |
| `session.set_scene` | `registerSessionSetScene` — `ControlCommandsSession.cpp:298-358` (id at 301); A16 `ControlReversibilityTableAction.cpp:260` `R("session.set_scene", RC::TrueInverse, true, "a scene's name and its tempo / time-signature overrides live in the <session> block", …)` | `session.set_scene {"scene":0,"name":"api-proof-scene","tempo":128.5}` | read-back scene 0 `name='api-proof-scene' tempo=128.5 tempo_enabled=True` | `true_inverse`, `reversible=true` | **MEASURED** |
| `session.set_slot` | `registerSessionSetSlot` — `ControlCommandsSession.cpp:361-412` (id at 364); A16 `ControlReversibilityTableAction.cpp:262` `R("session.set_slot", RC::TrueInverse, true, "a clip slot's reference and launch settings are cells of the <session> block and have no journalled object behind them", …)` | 6 × `session.set_slot {"track":N,"scene":M,"type":"midi","pattern":P,"name":…,"mode":"trigger","quantisation":"global"}` | 6 cells built and read back: `[(0,0),(0,1),(1,0),(1,1),(2,0),(4,0)]`, each `type=midi` with its `pattern` and `name` | `true_inverse`, `reversible=true` | **MEASURED** |
| `session.clear` | `registerSessionClear` — `ControlCommandsSession.cpp:452-480` (id at 455); A16 `ControlReversibilityTableAction.cpp:266` `R("session.clear", RC::TrueInverse, true, "it empties every cell, every scene override and the global quantisation at once, on a model the engine does not journal", …)` | `session.clear {}` | `grid 5x2/6 clips -> 0x0/0`; one `control.undo` restored 6 clips (`undone_command=session.clear`); a cleared grid then `project.save` writes **no `<session>` block** (10 703 bytes saved, file read back client-side) | `true_inverse`, `reversible=true` | **MEASURED** |
| `session.clear_slot` | `registerSessionClearSlot` — `ControlCommandsSession.cpp:415-449` (id at 418); A16 `ControlReversibilityTableAction.cpp:264` `R("session.clear_slot", RC::TrueInverse, true, "clearing a cell destroys a reference id or an audio source path that no live object holds a copy of", …)` | `session.clear_slot {"track":1,"scene":1}` | `clips 6 -> 5`; reply slot `type=empty track=1 scene=1`; one `control.undo` restored it (`undone_command=session.clear_slot`, clips back to 6) | `true_inverse`, `reversible=true` | **MEASURED** |
| `session.launch_slot` | `registerLaunchSlot` — `src/core/ControlCommandsSessionLaunch.cpp:98-153` (id at 101); A16 `ControlReversibilityTablePassive.cpp:332` `R("session.launch_slot", RC::NotMutating, false, "the request queues into the scheduler's lock-free queue; the slot's launch state lives on the audio thread and is not project state", "nothing to reverse: session.stop_slot is the operation a client calls, and it is available directly", "")` | `session.launch_slot {"track":0,"scene":0}` | `scheduled_tick=192` **==** the engine's own `next_bar` 192; the start LANDED: `start_line=192`, `start_line_starts=1`, `completed_launches 0->1` after 0.9 s. The cell's `quantisation:"global"` resolved to the session default `bar` — the resolution is measured, not assumed | `not_mutating` (no record) | **MEASURED** |
| `session.launch_scene` | `registerLaunchScene` — `ControlCommandsSessionLaunch.cpp:155-219` (id at 158); A16 `ControlReversibilityTablePassive.cpp:334` `R("session.launch_scene", RC::NotMutating, false, "the same queued requests, one per non-empty cell of the row; nothing in the <session> block is written", "nothing to reverse: session.stop_all drops every launched slot and session.stop_slot stops one", "")` | `session.launch_scene {"scene":0}` | `clips=3`, `in_sync=True`, all three on `sync_tick=384`; the starts LANDED after 1.7 s: `start_line=384`, `start_line_starts=3`, `completed 1->4`, `dropped_commands=0` | `not_mutating` (no record) | **MEASURED** |
| `session.stop_slot` | `registerStopSlot` — `ControlCommandsSessionLaunch.cpp:221-257` (id at 224); A16 `ControlReversibilityTablePassive.cpp:336` `R("session.stop_slot", RC::NotMutating, false, "a stop request is the same engine-state queue; the scheduled stop fires on the audio thread", "nothing to reverse: a stopped slot is relaunched with session.launch_slot", "")` | `session.stop_slot {"track":0,"scene":0}` | `stop_requested=true`; the audio thread **consumed** it: `processed_commands 4 -> 5` after 0.1 s, `dropped_commands=0` | `not_mutating` (no record) | **MEASURED** |
| `session.stop_all` | `registerStopAll` — `ControlCommandsSessionLaunch.cpp:259-283` (id at 262); A16 `ControlReversibilityTablePassive.cpp:338` `R("session.stop_all", RC::NotMutating, false, "one atomic reset request; it edits no model and drops only the audio thread's transient slot table", "nothing to reverse: the slots are relaunched from the model, which the reset did not touch", "")` | `session.stop_all {}` | `reset_requested=true`; the reset was applied: `completed_launches 4 -> 0` after 0.1 s, and the grid model was **left alone** (`clips=6` before and after) | `not_mutating` (no record) | **MEASURED** |

**Registration** (the whole group, both halves, one guarded block):
`src/core/ControlRegistryRegistrations.cpp:119-128` — `#ifdef LMMS_HAVE_SESSION_VIEW` → `registerSessionCommands(registry)` (line 125) and `registerSessionLaunchCommands(registry)` (line 126). The A16 rows are guarded by the same `#ifdef` in both table files (`ControlReversibilityTableAction.cpp:252-268`, `ControlReversibilityTablePassive.cpp:327-342`), so the registry and the classification table stay consistent in both directions.

**A16 rows: measured, not grepped.** For the six mutating ids the step above also read the A16 record back
out of the running instance (`control.transactions`) and confirmed `class=true_inverse reversible=true`, with
`before` carrying the pre-edit grid. For the five launch/stop/read ids it confirmed **no** record is
recorded, which is exactly what their `RC::NotMutating, false` rows claim. The script fails the run if
either direction is violated.

## Refusals — the other half of "not a stub"

A refusal stub refuses the illegal call and nothing else. So the property a stub cannot have is refusing the
illegal call **and** performing the legal one, and both halves are measured. Seventeen refusals, all
`kind=invalid_args`, all with the id's own message:

| id | command | reply |
|---|---|---|
| `session.set_grid` | `{"tracks":9000,"scenes":2}` | `tracks: 9000 is above the maximum 256` |
| `session.set_quantisation` | `{"quantisation":"whenever"}` | refused (not in the enum) |
| `session.set_scene` | `{"scene":900}` | `scene: 900 is above the maximum 511` |
| `session.set_scene` | `{"scene":2}` | `scene 2 is outside the grid's 2 scenes; session.set_grid resizes it` |
| `session.set_slot` | `{"track":900,…}` | `track: 900 is above the maximum 255` |
| `session.set_slot` | `{"track":5,"scene":0,"type":"empty"}` | `slot (track 5, scene 0) is outside the grid (5 tracks x 2 scenes); session.set_grid resizes it` |
| `session.set_slot` | `{"track":0,"scene":0,"type":"midi"}` | `type 'midi' needs 'pattern': the PatternStore id the slot references` |
| `session.clear_slot` | `{"track":900,…}` / `{"track":5,…}` | `…above the maximum 255` / `…is outside the grid (5 tracks x 2 scenes)` |
| `session.launch_slot` | `{"track":900,…}` | `track: 900 is above the maximum 255` |
| `session.launch_slot` | `{"track":0,"scene":0}` (empty cell) | `slot (track 0, scene 0) is empty: there is no clip to launch; session.set_slot defines one` |
| `session.launch_slot` | `{"track":4,"scene":1}` (no song track) | `column 4 has no song track: the engine takes a track over by its position in the song's track list, and this song has 4` |
| `session.launch_scene` | `{"scene":900}` / `{"scene":2}` | `scene: 900 is above the maximum 511` / `scene 2 is outside the grid's 2 scenes` |
| `session.clear_slot` / `session.stop_slot` | `{"track":5,"scene":0}` | `slot (track 5, scene 0) is outside the grid (5 tracks x 2 scenes)` |
| `session.clear` | `{"tracks":1}` | refused — `session.clear` declares **no** arguments |

**Two layers, measured and named separately.** A value outside the declared schema bound is refused by the
REGISTRY before any handler runs (`… is above the maximum N`); a value inside the schema but outside the grid
is refused by the HANDLER's own `gridContains`/bounds check (`slot (… ) is outside the grid`). The script
drives both and keeps them apart, because a stub detector that only ever sent `900` would be measuring the
schema and calling it the handler.

## The limitations line for the absent grid UI

`docs/KNOWN-LIMITATIONS.md:82-89`, carrying the sentence and its bounds:

> **The Session View is in the 0.3.0-alpha builds, and there is still no way to operate it from the
> interface.** … there is **no clip launcher, no scene launcher and no clip grid** — no UI at all — and a
> launched session slot **does not render audio**, because this tree has no session-clip playback path
> (`src/core/SessionClip.cpp` is serialisation only). An agent can build a grid, launch a clip or a whole
> scene, and read the launch back through `--control-socket` (`session.get_state`, `session.set_slot`,
> `session.launch_scene`, ...); a user cannot see or hear any of it.

**It says what this run measured.** The run drove the grid, a clip launch and a whole scene launch through
`--control-socket` and read each back, and it never asserts audio — the "start" a launch produces is the
engine's recorded start event (`launch.start_line`, `start_line_starts`, `start_observed`), which is what
the transcript's `session.launch_slot` and `session.launch_scene` rows show. "No UI at all" was re-measured
here rather than accepted: `grep -rniIl 'sessionmodel\|sessionScheduler\|ClipSlot\|launchscene' src/gui/`
returns **nothing**, so no file under `src/gui/` reaches the session model.

**And one gap this audit found, named rather than papered over:** the same line was **absent** from
`docs/RELEASE-NOTES-v0.3.0-alpha.md`, whose only mention of the group was the "Not in this draft yet" note
deferring it to wave W12. The scope contract's criterion 4 requires the line in *both* documents, so a
minimal additive section was added there ("Session View: eleven ids, measured one at a time",
`docs/RELEASE-NOTES-v0.3.0-alpha.md:1998-2012`). That section is the only release-notes edit in this branch
and it is additive: nothing existing was reworded, moved or removed.

## What this audit does NOT prove

- **Per-slot phase is not readable over the socket.** `session.get_state` reports the model's cells and the
  launch engine's aggregate counters, not `SlotLaunchState`, so nothing here asserts a stopped slot returned
  to `Idle`. That half is `tests/src/core/SessionSchedulerTest.cpp`, in-process.
- **A launched slot renders no audio** in this tree (see the limitations line). "Started" in this table means
  the engine recorded the start on the scheduled line — stated as that, and never as audio.
- **The A16 record's `inverse.op` and its `class` disagree in wording**, and this is a measurement, not an
  opinion: every mutating `session.*` record reads
  `"inverse":{"args":{},"op":"UNIMPLEMENTED: replay the captured <session> block through session.set_grid / session.set_slot / session.set_scene"}`
  while `"class":"true_inverse","reversible":true`. The undo itself is real and was exercised (one
  `control.undo` restored a cleared cell and a cleared grid, both measured), and the A16 row's declared
  mechanism — a `ProjectJournal` action checkpoint — is what actually runs. `recordSessionEdit`
  (`ControlCommandsSession.cpp:103-108`) sets that text deliberately, so that the record does not name a
  command that does not exist. **The `op` field is therefore a sentinel, not a broken reverse** — but a
  strict A16 reader that keys on `inverse.op` will see the word `UNIMPLEMENTED` on all six mutating ids'
  records, and that is worth a decision by whoever owns the A16 contract rather than a silently accepted
  string.
- **`tests/fork-sources.txt` does not reproduce from its own header recipe on this base commit**, and did
  not before this branch: six entries (`tests/control-detect-commands.py`, `tests/control-mcp-group-coverage.py`,
  `tests/lua-api-surface.py`, `tests/mcp_stdio_session.py`, `tests/src/core/ImportDetectionTest.cpp`,
  `tests/src/core/ScriptDawBindingTest.cpp`) are listed but not derived by any pathspec line of the recipe.
  This branch adds its own file to all forty python pathspec lines so that entry IS derived, and did **not**
  delete or reword the six: that is a pre-existing red for the merge tip to resolve, not for this lane to
  silence.
