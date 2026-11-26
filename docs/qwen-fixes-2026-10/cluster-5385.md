FIXED

Cluster 5385 - clearing an active tempo map leaves playback at its last mapped tempo.

## Decision: REAL (measured, not argued)

The finding names `Song::followTempoMap`'s inactive branch. On this branch (before
the fix) that branch was `src/core/Song.cpp:1551-1555`:

```cpp
if( !active )
{
    m_tempoMapAppliedTempo = -1;
    return;
}
```

It dropped the follower's memo and wrote nothing else. `Engine::s_framesPerTick`
is a persistent `std::atomic<float>` (`src/core/Engine.cpp:152-156`, `:161-171`)
whose only writers are:

| writer | site | when |
|---|---|---|
| the engine's own value | `Engine::updateFramesPerTick()` | `Song::setTempo()` (`src/core/Song.cpp:398`), the Link release (`src/core/Song.cpp:495`), the `sampleRateChanged` slot (`src/core/Song.cpp:3104`) |
| a mapped tempo | `Engine::updateFramesPerTickForTempo()` | the follower, `src/core/Song.cpp:1569` |

Neither tempo-map command touches the scalar: `transport.tempo_map_clear`
(`src/core/ControlCommandsTransportMap.cpp:436`) and
`transport.tempo_map_set_active` both edit through `editMapWithUndo`
(`src/core/ControlCommandsTransportMap.cpp:157-171`), which is
`Song::tempoMap().edit(...)` - the seqlock publication only
(`docs/TEMPO-MAP.md` section 4). So once the follower applied a mapped tempo,
nothing reverted it when the map went away. Confirmed by the RED run: after
clearing an applied 70 BPM map on a 140 BPM song the engine still reported
`787.5` frames/tick where the global tempo is `393.75`
(`logs/19-test-red-final.log`).

Reachable through both doors the finding names: `tempo_map_clear` (clears the
events AND switches the map off) and `tempo_map_set_active(false)` (switches it
off, events kept). Same defect through a third door, which this fix also closes:
`Song::clearProject()` dropped the memo without the scalar
(`src/core/Song.cpp:1854-1855` before the fix) and every accepted project load
runs it first (`src/core/Song.cpp:2112` in `Song::loadProject`), so a project
opened after one with an active map inherited the mapped rate when its own global
tempo happened to equal the previous one (no `setTempo()` signal, no restore).
The map element reader (`src/core/SongSections.cpp:136-137`) drops the memo too;
because `clearProject()` has already handed the rate back by then, that site
needs no call of its own.

## Fix (at the cause, one rule in two places)

`src/core/Song.cpp:1551-1565` - the inactive branch hands the engine's own rate
back in the one block that sees a map it had applied go inactive. Guarded on the
memo, so a map that was never applied still writes nothing: the empty-map path
stays byte-identical (`docs/TEMPO-MAP.md` section 3), and an inactive map that
was never in force costs one relaxed load.

```cpp
if( m_tempoMapAppliedTempo.load( std::memory_order_relaxed ) != -1 )
{
    m_tempoMapAppliedTempo.store( -1, std::memory_order_relaxed );
    Engine::updateFramesPerTick();
}
```

`src/core/Song.cpp:1862-1873` - `clearProject()` pairs the memo drop with the
same restore, for the load path above.

`docs/TEMPO-MAP.md` section 4 records the rule and the one exception to the
byte-identity promise. No header declaration changed, so no extra target needed
rebuilding beyond the library the header-free `Song.cpp` edit already rebuilds.

## Regression test

`tests/src/core/TempoMapTest.cpp` - `droppingAnAppliedMapRestoresTheEngineRate`.

It measures the engine's own scalar and the ticks the transport actually carries,
in this order (the state sequence): a project with no map at the global 140 BPM
(`393.75` frames/tick) -> the same event with the map ON at 70 BPM (`787.5`, the
transport retimed, fewer ticks in the same 16 periods) -> the map CLEARED while
the mapped rate is the scalar (back to `393.75`, and the same 16 periods carry
the same tick count as the unmapped run) -> the map ON again -> the map SWITCHED
OFF with its event kept (back to `393.75` again). Every count starts from the
scalar AS IT IS: the existing `advancePlayhead()` helper resets it and would hide
this defect, which is why the new test does not use it.

