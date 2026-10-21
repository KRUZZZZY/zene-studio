# Cluster new-p9-475: "Add at playhead" can overwrite the mapped meter - and two siblings in the same file

**Verdict: FIXED**

new-p9-475: FIXED
new-p17-187: FIXED
new-p17-186: FIXED

Three bugs, one file (`src/gui/TempoMapPanel.cpp`), one build. Each was decided on its
own, each has its own regression test in `tests/src/gui/TempoMapPanelTest.cpp`, and each
has its own RED/GREEN proof (the bug's own product change reverted, the bug's own test
fails, restored, it passes).

## new-p9-475: FIXED

**"Add at playhead" can overwrite the mapped meter.** `TempoMapPanel::addEvent()` sent the
song's GLOBAL time signature (and its global tempo) with every new event, so adding an
event in a map that was in 3/4 at the playhead put the song's 4/4 into the map at that
tick and reset the mapped metre there.

- Before: `src/gui/TempoMapPanel.cpp:270-280` - `addEvent()` built
  `transport.tempo_map_add` with `song->getTimeSigModel().getNumerator()` /
  `getDenominator()` and `song->getTempo()`.
- The map's own answers live in the engine: `TempoMap::timeSignatureAtTick()` and
  `TempoMap::tempoAtTick()` (`include/TempoMap.h:243`, `include/TempoMap.h:242`),
  reached from the panel through `song->tempoMap().map()` - the same access the control
  command uses (`src/core/ControlCommandsTransportMap.cpp:223`).
- After: `src/gui/TempoMapPanel.cpp:320-341` - the event carries the metre
  (`map.timeSignatureAtTick(tick, globalSignature)`) and the tempo
  (`map.tempoAtTick(tick, song->getTempo())`) the MAP has in force at that tick, and a
  tempo inside a ramp keeps its ramp (`rampsAt()`, `src/gui/TempoMapPanel.cpp:84-101`),
  so the curve is not flattened either. The engine's answers are the global values when
  the map is empty, inactive, or has no event at or before the tick - so an empty map
  behaves exactly as before.
- Regression: `tests/src/gui/TempoMapPanelTest.cpp:192` -
  `theAddAtPlayheadKeepsTheMappedMetreAndTempo()` puts a 3/4 @ 120 BPM event at tick 0
  in the map (the song stays 4/4), adds an event at tick 480 through the panel's own
  `addEvent()` (what the "Add at playhead" button calls) and reads the added event back
  from `transport.tempo_map_get`: it must carry 3/4 and 120 BPM, not the song's 4/4 and
  140 BPM.
- RED: the test failed with actual `4` / expected `3` (numerator) before the fix
  (`test-1-red.log`, `test-4-red-reversed.log`); GREEN after it (`test-2-green.log`,
  `test-5-green-reapplied.log`).

