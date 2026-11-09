FIXED

# cluster gui-1030-tab-mixer-density-compact: "Compact density choice is clipped in a narrow Mixer panel"

Branch `bugs/qwen-gui-1030-tab-mixer-density-compact`, base `af010bc3cc`.
LOG FOLDERS: round 1 `~/.local/share/zene-bunny-fix/qwen/runs/gui-1030-tab-mixer-density-compact.r1/logs`,
round 2 `….r2/logs` (abbreviated `r2/logs` below).

## Round 3: integration onto `050/m0-train-26` (`af010bc3cc`)

Integration branch `integration/gui-1030-tab-mixer-density-compact-af010bc3cc`, accepted patch
`bugs/qwen-gui-1030-tab-mixer-density-compact @ ae3a90431e`. Log folder `….r3/logs` (`r3/logs` below).

**Conflict on the pinned target: none.** `git merge-base ae3a90431e af010bc3cc` is `af010bc3cc` itself, so
the accepted branch already sits on the pinned target. The staged integration tree is byte-identical
to the accepted patch: `git diff --cached ae3a90431e --stat` prints nothing. There is no conflict marker in
`include/MixerModeBar.h` or `src/gui/MixerModeBar.cpp` (`grep -c '<<<<<<<\|>>>>>>>'` = 0 for each). No
line was changed in the integration, so there is no semantic difference from the accepted patch.

**Where the reported conflict comes from: the newer line `050/m0-train-27`.** It merged
`55a36358f7` ("mode bar wraps its controls instead of clipping them", cluster
`gui-1030-tab-mixer-routing-matrix`). That commit fixes **the same root cause with a different design**:
a private `WrapLayout` class inside `src/gui/MixerModeBar.cpp`, and its own regression
`MixerModesTest::theModeBarWrapsInsteadOfClippingWhenNarrow`. This patch uses the separate
`MixerModeBarLayout` files and `addUnit`/`pushRight`. The sibling branches
`bugs/qwen-gui-1030-tab-mixer-density-expanded` (`0f78bb577b`, `d3d77ccbcc`) and
`…-pick-the-channels-to-show` (`8a4e5df266`, `63cbed3a21`) also change the same lines.
Merging this patch on top of train-27 would mean choosing one wrapping design and dropping the other.
That is a new design decision, not a conflict resolution, so I did not do it.
**For train-27 and later this cluster is a duplicate of `gui-1030-tab-mixer-routing-matrix`:** whoever
integrates there should pick one layout. This report does not resolve that. I did not check whether this
cluster's regression passes on train-27's `WrapLayout`; that would need a train-27 build, which I did not make.

**Failure paths and platform hazards (unchanged from the accepted patch):**
- A narrow panel (300 px): the bar wraps and every control keeps its `minimumSizeHint`. Tested.
- A wide panel (2400 px): one row, with Routing Matrix... at the right margin. Tested.
- Height-for-width: the parent column must honour `heightForWidth`. The QVBoxLayout in the test does, and
  the Mixer's column is a QVBoxLayout too.
- Nothing runs on the audio thread. No command, schema or saved data changed. Only Qt Widgets API is used
  (QLayout subclass), with nothing specific to one platform. Only Linux offscreen was built and tested
  here; macOS and Windows were not checked.

