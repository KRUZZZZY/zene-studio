# Cluster review-873124875 — Tempo insertion inverse fails to restore inactive map state

Verdict: **ALREADY-FIXED** on this branch (`bugs/qwen-review-873124875`, HEAD `06c5dcf48`).
No product change was needed or made; the fix is already present in commit `0f0d746af`
("fix(rework 873124875): tempo_map_add advertised a remove that left an activated map active").

## Finding recap

Inserting into an inactive tempo map autoactivates it (an add sets the map active unless
`active:false` is passed), but the advertised inverse `transport.tempo_map_remove` keeps
the active flag it finds, so re-issuing it left the map active and did not restore the
previous inactive state. The intended contract also allows empty-active maps, so the
fix must not globally deactivate on remove; instead the add must stop advertising a
remove whenever it changed the active flag, and the recorded action checkpoint (the
whole captured map) is the way back.

## Code evidence (current tree)

- `src/core/ControlCommandsTransportMap.cpp:284-301` (`addReply`): computes
  `activeChanged = song->tempoMap().map().active() != before.active()` and, when
  `replaced || activeChanged`, calls `mutationResult(..., QString(), QJsonObject())`
  so no `transport.tempo_map_remove` inverse is advertised; the mechanism stays the
  action checkpoint.
- `src/core/ControlCommandsTransportMap.cpp:157-165` (`editMapWithUndo`): records an
  action checkpoint that restores the captured `before` map (including `m_active`)
  through `TempoMapPublisher::edit` on undo/redo.
- `src/core/ControlCommandsTransportMap.cpp:393-428` (`registerRemoveCommand`): the
  remove's own advertised inverse (`transport.tempo_map_add`) carries
  `active=before.active()` (cluster 4906/4000), so a remove on an inactive map
  restores inactive state; remove itself never deactivates, preserving empty-active
  maps.

## Test evidence (the finding's exact scenario)

`tests/src/core/ControlTempoMapCommandsTest.cpp`:

- `anAddThatActivatesAnInactiveMapAdvertisesNoRemoveInverse` (line ~449): inactive
  map with one event, add at tick 768 autoactivates the map, the recorded transaction
  names no inverse command, and `control.undo` restores the inactive one-event map.
- `contractRowsClassifyTheGroup` (line ~392): its first add is to an empty inactive
  map that this add also activates; the record now names the checkpoint, not a remove.
- `everyMapEditUndoesToItsPreCommandState` (line ~516): case (1) undoes the first
  event back to an empty AND inactive map; further cases undo set_active, clear, etc.
- `removeInverseRebuildsRampAndInactiveState` (line ~418): re-issuing the advertised
  inverse of a remove on an inactive map keeps the map inactive and restores ramp.

## RED/GREEN proof that the tests catch the defect

The product state was temporarily reverted by removing the `activeChanged` term from
the `addReply` condition (leaving only `replaced`), then restored.
LOGS = `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/review-873124875.r1/logs`.

1. RED (activeChanged handling reverted):

   ```
   python3 - <<'PY'
   p='src/core/ControlCommandsTransportMap.cpp'
   s=open(p).read()
   s=s.replace('\tconst bool activeChanged = song->tempoMap().map().active() != before.active();\n\tif (replaced || activeChanged)\n', '\tif (replaced)\n')
   open(p,'w').write(s)
   PY
   cmake --build build --target ControlTempoMapCommandsTest  (EXIT=0)
   cd build/tests && ctest -R ControlTempoMapCommandsTest --output-on-failure  (EXIT=8)
   ```

   Result: `0% tests passed, 1 tests failed out of 1`
   (`anAddThatActivatesAnInactiveMapAdvertisesNoRemoveInverse` reported a remove
   where "" was expected).

2. GREEN (fix restored):

   ```
   cp /tmp/opencode/product_fixed.cpp src/core/ControlCommandsTransportMap.cpp
   cmake --build build --target ControlTempoMapCommandsTest  (EXIT=0)
   cd build/tests && ctest -R ControlTempoMapCommandsTest --output-on-failure  (EXIT=0)
   ```

   Result: `100% tests passed out of 1` (9 tests).

## Gates

- `bash tests/complexity-gate.sh --check` — EXIT=1 on this tree, but only due to
  pre-existing baseline entries (68 grandfathered functions not in the stale fork
  baseline; `addReply` itself is small and introduces no new over-target function).
- `bash tests/file-length-gate.sh --check` — EXIT=1 on this tree, but only due to
  pre-existing fork files already over 500 code lines absent from the 5-entry fork
  baseline; `src/core/ControlCommandsTransportMap.cpp` was not changed.

## Files committed

- `docs/qwen-fixes-2026-10/cluster-review-873124875.md` (this report).
No product or test files were changed.
