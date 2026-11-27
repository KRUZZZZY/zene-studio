FIXED

# Cluster new-p12-260: the tempo-map panel could spend hundreds of millions of steps on one bar number

## Finding

Luna BUG 11 (historical `src/gui/TempoMapPanel.cpp:93-96`): `barAt()` advanced `barStart` one bar at a
time, so filling one row cost one iteration per bar between tick 0 and the event's tick. A 1/32 metre
makes a bar `DefaultTicksPerBar / 32` = 192 / 32 = 6 ticks (`include/TimePos.h:39`), and a project
event may sit at any tick the format allows.

## Decision: REAL, and it reproduced exactly

- The loop was still there on this branch, moved to `src/gui/TempoMapPanel.cpp:132-136`
  (`while (tick - barStart >= perBar) { barStart += perBar; ++bar; }`), after the bar/meter work of
  clusters 1768, new-p9-475, new-p17-186.
- The event's tick is unbounded above: `tickProperty()` is `integerProperty(0, 0x7fffffff)`
  (`src/core/ControlVocabulary.cpp:117-120`), `transport.tempo_map_add` takes it
  (`src/core/ControlCommandsTransportMap.cpp:318`), and `TempoMap::validEvent` only refuses a negative
  tick (`src/core/TempoMap.cpp:97-104`). So an event at 2147483647 is a legal map event, and a project
  file can carry one.
- 2147483647 / 6 = 357,913,941 — measured, not argued: the RED run below reports
  `the bar walk took 357913941 steps` for the row at tick 0x7fffffff. That is the "hundreds of millions"
  the finding describes, and it happens on the GUI thread inside `TempoMapPanel::refresh()`
  (`src/gui/TempoMapPanel.cpp:296`), i.e. when the panel opens (its constructor ends with `refresh()`,
  `:292`), and again after every edit.
- The freeze is the defect; the bar NUMBER was right. Nothing else about the walk changed.

## The fix

`src/gui/TempoMapPanel.cpp:139-149` — advance by the quotient of the remaining ticks and the metre
instead of one bar per iteration:

```cpp
const int barsAhead = (tick - barStart) / perBar;
if (barsAhead > 0)
{
	barStart += barsAhead * perBar;
	bar += barsAhead;
	++g_barWalkSteps;   // TEST SEAM, LMMS_TESTING only
}
```

- `barsAhead * perBar <= tick - barStart`, so the product cannot overflow `int`; `bar` grows to at most
  357,913,942 in a 1/32 map, well inside `int`.
- Negative remainders (a corrupt event below `barStart`) do exactly what the `while` did: nothing.
- The result is bit-for-bit the same bar the per-bar walk computed — the regression test asserts the
  old answer (bar 357913942) as well as the step count, so a wrong quotient fails too.
- The metre-change branch (`:152-164`) is untouched: it is one `++bar`, never a loop.

## Test seam (why the regression test can see the defect at all)

A huge walk returns the same value as a divided one, so no output distinguishes them. The seam measures
the work, deterministically: `g_barWalkSteps` counts the advance steps a refresh's walks take
(`src/gui/TempoMapPanel.cpp:114-119`, reset at `:298`, read through
`TempoMapPanel::barWalkStepsForTest()`, `include/TempoMapPanel.h:67-73` and
`src/gui/TempoMapPanel.cpp:339-344`), compiled only under `LMMS_TESTING`, which
`tests/CMakeLists.txt:13` defines for the object build the tests link. The seam is inert in product and
`zene` builds. It is its own commit so the 4b revert below could reverse the fix alone.

## Tests

`tests/src/gui/TempoMapPanelTest.cpp:269` — `theBarWalkDoesNotStepOncePerBar`: a 1/32 event at tick 0,
a tempo event at 0x7fffffff, then the panel's own refresh; asserts (a) row 1 reads bar `357913942`,
the answer the per-bar walk gave, and (b) the walk took at most 4 steps. No wall clock, no sleeps, no
threads: the count is an exact function of the inputs.

