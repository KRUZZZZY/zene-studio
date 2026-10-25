# Cluster new-p14-285: a full script command queue must not swallow a DAW edit in silence

**Verdict: FIXED**

## Round 2: the one-slot case (the review's blocking finding)

The review was right, and the hole was mine: a model edit travels as TWO
commands - its undo checkpoint and its value write - and I had only guarded
the FIRST of them. With exactly one queue slot free, `enqueueModelWrite()`
queued the checkpoint, the write was dropped, and the script was told the
change was refused - while the journal kept a checkpoint for a change that
never happened: an EMPTY undo step, one that restores nothing.

The fix reserves the room for the pair BEFORE either command is queued, so the
change is wholly queued or wholly refused:

- `include/ScriptEngine.h:159` / `src/core/ScriptCommandQueue.cpp:57` -
  `ScriptCommandQueue::push(first, second)`: the producer-side pair. The
  reservation is taken from the ring's free space (`m_buffer.free() < 2` ->
  refused, both commands dropped and counted as two dropped commands) and the
  pair is then written in ONE ring write, so the two commands land together.
- `include/ScriptEngine.h:329` / `src/core/ScriptEngine.cpp:727` -
  `ScriptEngine::enqueue(first, second)`: the engine's door to that pair.
- `src/core/ScriptDawEdit.cpp:149` - `enqueueModelWrite()` queues the
  checkpoint and the write through that one reservation.

The reservation is sound because the producer is the only writer: the room can
only grow between the check and the write (the consumer only ever frees it).

The regression for it is a new test file, `tests/src/core/ScriptDawPairReservationTest.cpp`:

- `aModelWriteWithOneSlotLeftLeavesNoStrandedUndoCheckpoint` (`:136`) - the
  queue is left exactly ONE slot short of full, the mute is refused, and both
  the model and the undo depth are unchanged. The room is measured, never
  assumed: the queue is filled to the brim, emptied, and filled again (which
  leaves the same room behind, because the queue was full both times); the
  count of what fitted is the room, and the queue is then filled to one slot
  short of it.
- `aModelWriteWithRoomForThePairIsQueuedWhole` (`:170`) - the reservation must
  not cost a change its undo step: with room for the pair, both commands land,
  the model moves, and ONE undo puts it back.

The new test lives in its own file on purpose: the file
`tests/src/core/ScriptDawQueueFullTest.cpp` is the one the QA pass reads, and
leaving it untouched keeps its cases and this one side by side without either
replacing the other.

The finding was real. `enqueueDaw` in `src/core/ScriptDawEdit.cpp` threw away the
enqueue result, and `ScriptEngine::enqueue()` itself returned `void`, so nothing
downstream could learn that the bounded queue had dropped a command. The
DAW-control write half (`setName`, `setEnabled`, `setGain`, `setMuted`,
`setSoloed`, `addChannel`, `loadEffect`, `removeEffect`) therefore returned
normally while their mutation was dropped on the floor. The queue counted the
drops (`ScriptEngine::droppedCommandCount()`), but the script was told nothing:
a lost edit that says nothing is a lie the script cannot act on.

## Evidence in the tree (before the fix)

- `src/core/ScriptEngine.cpp:721` - `void ScriptEngine::enqueue(const ScriptCommand&)`:
  `m_commands.push(command);` - the queue's refusal result is discarded.
- `include/ScriptEngine.h:298` - the declaration returned `void`, so no caller
  could ask whether the command was queued.
- `src/core/ScriptDawEdit.cpp:88-91` - `void enqueueDaw(ScriptCommand command)`:
  `ScriptEngine::instance()->enqueue(command);` - the finding's `where`.
- `src/core/ScriptDawEdit.cpp:106-118` - `enqueueModelWrite()` pushed the undo
  checkpoint and the write, and ignored both results.
- The drop itself is by design and unchanged: `src/core/ScriptEngine.cpp` counts
  it (`m_commands.push()` returns false, the count is kept), and spec section 4
  (`docs/specs/SPEC-lua-api-v0.md`) says a command-queue overflow drops the
  command and never blocks the audio thread. The bug is that the SCRIPT was
  never told, not that the command was dropped.