| Step (round 3) | Command | Result |
| --- | --- | --- |
| Build the integrated tree | `CCACHE_DISABLE=1 cmake --build build --target zene MixerModesTest MixerZonesTest MixerMeteringTest -j4 > r3/logs/01-build-green.log 2>&1` | **EXIT=0** |
| GREEN, Mixer family | from `build/tests`: `ctest -R '^Mixer(Modes|Zones|Metering)Test$' --output-on-failure > r3/logs/02-test-green.log 2>&1` | **EXIT=0**, 3/3 passed |
| 4b: export the product patch | `git diff --cached -- include/MixerModeBar.h src/gui/MixerModeBar.cpp src/gui/CMakeLists.txt include/MixerModeBarLayout.h src/gui/MixerModeBarLayout.cpp > r3/logs/fix.patch` | 404 lines |
| 4b: reverse | `git apply -R r3/logs/fix.patch` | **EXIT=0** |
| 4b: RED | `CCACHE_DISABLE=1 cmake --build build --target zene MixerModesTest -j4 > r3/logs/03-build-red.log 2>&1`; `ctest -R '^MixerModesTest$' --output-on-failure > r3/logs/04-test-red.log 2>&1` | build EXIT=0; ctest **EXIT=8**, 0/1: `'onScreen(compact)' returned FALSE. (Density: Compact is clipped in a 300 px panel)` (`r3/logs/04-test-red.log:24`) |
| 4b: re-apply | `git apply r3/logs/fix.patch` | **EXIT=0**; afterwards `git diff --stat` (unstaged) is empty |
| 4b: GREEN | `CCACHE_DISABLE=1 cmake --build build --target zene MixerModesTest MixerZonesTest MixerMeteringTest -j4 > r3/logs/05-build-green.log 2>&1`; `ctest -R '^Mixer(Modes|Zones|Metering)Test$' --output-on-failure > r3/logs/06-test-green.log 2>&1` | build **EXIT=0**; ctest **EXIT=0**, 3/3 |
| Gates, direct | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > r3/logs/07-feature-check.log 2>&1` | EXIT=1: `PermissionError` creating `<worktree>-feature-check` (outside my write-jail, the same as round 2) |
| Gates, log folder redirected | `python3 r3/logs/fc_wrap.py r3/logs/08-feature-check-logs <worktree> > r3/logs/08-feature-check.log 2>&1` | **EXIT=0**, 10 changed files, all 8 rows PASS, `RESULT: PASS` |

`fc_wrap.py` is the round-2 wrapper, copied unchanged. It runs `feature_check.py` through `runpy` and
only moves its log folder. Gates, limits and baselines are untouched.

## Round 2: the verdict was changed from FALSE-POSITIVE to REAL

Round 1 called this a false positive and was disputed, correctly. Its probe asked for a 200 px host, but
the host grew to 1632 px, so the probe never tested a narrow panel. Round 2 pinned the panel at 300 px
with `setFixedSize`, which the Mixer gets when it sits in a narrow shell splitter column
(`src/gui/shell/Shell.cpp:181-210`). The bug showed up there.

**Cause.** The mode bar placed all of its controls in one `QHBoxLayout` (old `src/gui/MixerModeBar.cpp:104`)
that could not wrap. In a 300 px panel that row squeezed every control to about 12 px. "Density: Compact"
was 12 px wide against an 82 px minimum, so its caption was clipped away. The other mode buttons, the
Show box and the chips were crushed the same way. Measured in `r2/logs/05-dbg.log:12-25` (probe
output, not committed):
`bar QRect(0,0 300x38) QSize(1610, 38)` and `"mixerMode_compact" QRect(80,4 12x30) QSize(82, 30)`.

**Fix.** A new layout, `MixerModeBarLayout` (`include/MixerModeBarLayout.h`, `src/gui/MixerModeBarLayout.cpp`),
replaces the `QHBoxLayout`:
- It places the controls in rows that wrap, and reports the height they need through `heightForWidth`, so
  the bar grows taller when its panel narrows. Each control keeps its full size.
- A caption and its choices ("Density:" Compact Expanded; "Show:" box) form one unit, which moves to the
  next row as a whole (`MixerModeBar.cpp` `addSegment` builds the unit; constructor `addUnit({show, m_filter})`).
- In a wide panel everything stays on one row as before. `pushRight()` stands in for the old
  `addStretch(1)`, keeping the Faders/Solo/Listen/Master/Zones chips and Routing Matrix... at the right end.
- No container widgets were added. Every control is still a direct child of the bar, so the
  accessibility path ("Mixer > Mixer modes") and the sweep's ids stay the same.
- The bar's minimum width falls from about 1576 px (one row) to its widest unit (about 290 px). This
  also helps the round 1 observation that the Mixer overflowed its slot.

**Regression test.** `MixerModesTest::aNarrowPanelKeepsEveryModeControlOnScreen`
(`tests/src/gui/MixerModesTest.cpp`) does the following:
- Puts a fresh `MixerModeBar` in a host fixed at **300 x 600 px**.
- Checks that `host.width() == 300`.
- Checks that Density: Compact, and every visible button and combo box on the bar, lies inside the host
  and the bar, is the widget `childAt` its centre (so nothing covers it), and is at least its
  `minimumSizeHint` (so it isn't crushed or clipped).
- Checks that the bar wrapped onto more than one row.
- Clicks Compact with `QTest::mouseClick` and checks that `density == compact`.
- Then widens the host to 2400 px and checks one row, with Routing Matrix... exactly the right margin
  from the bar's right edge, as the old stretch placed it.

The test uses no timers or sleeps: it sets fixed sizes, calls `QLayout::activate()` and processes pending
events once. A `qScopeGuard` resets the density to expanded even if a check fails. I added it after a
failing draft leaked Compact into `sendKnobKeepsItsSourceWhileFocused` (`r2/logs/11-test-green2.log:31`).

## Evidence table (round 2, final patch)

| Step | Command | Result |
| --- | --- | --- |
| RED, before any product edit | `CCACHE_DISABLE=1 cmake --build build --target zene MixerModesTest -j4 > r2/logs/06-build-red.log 2>&1`, then from `build/tests`: `ctest -R '^MixerModesTest$' --output-on-failure > r2/logs/07-test-red.log 2>&1` | build EXIT=0; ctest **EXIT=8**: `'onScreen(compact)' returned FALSE. (Density: Compact is clipped in a 300 px panel)` |
| GREEN, Mixer family | `CCACHE_DISABLE=1 cmake --build build --target zene MixerModesTest MixerZonesTest MixerMeteringTest -j4 > r2/logs/32-build-green.log 2>&1`; `ctest -R '^Mixer(Modes|Zones|Metering)Test$' --output-on-failure > r2/logs/33-test-green.log 2>&1` | EXIT=0 / EXIT=0, **3/3** tests passed |
| 4b: export the product patch | `git diff -- include/MixerModeBar.h src/gui/MixerModeBar.cpp src/gui/CMakeLists.txt include/MixerModeBarLayout.h src/gui/MixerModeBarLayout.cpp > r2/logs/fix.patch` (new files marked with `git add -N` first) | 404 lines |
| 4b: reverse the product only | `git apply -R r2/logs/fix.patch` | **EXIT=0** (test and manifests kept) |
| 4b: rebuild and test with the fix reversed | `CCACHE_DISABLE=1 cmake --build build --target zene MixerModesTest -j4 > r2/logs/34-build-red.log 2>&1`; `ctest -R '^MixerModesTest$' --output-on-failure > r2/logs/35-test-red.log 2>&1` | build EXIT=0; ctest **EXIT=8**, the same `onScreen(compact)` failure, 0/1 passed |
| 4b: re-apply | `git apply r2/logs/fix.patch` | **EXIT=0** |
| 4b: rebuild and test with the fix | `CCACHE_DISABLE=1 cmake --build build --target zene MixerModesTest MixerZonesTest MixerMeteringTest -j4 > r2/logs/36-build-green.log 2>&1`; `ctest -R '^Mixer(Modes|Zones|Metering)Test$' --output-on-failure > r2/logs/37-test-green.log 2>&1` | build **EXIT=0**; ctest **EXIT=0**, 3/3 passed |
| Whole MixerModesTest binary | `./MixerModesTest > r2/logs/27-modes-verbose.log 2>&1` (from `build/tests`) | exit 0, `Totals: 27 passed, 0 failed` |
| Gates, direct | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > r2/logs/38-feature-check-direct.log 2>&1` | EXIT=1: `PermissionError` on `<worktree>-feature-check/fork-sources.log`. That log folder is outside my write-jail (see below) |
| Gates, log folder redirected | `python3 r2/logs/fc_wrap.py r2/logs/39-feature-check-logs <worktree> > r2/logs/39-feature-check.log 2>&1` | **EXIT=0**, 9 changed files, all 8 rows PASS (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length) |