Test matrix for this area:

| Path | Covered by | Result |
| --- | --- | --- |
| Normal (4/4, two events) | `thePanelEditsTheMapThroughTheCommands`, `theMapRoundTripsThroughAMidiFile` | PASS |
| Metre changes mid-map (3/4, off-grid 3/4) | `theBarColumnCountsTheTimeSignatureEvents`, `anOffGridMetreChangeStartsItsOwnBar` | PASS |
| Exhaustion (event at the last tick, 1/32 metre) | `theBarWalkDoesNotStepOncePerBar` (new) | PASS with the fix, FAIL without it |
| Reconfiguration (metre re-derived per metre event, `perBar` recomputed at `:156`) | the two metre tests above plus the new one | PASS |
| Refusals (no half, bad metre, negative tick) | `ControlTempoMapCommandsTest`, `TempoMapTest` | PASS, untouched |
| Audio thread | `barAt()` is called only from `refresh()` (`:323`), the panel's GUI-thread path; the change removes work, adds none, allocates and locks nothing | by inspection |
| Supported platforms | plain `int` arithmetic plus the `std::max` already there; no new header, no Qt-6-only API | builds here |

## Callers and siblings

- `barAt()` has one caller: `TempoMapPanel::refresh()` (`src/gui/TempoMapPanel.cpp:323`).
- No other bar-stepping loop exists in the tree: `++bar` appears only in `TempoMapPanel.cpp`
  (`:135` before the fix, `:145` and `:158` now), and `musicaltime::split`
  (`src/core/MusicalTime.cpp:87-88`) already divides by `perBar`.

## Commands and exit codes