## The fix

1. `include/ScriptEngine.h:297-306` / `src/core/ScriptEngine.cpp:721-725` -
   `enqueue()` now returns `bool`: `false` means the bounded queue was full and
   the command was dropped. The count and the drop itself are unchanged.
2. `src/core/ScriptDawEdit.cpp:88-118` (the refusal helpers `logRefusal()` at `:88`,
   `queueFullRefusal()` at `:100`, `enqueueDaw()` at `:111`, `enqueueModelWrite()`
   at `:132`)
   take the op's name and report a refusal on the script's own console (the
   `logRefusal()` channel this file already uses for every other refusal):
   `"[error] renaming this channel refused: the script command queue is full
   (1024 commands); the edit was dropped"`.
   `enqueueModelWrite()` refuses the change WHOLE: a queue with no room for the
   undo checkpoint has no room for the write either, so the model write is
   refused together with its checkpoint - the change stays one honest undo
   step, and no checkpoint is stranded without its write.
3. Every mutator in the file now names its op: `src/core/ScriptDawEdit.cpp`
   `addChannel` (`:354`), `setGain` (`:375`), `setMuted` (`:382`),
   `setSoloed` (`:390`), `setName` (`:406`), `loadEffect` (`:429`),
   `removeEffect` (`:441`), `setEnabled` (`:459`).
4. The two siblings of that path in the same half were checked and fixed the
   same way:
   - `src/core/ScriptCommandBridge.cpp:293` - the registry-call path into the
     same queue. It used to enqueue and then read a default (empty) failure
     back; it now answers the script with the reason the command was dropped.
   - `src/core/ScriptDawBindings.cpp:596` - `LuaFloatModel::setValue()` (the
     `gainModel():setValue()` path) enqueued a model write with no result
     check; it now refuses with the same message.

Not changed, on purpose: the older v0 binding layer (`src/core/ScriptBindings.cpp`)
keeps its documented "failed pushes are dropped and counted" behaviour
(`docs/LUA-API-STABILISATION.md:29`), which is what the existing flood test in
`ScriptEngineTest` asserts. That file is grandfathered in
`tests/file-length-baseline.tsv:3` at 930 code lines with a growth tolerance of
0 (`tests/file-length-gate.sh`), so adding refusal paths there would need a
gate re-anchor. The finding's `where` is the DAW-control write half, and that
is where the fix lands.

## The regression test

`tests/src/core/ScriptDawQueueFullTest.cpp` (new; registered in
`tests/CMakeLists.txt`, `tests/fork-sources.txt`, `tests/all-sources.txt`).

The test fills the queue the way a script fills it - through the same public
`enqueue()` - with commands that are no-ops when the apply side runs them
(a checkpoint with no journalled object behind it), then calls a DAW mutator and
reads the answer the way a script reads one: from the console. No clock, no
sleep, no race: the queue holds a fixed number of commands and the producer is
the only writer.

Three cases, one per kind of write in the half:

- `aRenameOnAFullQueueIsRefusedAndTheScriptIsTold` - `setName` on a full queue:
  the channel keeps its name, and the console carries a refusal that names the
  queue.
- `aMuteOnAFullQueueIsRefusedAndTheScriptIsTold` - `setMuted`: the mute model
  is untouched, and the refusal is reported.
- `anEffectSwitchOnAFullQueueIsRefusedAndTheScriptIsTold` - `setEnabled`: the
  effect keeps its state, and the refusal is reported.

## Evidence table