**The tempo half of the same call.** The finding names the time signature, but the same
line sent the song's global TEMPO as well, which flattened a mapped tempo in exactly the
same way (the map's tempo in force at the playhead would be replaced by the song's).
The fix therefore carries the map's tempo as well - the sibling of the changed field -
and the regression asserts it (120 BPM, not the song's 140). A tempo inside a ramp is
kept on the ramp, so the added event does not break the curve.

## new-p17-187: FIXED

**"Ramp to Next" silently does nothing on a meter-only row.** The Ramp button was
enabled for any selected row, but `toggleRamp()` returned `false` at once for an event
without a tempo, and the click handler ignored that result: the click changed nothing
and said nothing.

- Before: `src/gui/TempoMapPanel.cpp:160-163` - `ramp->setEnabled(m_table->currentRow() >= 0)`
  for any row; `src/gui/TempoMapPanel.cpp:287` - `toggleRamp()` returned `false` for a
  row with no tempo, in silence.
- After: `src/gui/TempoMapPanel.cpp:205-212` - the button is enabled only for a row that
  holds a tempo (`rowHasTempo()`, `src/gui/TempoMapPanel.cpp:96-102`, fed by the
  `has_tempo` flag the refresh already reads from `transport.tempo_map_get` and stores
  in the row's item data), and `toggleRamp()` names the refusal in the summary line
  (`src/gui/TempoMapPanel.cpp:349-351`) for a call that does not come from the button.
  A row that holds a tempo is unchanged: the button stays enabled and the ramp still
  toggles.
- Regression: `tests/src/gui/TempoMapPanelTest.cpp:216` -
  `theRampButtonIsOffOnAMetreOnlyRow()` puts a metre-only event and a tempo event in
  the map, selects the metre-only row and expects the Ramp button to be off, then
  selects the tempo row and expects it on.
- RED: `'!ramp->isEnabled()' returned FALSE` before the fix (`test-1-red.log`,
  `test-4-red-reversed.log`); GREEN after it.

## new-p17-186: FIXED

**Off-grid meter changes can mislabel later bars.** In `barAt()` a meter event changed
`perBar` but left `barStart` anchored to the previous bar's start, so every later
boundary was computed from the old origin - the code did not do what its own comment
said ("The meter from THIS tick on").

- Before: `src/gui/TempoMapPanel.cpp:84-107` - `perBar` was replaced at the meter event
  and `barStart` was left alone, so the bar that the meter event interrupts was measured
  with the NEW metre and the bars after it ran on from the old origin.
- The rule is now defined and implemented: **a meter change starts a bar of its own at
  the event's tick** - the bar it interrupts ends there, and the new metre's bars are
  counted from the event's tick. An event that carries the metre already in force
  changes nothing (so a tempo-only event never moves the grid), and a meter change that
  falls exactly on a bar line keeps that line (the pre-existing
  `theBarColumnCountsTheTimeSignatureEvents` case is unchanged).
- After: `src/gui/TempoMapPanel.cpp:121-152` (`barAt()` now takes the project's
  numerator/denominator, `src/gui/TempoMapPanel.cpp:297-298`).
- Regression: `tests/src/gui/TempoMapPanelTest.cpp:241` -
  `anOffGridMetreChangeStartsItsOwnBar()` puts a 3/4 event at tick 100 (inside the first
  4/4 bar) and an event at tick 250: the meter event opens bar 2 (tick 100), and tick
  250 is in bar 3 (that bar ended at 100+144).
- RED: the meter event's row read bar `1` instead of `2` before the fix
  (`test-1-red.log`, `test-4-red-reversed.log`); GREEN after it.

## Evidence table

| Check | Exact command and result |
| --- | --- |
| RED before any fix | `ctest -R '^TempoMapPanelTest$' --output-on-failure` from `build/tests`, output to `test-1-red.log`; **EXIT=8**, 5 passed / 3 failed - the three new tests failed (actual `4` vs `3`; `'!ramp->isEnabled()' returned FALSE`; actual `"1"` vs `"2"`). |
| GREEN after the fix | Same ctest command, output to `test-2-green.log`; **EXIT=0**, 1/1 CTest target passed. |
| Reversed-fix proof (whole patch) | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-475.r1/logs/fix.patch` **EXIT=0**; rebuild **EXIT=0**; ctest output to `test-4-red-reversed.log` **EXIT=8**, the same three failures. |
| Restored-fix proof (whole patch) | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-475.r1/logs/fix.patch` **EXIT=0**; rebuild **EXIT=0**; ctest output to `test-5-green-reapplied.log` **EXIT=0**. |
| Per-bug proof (new-p9-475) | `git apply .../revert-bug7-new-p9-475.patch` **EXIT=0**; rebuild **EXIT=0** (`build-12-bug7-new-p9-475-reverted.log`); ctest to `test-10-bug7-new-p9-475-red.log` **EXIT=8** - 7 passed, 1 failed: `theAddAtPlayheadKeepsTheMappedMetreAndTempo` and nothing else; `git apply -R` the same patch **EXIT=0** (the final code back). |
| Per-bug proof (new-p17-187) | `git apply .../revert-bug4-new-p17-187.patch` **EXIT=0**; rebuild **EXIT=0** (`build-12-bug4-new-p17-187-reverted.log`); ctest to `test-10-bug4-new-p17-187-red.log` **EXIT=8** - 7 passed, 1 failed: `theRampButtonIsOffOnAMetreOnlyRow` and nothing else; `git apply -R` the same patch **EXIT=0**. |
| Per-bug proof (new-p17-186) | `git apply .../revert-bug3-new-p17-186.patch` **EXIT=0**; rebuild **EXIT=0** (`build-12-bug3-new-p17-186-reverted.log`); ctest to `test-10-bug3-new-p17-186-red.log` **EXIT=8** - 7 passed, 1 failed: `anOffGridMetreChangeStartsItsOwnBar` and nothing else; `git apply -R` the same patch **EXIT=0**. |
| Final build of the committed code | `cmake --build build --target TempoMapPanelTest zene -j8` output to `build-13-final.log`; **EXIT=0**. |
| Affected family | `ctest -R '^(TempoMapPanelTest\|TempoMapTest\|TempoMapPersistenceTest\|SmfInterchangeTest\|SmfInterchangeRoundTripTest\|TapTempoTest)$' --output-on-failure` from `build/tests`, output to `test-11-final.log`; **EXIT=0**, 6/6 CTest targets passed. |
| Gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>`, output to `gate-2.log`; **EXIT=0**, every row PASS (complexity and file-length: "no new item in your files"). |
| Pre-submit check | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p9-475`, output to `submit-check.log`; **EXIT=0**. |

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.**
  RED `ctest -R '^TempoMapPanelTest$'` **EXIT=8** (three failures, named above), GREEN
  **EXIT=0**; and per bug, with only that bug's product change reverted
  (`revert-bug7-new-p9-475.patch`, `revert-bug4-new-p17-187.patch`,
  `revert-bug3-new-p17-186.patch`), only that bug's test fails - the exit codes are in
  the per-bug table below. The reverse-and-restore of the whole product patch is
  `fix.patch`: **EXIT=0** to reverse, ctest **EXIT=8**, **EXIT=0** to restore, ctest
  **EXIT=0**.
- **The test is deterministic.** No wall-clock timing, no sleep, no race: the map is
  built through `transport.tempo_map_add` with fixed ticks, the panel is read through
  `transport.tempo_map_get` and the table's own cells, and the play head is never
  used - the test calls the panel's `addEvent(tick, 0)`, which is exactly what the
  "Add at playhead" button calls with the song's play position.
- **The fix is at the cause and complete.** The cause is "the panel wrote the song's
  global values into the map". Both callers of `addEvent()` were checked: the
  "Add at playhead" button (`src/gui/TempoMapPanel.cpp:231-232`) and the tests; the
  sibling field in the same call (the tempo) was fixed with it, and the sibling button
  in the same dialog ("Add...", `src/gui/TempoMapPanel.cpp:200-219`) was checked - it
  sends only a tick and a tempo, so it never carried a metre and is unchanged. The
  other two callers of the map's own state - `refresh()` and `toggleRamp()` - read the
  same `transport.tempo_map_get` payload, and the row data the ramp button now uses
  (`has_tempo`) comes from that same payload.
- **No collateral damage.** With an empty, inactive or pre-first-event map the engine
  answers the global values, so `addEvent()` writes exactly what it wrote before; the
  map's storage, save format and command surface are untouched (no new command, no new
  file, no change to `TempoMap`); the pre-existing bar test (a meter change ON a bar
  line) and the MIDI round trip still pass. The bar rule only differs from before for a
  meter change that falls BETWEEN bar lines - which is the bug.
- **Undo and realtime.** Nothing changed on the audio thread: the panel is a GUI
  widget, the engine's map is read through the same accessor the control command uses,
  and no allocation, lock, wait, I/O or logging was added to any audio path. The edit
  still goes through the single `transport.tempo_map_add` command, so it stays one
  honest undo step, and the command's reversibility class is unchanged (no new
  command).