Determinism: `Engine::audioEngine()->audioDev()->stopProcessing()` in
`initTestCase` means the test alone drives `Song::processNextBuffer()`
synchronously; no wall clock, no sleeps, no threads.

## Commands and exit codes (from `logs/`, all under
`/home/kruzzzzy/.config/kads/zene/state/runs/5385.r1/logs/`)

RED (test only, product code at HEAD):

```text
cmake --build build --target zene TempoMapTest -- -j4 > logs/1-build-red.log 2>&1
EXIT=0                              (logs/1-build-red.exit)
cd build/tests && ctest -R '^TempoMapTest$' --output-on-failure > logs/2-test-red.log 2>&1
EXIT=8 - FAIL! TempoMapTest::droppingAnAppliedMapRestoresTheEngineRate()
         'clearing the map left the engine at 787.5 frames/tick; the global tempo 140 is 393.75'
         (logs/2-test-red.exit)
```

GREEN (fix applied):

```text
cmake --build build --target zene TempoMapTest -- -j4 > logs/3-build-green.log 2>&1
EXIT=0                              (logs/3-build-green.exit)
cd build/tests && ctest -R '^TempoMapTest$' --output-on-failure > logs/4-test-green.log 2>&1
EXIT=0 - 1/1 passed                 (logs/4-test-green.exit)
```

Proof that the test catches the bug (repeated on the final, committed product
state; `docs/TEMPO-MAP.md` is documentation and carries no code):

```text
git diff -- src/core/Song.cpp > logs/fix.patch
git apply -R logs/fix.patch
EXIT=0
cmake --build build --target zene TempoMapTest -- -j4 > logs/18-build-red-final.log 2>&1
EXIT=0                              (logs/18-build-red-final.exit)
cd build/tests && ctest -R '^TempoMapTest$' --output-on-failure > logs/19-test-red-final.log 2>&1
EXIT=8 - FAIL! the same assertion, 787.5 vs 393.75   (logs/19-test-red-final.exit)
git apply logs/fix.patch
EXIT=0
cmake --build build --target zene TempoMapTest -- -j4 > logs/20-build-green-final.log 2>&1
EXIT=0                              (logs/20-build-green-final.exit)
cd build/tests && ctest -R 'TempoMap' --output-on-failure > logs/21-test-green-final.log 2>&1
EXIT=0 - 5/5 passed (ControlTempoMapCommandsTest, TempoMapPersistenceTest,
         TempoMapTest, TempoMapPanelTest, ControlTempoMapPersistence)
                                    (logs/21-test-green-final.exit)
```

Family and collateral runs:

```text
ctest -R 'TempoMap'  (before the family targets were built) logs/8-test-refix-family.log  EXIT=8
   -> TempoMapTest Passed; 3 targets "Not Run" because their binaries did not exist yet
cmake --build build --target ControlTempoMapCommandsTest TempoMapPersistenceTest TempoMapPanelTest -- -j4
                                    logs/9-build-family.log  EXIT=0
ctest -R 'TempoMap'                 logs/10-test-family.log  EXIT=0 - 5/5 passed
cmake --build build --target LinkPhaseLockTest UndoHonestyTest AutomationModesTest -- -j4
                                    logs/11-build-collateral.log  EXIT=0
ctest -R '^(LinkPhaseLockTest|UndoHonestyTest|AutomationModesTest)$'
                                    logs/15-test-collateral3.log  EXIT=0 - 3/3 passed
./TempoMapTest droppingAnAppliedMapRestoresTheEngineRate
                                    logs/16-test-function.log  EXIT=0
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>
                                    logs/17-feature-check.log  EXIT=0 - RESULT: PASS
```

## Test matrix