| Check | Exact command and result |
| --- | --- |
| RED before fix | `ctest -R ScriptDawQueueFullTest --output-on-failure > .../logs/test-red-1.log 2>&1; echo EXIT=$?` from `build/tests`; **EXIT=8**. 3 of 5 slots failed: "the dropped rename was not reported to the script: " (empty console), the same for the mute and the effect switch. |
| GREEN after fix | Same ctest command, output to `.../logs/test-green-2.log`; **EXIT=0**, 1/1 CTest target passed (5 slots: init, 3 cases, cleanup). |
| Reversed-fix proof | `git apply -R .../logs/fix.patch; echo EXIT=$?` **EXIT=0**; `cmake --build build --target zene ScriptDawQueueFullTest ScriptDawRemovalUndoTest ControlAutomationScriptTest ScriptEngineTest ScriptMemoryBudgetTest > .../logs/build-8.log 2>&1` **EXIT=0**; ctest to `.../logs/test-revert-1.log` **EXIT=8** - the same 3 assertions of the new test fail again. |
| Restored-fix proof | `git apply .../logs/fix.patch; echo EXIT=$?` **EXIT=0**; rebuild to `.../logs/build-9.log` **EXIT=0**; ctest to `.../logs/test-green-3.log` **EXIT=0**, 1/1 passed. |
| Affected family | `ctest -R "Script|Lua" --output-on-failure > .../logs/test-family-3.log 2>&1; echo EXIT=$?` - **14 CTest targets executed, 10 passed, 4 failed**. The 4 failures are pre-existing and environmental (proven below), and are byte-identical with and without the fix. |
| Pre-existing failures | `diff` of the failing assertions in `.../logs/test-revert-1.log` (fix reverted) and `.../logs/test-family-1.log` (fix applied) shows exactly one difference: the 3 assertions of the new test. The other failures are identical in both runs: `ControlAutomationScriptTest` (4 slots, "no track with parameters in the fresh song"), `ScriptDawRemovalUndoTest` (11 slots, effect-chain undo), `ScriptEngineTest` (1 slot, "The plugin \"tripleoscillator\" wasn't found or could not be loaded!"), `ScriptMemoryBudgetTest` (1 slot, the simulated allocation failure succeeds because the host overcommits). |
| Round-2 RED (pair fix reverted) | `git apply -R .../logs/fix-round2.patch; echo EXIT=$?` **EXIT=0**; `cmake --build build --target ScriptDawPairReservationTest ScriptDawQueueFullTest > .../logs/build-15.log 2>&1` **EXIT=0**; `ctest -R "ScriptDawPairReservationTest|ScriptDawQueueFullTest" --output-on-failure > .../logs/test-round2-red-1.log 2>&1` **EXIT=8** - `aModelWriteWithOneSlotLeftLeavesNoStrandedUndoCheckpoint` fails: "Actual (engine->processCommands()): 512, Expected (pending): 511" - the checkpoint was applied although the write was refused. |
| Round-2 GREEN (pair fix restored) | `git apply .../logs/fix-round2.patch; echo EXIT=$?` **EXIT=0**; rebuild to `.../logs/build-16.log` **EXIT=0**; `ctest -R "ScriptDawPairReservationTest|ScriptDawQueueFullTest" --output-on-failure > .../logs/test-round2-green-1.log 2>&1` **EXIT=0**, 2/2 CTest targets passed. |
| Round-2 affected family | `ctest -R "Script|Lua" --output-on-failure > .../logs/test-round2-family-1.log 2>&1; echo EXIT=$?` - **15 CTest targets executed, 11 passed, 4 failed**. The 4 failures are the same pre-existing ones as in round 1: the diff of the failing assertions against `.../logs/test-family-3.log` is empty (17 identical failing assertions, no new one). |
| Round-2 gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py . > .../logs/gate-4.log 2>&1; echo EXIT=$?` - **EXIT=0**, `RESULT: PASS` (13 changed files vs the base). |
| Gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-285 > .../logs/gate-1.log 2>&1; echo EXIT=$?` - **EXIT=0**, `RESULT: PASS` (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity "no new item in your files", file-length "no new item in your files"). |
| Realtime / undo / commands | No audio-thread code touched: `enqueue()` and the refusal run on the script worker thread, the queue is never blocked, and no new control command was added. The undo checkpoint is still queued immediately before its write, and a refused write takes its checkpoint with it - no stranded checkpoint, no empty undo step. |

## Build and test commands

