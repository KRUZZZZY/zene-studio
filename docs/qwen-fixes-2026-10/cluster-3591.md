# Cluster 3591 — malformed tempo-event tick becomes tick zero

**Verdict: ALREADY-FIXED** (on this branch, before this session).

## Finding

`src/core/TempoMap.cpp:379` (historical): `QString::toInt()` returns zero when a
present `tick` attribute is malformed or out of range, so
`<event tick="nonsense" bpm="120"/>` loaded as a 120-BPM event at tick 0.

## Evidence

The fix is already present in `src/core/TempoMap.cpp`, in `loadSettings`:

- `src/core/TempoMap.cpp:384-387` — the tick is parsed with an `ok` flag and the
  event is refused when a PRESENT tick cannot be read:

  ```cpp
  bool tickOk = true;
  event.tick = static_cast<tick_t>(
      node.attribute(QStringLiteral("tick"), QStringLiteral("0")).toInt(&tickOk));
  if (!tickOk) { continue; }
  ```

  `QString::toInt(&ok)` sets `ok = false` for text that is not a number and for
  a number outside the int32 range, so both malformed and out-of-range ticks are
  rejected; an ABSENT tick still defaults to 0 (start of the map), which is the
  documented behaviour.

- Regression test already in the tree:
  `tests/src/core/TempoMapPersistenceTest.cpp:164` —
  `aMalformedEventTickIsRejectedRatherThanBecomingZero`.

- The fix landed on this branch as commit `8523e6f1c`
  ("fix(tempo-map): a malformed tempo-event tick loaded as tick zero", Luna
  cluster 3591), which touched `src/core/TempoMap.cpp` (+9/-1) and
  `tests/src/core/TempoMapPersistenceTest.cpp` (+48). No further code change is
  needed; nothing was edited this session.

## Verification commands (this session)

All logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/3591.r1/logs`:

```
cmake --build build --target zene TempoMapPersistenceTest   > logs/build-1.log 2>&1   EXIT=0
cd build/tests && ctest -R '^TempoMapPersistenceTest$' --output-on-failure > logs/test-1.log 2>&1   EXIT=0
cd build/tests && ctest -R 'TempoMap' --output-on-failure > logs/test-2.log 2>&1   EXIT=8
```

(test-2.log EXIT=8 was three test executables "Not Run" — not built yet, not
assertion failures: ControlTempoMapCommandsTest, TempoMapTest, TempoMapPanelTest.)

```
cmake --build build --target TempoMapTest TempoMapPanelTest ControlTempoMapCommandsTest >> logs/build-2.log 2>&1   EXIT=0
cd build/tests && ctest -R 'TempoMap' --output-on-failure > logs/test-3.log 2>&1   EXIT=0
```

All 5 TempoMap-family executables pass.

## Gates

```
bash tests/complexity-gate.sh --check   > logs/gate-1.log 2>&1   EXIT=0
bash tests/file-length-gate.sh --check >> logs/gate-1.log 2>&1   EXIT=0
```

## Action

No code change; only this report is committed.