- **The ALREADY-FIXED / FALSE-POSITIVE question does not arise.** All three bugs are
  FIXED: each one was reproduced by its own regression test on the unmodified code
  (RED, exit 8) before any product change was made, and the code each fix changed is
  quoted with its line numbers above.
- **Qt 5.15 / MSVC / macOS portability.** No new Qt API beyond `QTableWidgetItem::data()`
  and `QTableWidget::item()`, both long-standing; no `windows.h` names, no
  `std::filesystem`, no Qt-6-only call.

## Build and test commands

All builds ran from the worktree root; all ctest runs ran from `build/tests`.

```text
cmake --build build --target TempoMapPanelTest -j8 > .../logs/build-1.log 2>&1; echo EXIT=$?  # EXIT=0, the test target builds
QT_QPA_PLATFORM=offscreen ctest -R '^TempoMapPanelTest$' --output-on-failure > .../logs/test-0.log 2>&1; echo EXIT=$?  # EXIT=0, the three pre-existing tests pass before the change
cmake --build build --target TempoMapPanelTest -j8 > .../logs/build-2.log 2>&1; echo EXIT=$?  # EXIT=0, the three regressions added
QT_QPA_PLATFORM=offscreen ctest -R '^TempoMapPanelTest$' --output-on-failure > .../logs/test-1-red.log 2>&1; echo EXIT=$?  # EXIT=8, RED: 3 failed
cmake --build build --target TempoMapPanelTest -j8 > .../logs/build-3.log 2>&1; echo EXIT=$?  # EXIT=0, the fix
QT_QPA_PLATFORM=offscreen ctest -R '^TempoMapPanelTest$' --output-on-failure > .../logs/test-2-green.log 2>&1; echo EXIT=$?  # EXIT=0, GREEN
cmake --build build --target TempoMapPanelTest TempoMapTest TempoMapPersistenceTest SmfInterchangeTest SmfInterchangeRoundTripTest -j8 > .../logs/build-4.log 2>&1; echo EXIT=$?  # EXIT=2, ControlTempoMapPersistence is not a build target; the other five built
QT_QPA_PLATFORM=offscreen ctest -R '^(TempoMapPanelTest|TempoMapTest|TempoMapPersistenceTest|SmfInterchangeTest|SmfInterchangeRoundTripTest)$' --output-on-failure > .../logs/test-3-family.log 2>&1; echo EXIT=$?  # EXIT=0, 5/5
git diff -- src/gui/TempoMapPanel.cpp include/TempoMapPanel.h > .../logs/fix.patch
git apply -R .../logs/fix.patch; echo EXIT=$?  # EXIT=0
cmake --build build --target TempoMapPanelTest -j8 > .../logs/build-6-reversed.log 2>&1; echo EXIT=$?  # EXIT=0
QT_QPA_PLATFORM=offscreen ctest -R '^TempoMapPanelTest$' --output-on-failure > .../logs/test-4-red-reversed.log 2>&1; echo EXIT=$?  # EXIT=8, RED
git apply .../logs/fix.patch; echo EXIT=$?  # EXIT=0
cmake --build build --target TempoMapPanelTest -j8 > .../logs/build-7-reapplied.log 2>&1; echo EXIT=$?  # EXIT=0
QT_QPA_PLATFORM=offscreen ctest -R '^TempoMapPanelTest$' --output-on-failure > .../logs/test-5-green-reapplied.log 2>&1; echo EXIT=$?  # EXIT=0, GREEN
bash .../logs/proof-per-bug-final.sh  # per bug on the committed code: revert that bug's hunk, build, ctest (RED), restore
cmake --build build --target TempoMapPanelTest zene -j8 > .../logs/build-13-final.log 2>&1; echo EXIT=$?  # EXIT=0
QT_QPA_PLATFORM=offscreen ctest -R '^(TempoMapPanelTest|TempoMapTest|TempoMapPersistenceTest|SmfInterchangeTest|SmfInterchangeRoundTripTest|TapTempoTest)$' --output-on-failure > .../logs/test-11-final.log 2>&1; echo EXIT=$?  # EXIT=0, 6/6
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > .../logs/gate-2.log 2>&1; echo EXIT=$?  # EXIT=0
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p9-475 > .../logs/submit-check.log 2>&1; echo EXIT=$?  # EXIT=0
```

Files committed (one commit per bug, each naming its cluster):

- `3e0ff8982` fix(tempo-map-panel): "Add at playhead" keeps the mapped metre and tempo (cluster new-p9-475)
  - `src/gui/TempoMapPanel.cpp`, `include/TempoMapPanel.h`, `tests/src/gui/TempoMapPanelTest.cpp`
- `0af25b0f6` fix(tempo-map-panel): the Ramp button is off on a metre-only row (cluster new-p17-187)
  - `src/gui/TempoMapPanel.cpp`, `tests/src/gui/TempoMapPanelTest.cpp`
- `30bc514a8` fix(tempo-map-panel): an off-grid metre change starts its own bar (cluster new-p17-186)
  - `src/gui/TempoMapPanel.cpp`, `tests/src/gui/TempoMapPanelTest.cpp`, `docs/qwen-fixes-2026-10/cluster-new-p9-475.md`