`fc_wrap.py` runs `feature_check.py` unchanged through `runpy`. It only sends the script's log files to
my log folder; the gates, limits and baselines are untouched. The reviewer's own direct run exited 0,
which agrees with this run. The direct failure comes from my sandbox.

### Mistakes made and fixed along the way (no result here depends on them)
- `r2/logs/01/02`: my first test draft checked only position and passed with the bug present. The crushed
  12 px widgets were still "inside" the panel. I added the `minimumSizeHint` check, and the test then went RED.
- `r2/logs/11`: a wide-panel check failed because my first `unitSize` started from `QSize()` (-1 wide), so
  every unit lost 1 px. I fixed it with `QSize(0, 0)` and did the same in `minimumSize`.
- `r2/logs/21`: my first header split cut the class at `struct Row`'s `};`, so the build failed. I fixed it.
- `r2/logs/30-31` are void: my first 4b attempt exported an empty patch (zsh did not word-split
  `$P`), so nothing was reversed. I redid it with the paths written out (rows 34-37 above).
- `cmake -S . -B build` was refused by the lane guard (`r2/logs/18-cmake.log`). `cmake --build`
  re-generated the build on its own and compiled the new file (`r2/logs/21-build-green.log`).
- ccache cannot write its temp folder in the jail, so every build used `CCACHE_DISABLE=1`.