All builds ran from the worktree root; all ctest runs ran from `build/tests`.
Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-285.r1/logs`.

```text
cmake --build build --target ScriptDawQueueFullTest > .../logs/build-1.log 2>&1; echo EXIT=$?  # EXIT=2, the new target did not exist in the configured tree yet
cmake -S . -B build > .../logs/cmake-1.log 2>&1; echo EXIT=$?  # EXIT=97, the lane guard refuses a manual configure
cmake --build build --target ScriptDawEffectsTest > .../logs/build-2.log 2>&1; echo EXIT=$?  # EXIT=0, this re-ran the configure and picked up the new target
cmake --build build --target ScriptDawQueueFullTest > .../logs/build-3.log 2>&1; echo EXIT=$?  # EXIT=2, first compile of the test: newMixerChannel() returns a reference, not a pointer
cmake --build build --target ScriptDawQueueFullTest > .../logs/build-4.log 2>&1; echo EXIT=$?  # EXIT=0
ctest -R ScriptDawQueueFullTest --output-on-failure > .../logs/test-red-1.log 2>&1; echo EXIT=$?  # EXIT=8, RED on the unfixed tree
cmake --build build --target zene ScriptDawQueueFullTest > .../logs/build-5.log 2>&1; echo EXIT=$?  # EXIT=0
ctest -R ScriptDawQueueFullTest --output-on-failure > .../logs/test-green-1.log 2>&1; echo EXIT=$?  # EXIT=8, the mute case still failed: the refused checkpoint did not report a refusal - gap closed before the RED/GREEN proof
cmake --build build --target zene ScriptDawQueueFullTest > .../logs/build-6.log 2>&1; echo EXIT=$?  # EXIT=0
ctest -R ScriptDawQueueFullTest --output-on-failure > .../logs/test-green-2.log 2>&1; echo EXIT=$?  # EXIT=0, GREEN
cmake --build build --target zene ScriptDawQueueFullTest ScriptDawRemovalUndoTest ControlAutomationScriptTest ScriptEngineTest ScriptMemoryBudgetTest > .../logs/build-7.log 2>&1; echo EXIT=$?  # EXIT=0, the rest of the script family rebuilt
ctest -R "Script|Lua" --output-on-failure > .../logs/test-family-1.log 2>&1; echo EXIT=$?  # EXIT=8, 14 targets: the 4 pre-existing failures below
git diff -- include/ScriptEngine.h src/core/ScriptEngine.cpp src/core/ScriptDawEdit.cpp src/core/ScriptDawBindings.cpp src/core/ScriptCommandBridge.cpp > .../logs/fix.patch; echo EXIT=$?  # EXIT=0
git apply -R .../logs/fix.patch; echo EXIT=$?  # EXIT=0, the product fix is reverted, the test stays
cmake --build build --target zene ScriptDawQueueFullTest ScriptDawRemovalUndoTest ControlAutomationScriptTest ScriptEngineTest ScriptMemoryBudgetTest > .../logs/build-8.log 2>&1; echo EXIT=$?  # EXIT=0
ctest -R "ScriptDawQueueFullTest|ScriptDawRemovalUndoTest|ControlAutomationScriptTest|ScriptEngineTest|ScriptMemoryBudgetTest" --output-on-failure > .../logs/test-revert-1.log 2>&1; echo EXIT=$?  # EXIT=8, the 3 new assertions fail again, and the pre-existing failures are unchanged
git apply .../logs/fix.patch; echo EXIT=$?  # EXIT=0, the fix is restored
cmake --build build --target zene ScriptDawQueueFullTest ScriptDawRemovalUndoTest ControlAutomationScriptTest ScriptEngineTest ScriptMemoryBudgetTest > .../logs/build-9.log 2>&1; echo EXIT=$?  # EXIT=0
ctest -R "ScriptDawQueueFullTest" --output-on-failure > .../logs/test-green-3.log 2>&1; echo EXIT=$?  # EXIT=0, GREEN
ctest -R "Script|Lua" --output-on-failure > .../logs/test-family-2.log 2>&1; echo EXIT=$?  # EXIT=8, the same pre-existing failures
cmake --build build --target ScriptBudgetActionTest > .../logs/build-10.log 2>&1; echo EXIT=$?  # EXIT=0, the one family target that had not been built in this tree
ctest -R "Script|Lua" --output-on-failure > .../logs/test-family-3.log 2>&1; echo EXIT=$?  # EXIT=8, 14 targets executed: 10 passed, the 4 pre-existing failures
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-285 > .../logs/gate-1.log 2>&1; echo EXIT=$?  # EXIT=0, RESULT: PASS
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-285 > .../logs/gate-2.log 2>&1; echo EXIT=$?  # EXIT=0, RESULT: PASS, re-run after the report and the manifests were staged (10 changed files)
cmake --build build --target ScriptDawQueueFullTest > .../logs/build-11.log 2>&1; echo EXIT=$?  # EXIT=0, the re-run of the configure that picks up the new test target
cmake --build build --target zene ScriptDawQueueFullTest ScriptDawPairReservationTest ScriptDawBindingTest ScriptDawEffectsTest ScriptDawRemovalUndoTest ScriptEngineTest ScriptBindingsTest ScriptCommandBridgeTest ScriptStabilisationTest ScriptImportTest ScriptClockTest ScriptMemoryBudgetTest ControlAutomationScriptTest > .../logs/build-12.log 2>&1; echo EXIT=$?  # EXIT=2, -Werror=sign-compare on the new test's loop bound
cmake --build build --target zene ScriptDawQueueFullTest ScriptDawPairReservationTest ScriptDawBindingTest ScriptDawEffectsTest ScriptDawRemovalUndoTest ScriptEngineTest ScriptBindingsTest ScriptCommandBridgeTest ScriptStabilisationTest ScriptImportTest ScriptClockTest ScriptMemoryBudgetTest ControlAutomationScriptTest > .../logs/build-13.log 2>&1; echo EXIT=$?  # EXIT=0
ctest -R "ScriptDawPairReservationTest" --output-on-failure > .../logs/test-pair-green-1.log 2>&1; echo EXIT=$?  # EXIT=8, the "room for the pair" case asserted the wrong undo-step count: a model write journals ITSELF, so the change leaves two journal entries and one undo
cmake --build build --target ScriptDawPairReservationTest > .../logs/build-14.log 2>&1; echo EXIT=$?  # EXIT=0
ctest -R "ScriptDawPairReservationTest" --output-on-failure > .../logs/test-pair-green-2.log 2>&1; echo EXIT=$?  # EXIT=0, GREEN
git diff -- include/ScriptEngine.h src/core/ScriptCommandQueue.cpp src/core/ScriptEngine.cpp src/core/ScriptDawEdit.cpp > .../logs/fix-round2.patch; echo EXIT=$?  # EXIT=0
git apply -R .../logs/fix-round2.patch; echo EXIT=$?  # EXIT=0, the pair reservation is reverted, the tests stay
cmake --build build --target ScriptDawPairReservationTest ScriptDawQueueFullTest > .../logs/build-15.log 2>&1; echo EXIT=$?  # EXIT=0
ctest -R "ScriptDawPairReservationTest|ScriptDawQueueFullTest" --output-on-failure > .../logs/test-round2-red-1.log 2>&1; echo EXIT=$?  # EXIT=8, RED: 512 commands applied where 511 were queued - the stranded checkpoint
git apply .../logs/fix-round2.patch; echo EXIT=$?  # EXIT=0, the pair reservation is restored
cmake --build build --target zene ScriptDawQueueFullTest ScriptDawPairReservationTest ScriptDawBindingTest ScriptDawEffectsTest ScriptDawRemovalUndoTest ScriptEngineTest ScriptBindingsTest ScriptCommandBridgeTest ScriptStabilisationTest ScriptImportTest ScriptClockTest ScriptMemoryBudgetTest ControlAutomationScriptTest ScriptBudgetActionTest > .../logs/build-16.log 2>&1; echo EXIT=$?  # EXIT=0
ctest -R "ScriptDawPairReservationTest|ScriptDawQueueFullTest" --output-on-failure > .../logs/test-round2-green-1.log 2>&1; echo EXIT=$?  # EXIT=0, 2/2 passed
ctest -R "Script|Lua" --output-on-failure > .../logs/test-round2-family-1.log 2>&1; echo EXIT=$?  # EXIT=8, 15 targets: the same 4 pre-existing failures
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py . > .../logs/gate-4.log 2>&1; echo EXIT=$?  # EXIT=0, RESULT: PASS
```

Files committed:

- `include/ScriptEngine.h`
- `src/core/ScriptEngine.cpp`
- `src/core/ScriptDawEdit.cpp`
- `src/core/ScriptDawBindings.cpp`
- `src/core/ScriptCommandBridge.cpp`
- `tests/src/core/ScriptDawQueueFullTest.cpp` (new)
- `tests/src/core/ScriptDawPairReservationTest.cpp` (new)
- `tests/CMakeLists.txt`
- `tests/fork-sources.txt`
- `tests/all-sources.txt`
- `src/core/ScriptCommandQueue.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p14-285.md`

## Self-check

The submit check asks five questions; here they are with the evidence.

**1. "The regression test FAILS with your product change reverted and PASSES with it (exit codes)."**

- Reverted (`git apply -R .../logs/fix.patch`, EXIT=0; rebuild to `build-8.log`,
  EXIT=0): `ctest -R "ScriptDawQueueFullTest|..."` to `.../logs/test-revert-1.log`,
  **EXIT=8** - all three slots of `ScriptDawQueueFullTest` fail with
  "'!refusal.isEmpty()' returned FALSE. (the dropped rename was not reported to
  the script: )" (the same for the mute and the effect switch).
- Restored (`git apply .../logs/fix.patch`, EXIT=0; rebuild to `build-9.log`,
  EXIT=0): `ctest -R ScriptDawQueueFullTest` to `.../logs/test-green-3.log`,
  **EXIT=0**, 1/1 CTest target passed.
- The very first run on the untouched tree is the same RED
  (`.../logs/test-red-1.log`, EXIT=8, 3 failed / 2 passed).

**2. "The test is deterministic: no wall-clock timing, sleeps or races (name the seam or injected values it uses)."**

- Fill seam: the public `ScriptEngine::enqueue()` - the same entry point a
  script's own edits use - filled with 4096 no-op commands (a checkpoint with no
  journalled object, so the apply side changes nothing). The queue holds a fixed
  number of commands and the producer is the only writer, so "the queue is
  full" is a fact, not a race.
- Read seam: `ScriptEngine::takeLogMessages()` - the script's own console
  buffer, cleared before each case.
- No clock, no sleep, no timeout, no thread hand-off and no injected
  timestamp: the test asserts on the state of the queue and on the console, both
  of which are set by the calls it makes itself.

**3. "The fix is at the cause and complete: every caller and sibling of what you changed was checked and stays consistent."**

- The cause is `ScriptEngine::enqueue()` discarding the queue's refusal
  (`include/ScriptEngine.h:297-306`, `src/core/ScriptEngine.cpp:721-725`); it
  now returns it.
- Every call site of `enqueue()` in the tree was listed and checked
  (`grep -rn "enqueue(" src/`): the DAB write half
  (`src/core/ScriptDawEdit.cpp:113`, `:144`, `:154`), the parameter wrapper
  (`src/core/ScriptDawBindings.cpp:596`) and the registry-call bridge
  (`src/core/ScriptCommandBridge.cpp:293`) all check the result and refuse;
  the older v0 layer (`src/core/ScriptBindings.cpp`, 18 call sites) keeps its
  documented "dropped and counted" behaviour (`docs/LUA-API-STABILISATION.md:29`)
  and is grandfathered in the file-length ratchet at 930 code lines with a
  growth tolerance of 0 (`tests/file-length-baseline.tsv:3`,
  `tests/file-length-gate.sh`), so refusal paths cannot be added there without
  re-anchoring a gate.
- Siblings of the changed functions inside the file: `enqueueDaw()` (the plain
  op) and `enqueueModelWrite()` (the checkpoint + write pair) both refuse, and
  the eight mutators that call them all name their op, so the script is told
  WHICH edit was lost.
- The two paths into the same queue from outside the file (the bridge and the
  parameter wrapper) were checked and made consistent.

**4. "No collateral damage: old projects and saved data still load as before, and any limit or check you added does not hit other callers that legitimately exceed it."**

- No save format, no file, no parameter default and no migration was touched:
  the change is the script-side refusal report only.
- No new limit was added. The queue keeps its 1024-command capacity and its
  drop-and-count behaviour; the drop counter is unchanged
  (`ScriptEngine::droppedCommandCount()`), and the existing flood test in
  `ScriptEngineTest` still sees the same drops.
- The refusal only fires where a command was ALREADY being dropped, so no
  caller that legitimately queues more than 1024 commands is hit by a new
  check - it now gets a console line it did not get before, and nothing else.
- No new control command was added (the control-command snapshot is unmoved),
  the queue is still never blocked, and the audio thread is untouched: the
  refusal is logged on the script worker thread, the producer side.
- Undo stays one honest step: the undo checkpoint is still queued immediately
  before its write, and a refused write refuses its checkpoint too
  (`src/core/ScriptDawEdit.cpp:144-150`), so no checkpoint is stranded without
  its write and no write loses its undo step.

**5. "If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended contract that shows it."**

- Not applicable: the verdict is FIXED. The bug was present on this branch
  (`src/core/ScriptDawEdit.cpp:88-91` discarded the enqueue result and
  `include/ScriptEngine.h:298` declared `void enqueue(...)`), which is what the
  RED run above shows.

**Round-2 answers (the review's blocking finding).**

- **"Reserve queue space for both the checkpoint and model write before
  enqueueing either."** - done, and at the cause: the reservation lives in the
  queue itself (`src/core/ScriptCommandQueue.cpp:57`,
  `include/ScriptEngine.h:159`), and `enqueueModelWrite()` uses the one
  reservation for the pair (`src/core/ScriptDawEdit.cpp:149`). The room is
  taken from the ring's free space BEFORE either command is written, so the
  change cannot land half of itself.
- **"Keep the refusal report"** - kept: a refused pair is reported on the
  script's console with the same message as before
  ("[error] muting this channel refused: the script command queue is full
  (1024 commands); the edit was dropped"), and the refusal now covers the
  pair as a whole.
- **"Add a deterministic one-slot-left regression that verifies the model and
  undo depth remain unchanged"** - added in
  `tests/src/core/ScriptDawPairReservationTest.cpp:136`:
  `aModelWriteWithOneSlotLeftLeavesNoStrandedUndoCheckpoint`. It leaves the
  queue exactly one slot short of full (the room is measured by filling and
  emptying the queue, never assumed), then asserts all three: the refusal is
  reported, the model did not move, and the undo depth did not change - plus
  the applied-command count, which is the direct witness that no checkpoint
  was left behind (512 commands applied where 511 were queued, in the
  reverted state).
- **Determinism** - the test uses the public `enqueue()`,
  `pendingCommandCount()`, `processCommands()` and `takeLogMessages()` only,
  and the room is a measured quantity. No clock, no sleep, no timeout.
- **Every caller and sibling checked** - the pair enqueue has exactly one
  caller (`enqueueModelWrite`); the single-command `enqueue()` path, the
  bridge, the parameter wrapper and the older v0 layer are unchanged. The
  refusal accounting stays consistent: a refused pair counts the two dropped
  commands, as two commands were dropped.
- **No collateral damage** - the queue's capacity, its drop behaviour and its
  counter are unchanged; a change that FITS is queued exactly as before (both
  commands, in the same order), so old projects, saved data and the undo
  history are unaffected. The undo story is unchanged for a change that fits:
  the model write journals itself, and ONE undo restores the model
  (`aModelWriteWithRoomForThePairIsQueuedWhole`).
- **Realtime** - the reservation is a read of the ring's free space on the
  script worker thread (the producer side). No allocation, no lock, no wait,
  no logging on the audio thread; the queue is still never blocked.

**Pre-existing failures (not caused by this change).** The 4 failing family
targets fail identically with and without the fix - proved by diffing the
failing assertions of `test-revert-1.log` against `test-family-1.log`, where the
only difference is the 3 new assertions. They need plugin modules this lane's
build tree does not have (`ControlAutomationScriptTest`: "no track with
parameters in the fresh song"; `ScriptDawRemovalUndoTest`: effect-chain undo;
`ScriptEngineTest`: "The plugin \"tripleoscillator\" wasn't found or could not
be loaded!"), plus a host that does not overcommit for the memory-budget case
(`ScriptMemoryBudgetTest`).
