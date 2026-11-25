ALREADY-FIXED

# Cluster 5173: liveness tokens do not protect modulation targets from concurrent destruction

Verdict: ALREADY-FIXED on branch `bugs/qwen-5173`. Two earlier commits replaced the check-then-use
window the finding describes with a pin-and-wait lifetime scheme. Both are ancestors of HEAD:

- `73ecb52ba3 fix(core): pin modulation targets during audio writes (cluster 5173)`
- `97789a3fd0 fix(core): scope modulation teardown gates to target generations (cluster 5173)`

`git merge-base --is-ancestor 73ecb52ba3 HEAD; echo A=$?` printed `A=0`. The same check for
`97789a3fd0` printed `B=0`.

The finding cites an older shape of the code. In that shape, `drivingSource` checked `targetAlive` and
the caller then wrote through the raw `entry.model` with nothing held in between. The earlier
SKIPPED-SCOPE note quotes that shape as `ModulationLayerPublisher.cpp:189` / `:222`. That window no
longer exists.

## Evidence (file:line on this branch)

- `include/ModulationLayer.h:418-435` has `beginTargetWrite(slot, token)`. On the audio thread it
  CAS-increments a writer count in `m_targetWriters[slot]`. The increment happens only while that
  atomic word still carries this token's generation and the closing bit is clear. It then re-checks
  `targetAlive`. It does not allocate, lock or wait: a failed check returns `false`, and the block
  skips the entry.
- `include/ModulationLayer.h:437-443` has `endTargetWrite(slot)`, which releases the pin with a
  `fetch_sub`.
- `src/core/ModulationLayerPublisher.cpp:300-317` is `applyModulationBlock`. It takes the pin at `:302`
  and holds it through an RAII guard, `TargetWritePin`, at `:303-308`. The raw-pointer writes happen
  while the pin is held: `writeModulatedEntry(entry.model, ...)` at `:316`, and the per-voice
  `entry.instrument->modulateNote` through `applyVoiceEntry` at `:312`. `drivingSource` (`:273-281`)
  now only reads the view's plain bytes and the token. It never dereferences the model.
- `src/core/AutomatableModel.cpp:184` emits `aboutToBeDestroyed()` at the entry of
  `~AutomatableModel`, before any value state is torn down. `src/core/Instrument.cpp:78` and
  `src/tracks/InstrumentTrack.cpp:255` do the same for an instrument target (cluster 5186).
- `src/core/ModulationLayerPublisher.cpp:222-238`: for every published target, `publishView` connects
  those signals to `retireTarget`.
- `src/core/ModulationLayerPublisher.cpp:130-159` is `retireTarget`. On the destroying thread it sets
  the closing bit with a CAS on the same atomic word, then yields until the writer count for this
  generation reaches 0, and only then clears `m_alive`. Destruction continues only after
  `retireTarget` returns.
- `src/core/ModulationLayerPublisher.cpp:174-200`: a republish closes every slot and waits for its
  writers in the same way, and it issues new tokens. A view published earlier therefore cannot pin a
  slot that has been rebound.
- `src/core/AutomatableModel.cpp:180-182` asserts that a model is never destroyed on an audio worker
  during a period. So the spin in `retireTarget` runs off the audio thread.

Why this closes the race (design reasoning; local TSan does not run on this box): the writer's
increment and the destroyer's closing bit are read-modify-writes on one atomic word. They are
therefore totally ordered.

- If the increment comes first, the destroyer sees a non-zero count and waits until the write at
  `:316` has finished.
- If the closing bit comes first, the CAS in `beginTargetWrite` fails its generation/closing check and
  the entry is skipped.

In both cases the model cannot be freed while the audio thread is writing through it, which is the fix
the finding asks for ("defer target destruction until audio readers have exited").

## Regression test (already on the branch, added by 73ecb52ba3)

`tests/src/core/ModulationAudioViewTest.cpp:222-271` is
`targetDestructionWaitsForAnInFlightAudioWrite`. Its audio thread parks inside the model write through
a `dataChanged` hook. Another thread then deletes the model. The test checks two things: the gate
closes, and the destruction does not finish while the write is still parked. The sibling
`rebindDuringDestructionIgnoresAStaleTargetClosure` (`:273`) covers the generation scoping that
97789a3fd0 added.

## Commands run this session