## Files committed
- `include/MixerModeBarLayout.h`, `src/gui/MixerModeBarLayout.cpp`: new, the wrapping layout
- `include/MixerModeBar.h`, `src/gui/MixerModeBar.cpp`: the bar uses it (`addUnit`, `pushRight`)
- `src/gui/CMakeLists.txt`: builds the new source
- `tests/fork-sources.txt`, `tests/all-sources.txt`: register the two new files, in byte order
- `tests/src/gui/MixerModesTest.cpp`: the regression test
- this report

## Self-check

- **Round 3 (integration):**
  - Reverse-apply 4b gave EXIT=0, rebuild EXIT=0 and ctest **EXIT=8** (`r3/logs/04-test-red.log:24`).
  - Re-apply gave EXIT=0, rebuild EXIT=0 and the Mixer family **EXIT=0** 3/3 (`r3/logs/06-test-green.log`).
  - Gates: the wrapped run gave EXIT=0, 8/8 PASS (`r3/logs/08-feature-check.log`). The direct run gave EXIT=1
    with the write-jail `PermissionError` (`r3/logs/07-feature-check.log`), so submit_check's own gate row
    may show FAIL for that reason only.
  - The integrated tree is identical to the accepted patch, so the points below still hold unchanged.

- **The regression test fails with the product change reversed and passes with it:**
  - Reversed: `git apply -R r2/logs/fix.patch` EXIT=0, rebuild EXIT=0, `ctest -R '^MixerModesTest$'`
    **EXIT=8** with `onScreen(compact)` FALSE (`r2/logs/35-test-red.log:24`).
  - Re-applied: `git apply r2/logs/fix.patch` EXIT=0, rebuild EXIT=0, Mixer family **EXIT=0** 3/3
    (`r2/logs/37-test-green.log`).
  - Before any product edit, it was also RED (`r2/logs/07-test-red.log:24`, EXIT=8).
- **Deterministic:** the test fixes the host's size (`setFixedSize(300, 600)`, then `2400`), calls
  `QLayout::activate()` and processes pending events once. It reads geometry and sends one synthetic
  click (`QTest::mouseClick`). It has no wall-clock waits, sleeps or threads, and `qScopeGuard` restores
  the density.
- **The fix is at the cause and complete:**
  - The cause was the bar's single non-wrapping row, so the row itself was replaced.
  - Every control on the bar goes through the new layout. Its siblings `addSegment` and `addChip`, and
    the Show/Pick/Follow/Matrix items, all use `addUnit`, and the old `addStretch` became `pushRight`.
  - `MixerModeBar.h` is included by `MixerView.cpp`, `MixerModesTest`, `MixerZonesTest` and
    `MixerMeteringTest`. All were rebuilt (`zene` plus the three tests) and pass.
  - The test checks every visible button and combo box on the bar, not only Compact.
- **No collateral damage:**
  - No saved data, command or schema changed; only the bar's placement did.
  - In a wide panel the bar keeps one row with the right group at the right edge (asserted in the test).
  - The object names, accessible names, parents and the `controlCommand` properties are unchanged, so
    `MixerZonesTest` and `MixerMeteringTest` still find the controls (3/3).
  - Nothing runs on the audio thread, and undo and commands are untouched.
  - The bar's minimum width only gets smaller, so it cannot push past a limit that other callers hit.
- **If the verdict is ALREADY-FIXED or FALSE-POSITIVE:** not applicable; the verdict is FIXED. Round 1's
  FALSE-POSITIVE was wrong, because its probe never produced a narrow panel (see "Round 2").