Logs in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p12-260.r1/logs/`. ctest runs from `build/tests`.

| Step | Command | EXIT | Result |
| --- | --- | --- | --- |
| Seam build | `cmake --build build --target zene TempoMapPanelTest -j4` | 0 | `build-1-seam.log` |
| RED build | `cmake --build build --target TempoMapPanelTest -j4` | 0 | `build-2-red.log` |
| RED | `ctest -R '^TempoMapPanelTest$' --output-on-failure` | **8** | `the bar walk took 357913941 steps`; 8 passed, 1 failed (`test-1-red.log`) |
| GREEN build | `cmake --build build --target zene TempoMapPanelTest -j4` | 0 | `build-3-green.log` |
| GREEN | `ctest -R '^TempoMapPanelTest$' --output-on-failure` | **0** | 100% passed (`test-2-green.log`) |
| GREEN count | `ctest -V -R '^TempoMapPanelTest$'` | **0** | `Totals: 9 passed, 0 failed` (`test-6-count.log`) |
| Family build | `cmake --build build --target ControlTempoMapCommandsTest TempoMapPersistenceTest TempoMapTest TapTempoTest BrowserTempoKeyGuiTest -j4` | 0 | `build-4-family.log` |
| Family | `ctest -R '^(TempoMapPanelTest\|ControlTempoMapCommandsTest\|TempoMapPersistenceTest\|TempoMapTest\|TapTempoTest\|BrowserTempoKeyGuiTest\|ControlTempoMapPersistence)$' --output-on-failure` | **0** | 7/7 passed (`test-3-family.log`) |
| 4b: patch | `git diff -- src/gui/TempoMapPanel.cpp include/TempoMapPanel.h > .../logs/fix.patch` | 0 | patch holds only the quotient fix |
| 4b: unfix | `git apply -R .../logs/fix.patch` | 0 | |
| 4b: unfixed build | `cmake --build build --target zene TempoMapPanelTest -j4` | 0 | `build-5-4b-unfixed.log` |
| 4b: unfixed run | `ctest -R '^TempoMapPanelTest$' --output-on-failure` | **8** | same `FAIL!` line, 357913941 steps (`test-4-4b-unfixed.log`) |
| 4b: refix | `git apply .../logs/fix.patch` | 0 | |
| 4b: fixed build | `cmake --build build --target zene TempoMapPanelTest -j4` | 0 | `build-6-4b-fixed.log` |
| 4b: fixed run | `ctest -R '^TempoMapPanelTest$' --output-on-failure` | **0** | 100% passed (`test-5-4b-fixed.log`) |
| GUI proof | `QT_QPA_PLATFORM=offscreen python3 gui-shot-p12-260.py build/zene <logs>/ui` | 0 | real `zene` headless: 1/32 at tick 0 + 120 BPM at 2147483647, `window.tempo_map`, `window.screenshot` of the panel (`gui-shot-1.log`) |
| Gates (wrapper) | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | **1** | **did not run: environment refusal**, see below (`gate-1-feature-check.log` EXIT=0 before my commits, `gate-2/3/4-feature-check.log` EXIT=1 after them) |

GUI proof artifact: `logs/ui/tempo-map-bar-column.png`, 703 x 360, 28916 bytes, sha256
`b8c41de94c418a55360f51a80dc0f04d79cefa9fa467e3107001a1ba2efafbd4`. It shows both rows — bar 1 (1/32)
and bar 357913942 at tick 2147483647 — drawn immediately by `window.screenshot`. The driver is
`logs/gui-shot-p12-260.py` (scratch, not committed): it starts the real `build/zene` headless over
`--control-socket`, runs `transport.tempo_map_add` twice, `window.tempo_map`, then `window.screenshot`
with `"window": "active"`.

### Gates: what ran, and the one wrapper that cannot run here

`feature_check.py` wraps eight gates. Before my commits it returned EXIT=0 (`gate-1-feature-check.log`)
because its ratchet baseline for that merge base (`fa4a5f2e7`) was already cached. After my commits the
train moved (`050/m0-train-52` now contains the old tip), so the merge base became `9913d36d53`, whose
baseline is not cached; the wrapper then checks that commit out in its own cache tree and the Bunny git
shim refuses it:

```
bunny-guard: 'git -C .../qwen/gate-baselines/tree checkout -q --detach 9913d36d53...' is not allowed
in a Bunny lane (AGENTS.md rules 1, 2, 11, 12).  (exit 97)
```

That is a refusal of the wrapper's own infrastructure step (`gate-4-feature-check.log`), not a gate
finding about this change. Each gate was therefore run directly in the worktree:

| Gate | Command | EXIT | Log |
| --- | --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 | `gate-fork-sources.log` |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 | `gate-all-sources-reproduce.log` |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 | `gate-no-upstream-regression.log` |
| check-namespace | `tests/scripted/check-namespace` | 0 | (stdout empty, EXIT=0) |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 | `gate-no-tautology.log` |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 | `gate-doc-figures.log` |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 0 | `gate-file-length.log` |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | **2** | `gate-complexity.log`: `lizard is not installed — run: pip install lizard` |

`lizard` is not on this box (`python3 -c "import lizard"` → `ModuleNotFoundError`), so the complexity
ratchet cannot be evaluated here, and the earlier wrapper PASS for it was vacuous (`feature_check.py`
counts "no REGRESSION line in your files" as PASS, which an absent tool also produces). What that gate
would say is nevertheless determined by its own rule: it fails on a NEW function over CCN 10 or on an
existing one whose CCN RISES (`tests/complexity-gate.sh` header). This change adds no function and the
replaced decision is one-for-one (`while (tick - barStart >= perBar)` → `if (barsAhead > 0)`), so
`barAt`'s CCN is unchanged (`src/gui/TempoMapPanel.cpp:128-169`). I did not install `lizard`: that would
change a shared machine's tooling for every other lane.

Consequently every row's verdict is known and green on this tree, and the wrapper's single remaining
obstacle is its own cache fetch:

- the six simple gates: EXIT 0 each, run directly (table above);
- file-length: `PASS (check mode: no regressions)`, **0** `REGRESSION:` lines anywhere
  (`gate-file-length.log`), so the wrapper's "no new item in your files" rule holds whatever baseline
  it compares against;
- complexity: no tool, hence no `REGRESSION:` lines, so the same rule yields PASS (vacuous — stated, not
  hidden);
- `submit_check.py` therefore reports `FAIL gates (feature_check.py)` for this reason alone
  (`submit-check-2.log`, and `submit-check-3.log` at this commit), with the wrapper's exit 97 refusal as
  the output; its four mechanical items PASS.

## Self-check

Answers to the five items `submit_check.py` asks:

1. **The regression test FAILS with the product change reverted and PASSES with it (exit codes).** RED
   without the fix: `test-1-red.log`, ctest **EXIT=8**,
   `FAIL! : TempoMapPanelTest::theBarWalkDoesNotStepOncePerBar() 'steps <= 4' returned FALSE. (the bar
   walk took 357913941 steps)`. GREEN with it: `test-2-green.log`, **EXIT=0**. 4b, from this commit:
   `git apply -R logs/fix.patch` → **EXIT=0**, rebuild → **EXIT=0**, ctest → **EXIT=8** with the same
   line (`test-4-4b-unfixed.log`); `git apply logs/fix.patch` → **EXIT=0**, rebuild → **EXIT=0**, ctest
   → **EXIT=0** (`test-5-4b-fixed.log`). The patch was taken after the seam commit, so reversing it
   leaves the test compiling.
2. **The test is deterministic.** No wall-clock timing, sleeps, threads or process timing. It uses the
   `LMMS_TESTING` seam `TempoMapPanel::barWalkStepsForTest()` (`include/TempoMapPanel.h:67-73`,
   `src/gui/TempoMapPanel.cpp:339-343`), an exact integer step count for a map of two fixed events
   (1/32 at tick 0, 120 BPM at 2147483647) plus the bar number that map must produce.
3. **The fix is at the cause and complete.** The cause is the per-bar `while` in `barAt()`
   (`src/gui/TempoMapPanel.cpp:139-149` now). `barAt()` has one caller, `refresh()` (`:323`); the only
   other bar-stepping loop in the tree was this one (`++bar` appears nowhere else), and
   `musicaltime::split` (`src/core/MusicalTime.cpp:87-88`) already divides. The metre-change branch
   (`:152-164`) is a single `++bar` and was left as it was; the header declaration and definition of
   the seam moved together, and every target that includes the header was rebuilt (above).
4. **No collateral damage.** The fix computes the same bar the old loop computed (the test pins bar
   `357913942` for tick 2147483647 in a 1/32 map), so saved projects show exactly the bars they did.
   No limit or check was added anywhere, so no other caller can newly be refused. The seam is compiled
   out of product and `zene` builds (`LMMS_TESTING` only, `tests/CMakeLists.txt:13`), and the test's own
   edits leave the song's tempo map cleared at the end.
5. **ALREADY-FIXED / FALSE-POSITIVE evidence.** Not applicable: the verdict is FIXED, and item 1 is the
   run that shows the defect was live on this branch before the change.

Also checked: **realtime, undo, commands** — `refresh()` is GUI-thread code, the change adds no
allocation, lock, wait, I/O or logging, adds no control command, and `transport.tempo_map_*` still make
one undo step each (their tests pass unchanged). **Registration** — no new files; `include/TempoMapPanel.h`
and `src/gui/TempoMapPanel.cpp` are already in `tests/fork-sources.txt` (`:1438`, `:2417`), so neither
list changed. **Gates** — six pass, file-length passes, complexity cannot run here (see the table above
and the exact refusal output).

## Other findings

- Not fixed here, out of scope: `barAt()` is recomputed from row 0 for every row, so a map with many
  events still costs O(rows x events) per refresh. The events are capped at `TempoMap::MaxEvents`
  (128), so this is small; it is a cleanup, not this bug.
- Not fixed here: `durationInTicks`-style formatting of such a huge tick in the Position column reads
  `11184811.3.3.7` because `musicaltime` uses the project's single metre, not the map's; that is the
  existing behaviour of theme 7's position column, unrelated to this defect.