| Command | EXIT | Log |
|---|---|---|
| `cmake --build build --target zene ModulationAudioViewTest -j4 > logs/build-1.log 2>&1; echo EXIT=$?` | 0 | build-1.log |
| `cd build/tests && ctest -R ModulationAudioView --output-on-failure > logs/test-1.log 2>&1; echo EXIT=$?` | 0 | test-1.log (`1/1 Test #186: ModulationAudioViewTest ... Passed`, `100% tests passed out of 1`) |
| `cd build/tests && ZENE_CONTROL_CHECK_RESULTS=1 ./ModulationAudioViewTest -v1 > logs/test-2.log 2>&1; echo EXIT=$?` | 0 | test-2.log (`PASS : ...targetDestructionWaitsForAnInFlightAudioWrite()`, `PASS : ...rebindDuringDestructionIgnoresAStaleTargetClosure()`, `Totals: 16 passed, 0 failed`) |

`logs/` is `/home/kruzzzzy/.config/kads/zene/state/runs/5173.r1/logs`.

There is no product change, so no RED/GREEN reversal was run in this session. That proof belongs to
commits 73ecb52ba3 and 97789a3fd0.

## Gates (step 5): blocked by the environment

| Command | EXIT | Log |
|---|---|---|
| `python3 .../qwen/feature_check.py <worktree> > logs/gates-1.log 2>&1; echo EXIT=$?` (run by Haiku) | 1 | gates-1.log |
| same command, run again in the lead session's shell `> logs/gates-2.log` | 1 | gates-2.log |

Neither run produced a gate table. In both, the gate script's own baseline step was refused by the
lane guard before any gate ran:

```
bunny-guard: 'git -C /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/gate-baselines/tree checkout -q --detach 3436a808b65a59d362b15837adc1308ea67bf675' is not allowed in a Bunny lane (AGENTS.md rules 1, 2, 11, 12). ...
subprocess.CalledProcessError: Command '[... 'checkout', '-q', '--detach', '3436a808b6...']' returned non-zero exit status 97.
```

As the rules require, I did not work around the guard. Running the gates needs the harness to allow
feature_check.py's baseline checkout in `qwen/gate-baselines/tree`. The diff of this job is one new
Markdown file under `docs/` and no product, test or registration file. So there is nothing for the
complexity, file-length or registration gates to measure, but this is unverified because the gate did
not run.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.** This job has no
  product change, so there is nothing to revert here. The regression test
  `targetDestructionWaitsForAnInFlightAudioWrite` came in with fix commit 73ecb52ba3. This session it
  PASSES on HEAD (test-1.log EXIT=0, test-2.log EXIT=0, `Totals: 16 passed, 0 failed`). The RED run
  belongs to that earlier commit's own record; I did not re-run it this session.
- **The test is deterministic.** It synchronises through a `Model::dataChanged` hook with
  `testsupport::BinarySemaphore`. The audio write parks at the hook, and the destroyer is observed
  through the `targetAlive` gate (`ModulationAudioViewTest.cpp:228-262`). It has no sleeps. The 2 s
  `try_acquire_for` and deadline only bound a hang: on a correct build they never decide the result.
- **The fix is at the cause and complete; callers and siblings checked.** Every raw-pointer write on
  the audio path is in `applyModulationBlock` (`ModulationLayerPublisher.cpp:300-317`). Both writes
  there, the model write at `:316` and the instrument write at `:312`, happen under the pin taken at
  `:302`. Both teardown sources are wired to `retireTarget`: model destruction
  (`AutomatableModel.cpp:184` → `:227`) and instrument destruction (`Instrument.cpp:78`,
  `InstrumentTrack.cpp:255` → `:233`). A rebind closes the slot and waits for its writers the same way
  (`:174-200`). `drivingSource` (`:273-281`) never dereferences the model.
- **No collateral damage.** This job changes no product code, no saved data and no limits. It adds
  only this report.
- **ALREADY-FIXED: the code that shows it.** `include/ModulationLayer.h:418-443`
  (`beginTargetWrite`/`endTargetWrite`), `src/core/ModulationLayerPublisher.cpp:130-159`
  (`retireTarget`), `:222-238` (watches) and `:302-316` (pinned write), and
  `src/core/AutomatableModel.cpp:184`. The fix commits 73ecb52ba3 and 97789a3fd0 are ancestors of HEAD
  (merge-base EXIT 0 for both).

## Other findings

- The gate is blocked by the environment: in this lane, `feature_check.py` cannot check out its
  baseline (see Gates above). Every lane that runs this gate under the same guard will hit it.

Delegation: Haiku (two starts) ran the build, the ctest and direct test runs, the first gate run and
the first completion check. I checked their key log lines myself. Sonnet and DeepSeek were not used:
the bug was already fixed, so there was no fix or test to write.