| path | covered by |
|---|---|
| normal: map applied, cleared | `droppingAnAppliedMapRestoresTheEngineRate`, clear half |
| normal: map applied, switched off (events kept) | same test, deactivate half |
| reconfiguration: global tempo changed while a map is in force | existing `globalTempoChangeInvalidatesMemoBeforeRenderResumes` (still passes) |
| reconfiguration: project replaced (`clearProject` + load) | `TempoMapPersistenceTest`, `ControlTempoMapCommandsTest` (its `init()` clears the project before every case) |
| never-applied map (byte identity) | existing `theMapRetimesThePlayheadAndAnInactiveOneDoesNot` and `theEmptyMapIsThePreChangeArithmeticVerbatim` (still pass) |
| exhaustion / refusal | not touched: the fix adds no limit and no refusal |
| audio thread | see below |
| supported platforms | C++20, `std::atomic` relaxed load/store and the engine's existing float expression; no POSIX-only call, no Qt API added |

## Realtime

`Song::followTempoMap()` and `Song::clearProject()` additions are an atomic
relaxed load, an atomic relaxed store and `Engine::updateFramesPerTick()`, which
is `one reader of the audio device's sample rate, one float divide and one relaxed
atomic store` (`src/core/Engine.cpp:152-156`): no allocation, no lock, no logging,
no Qt call, no signal. This is the call the Link release already makes from the
same per-block path (`src/core/Song.cpp:495`), and the audio thread already reads
`m_tempoModel.value()` in this function. The guarded branch runs at most once per
map transition; a project with no map, and the steady state with a map, execute
exactly the instructions they executed before.

Undo: the fix adds no command and no undo step; `tempo_map_clear` /
`tempo_map_set_active` keep their single action checkpoint
(`src/core/ControlCommandsTransportMap.cpp:157-171`), and `control.undo` writes
the captured map back through the same `edit()`, so the follower re-applies it on
the next block.

## Self-check

1. **The regression test FAILS with the product change reverted and PASSES with
   it.** Reverted: `git apply -R logs/fix.patch` EXIT=0, rebuild EXIT=0,
   `ctest -R '^TempoMapTest$'` **EXIT=8** (`logs/19-test-red-final.log`).
   Restored: `git apply logs/fix.patch` EXIT=0, rebuild EXIT=0,
   `ctest -R 'TempoMap'` **EXIT=0**, 5/5 (`logs/21-test-green-final.log`).
2. **Deterministic.** No wall clock, sleep, thread or repeat-until-it-flaps. The
   test stops the dummy device thread in `TempoMapTest::initTestCase`, drives
   `Song::processNextBuffer()` itself, and asserts (a) the engine's own scalar
   against the transcribed engine expression, and (b) exact tick counts over a
   fixed number of periods, both from a known scalar.
3. **At the cause and complete.** Every writer of `Engine::s_framesPerTick` and
   every site that drops `m_tempoMapAppliedTempo` was read and is listed in the
   decision table above. `setTempo()` and the Link release already paired the memo
   drop with the restore; `clearProject()` and the follower did not and now do.
   Both command doors (`clear`, `set_active`) go through the same
   `TempoMapPublisher::edit`, and the deactivate door is asserted separately. No
   sibling command of the `transport.*` group touches the scalar.
4. **No collateral damage.** The guard is an equality test on the follower's own
   memo, not a limit, so nothing that legitimately exceeds anything is rejected.
   A map that was never applied writes nothing, so the empty-map byte-identity
   path is unchanged (existing tests still pass). Old projects: nothing in the
   save/load format changed; `TempoMapPersistenceTest` (byte-identical re-save and
   map round trip) and `ControlTempoMapPersistence` pass.
5. **ALREADY-FIXED / FALSE-POSITIVE:** not applicable; the verdict is FIXED with
   the measured RED above.

## Other findings

- `ControlAutomationModesTest` fails in this build tree for an environment
  reason, not because of this change: it reports *"this song exposes no
  automatable device parameter ... the binary needs the instrument modules
  (LMMS_TEST_PLUGIN_DIR, see tests/CMakeLists.txt)"*
  (`logs/14-test-collateral2.log`). This job's rules forbid setting
  `LMMS_PLUGIN_DIR`, so the missing capability is reported rather than worked
  around. The test was not previously built in this tree and does not cover the
  tempo map.
- `git status` in the worktree was clean on arrival; `build/` is untracked and
  ignored by the repository.
