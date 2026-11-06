FIXED

# Cluster gui-1030-tab-mixer-routing-matrix: Routing Matrix control clipped in a narrow Mixer panel

## Verdict: REAL, now fixed

**Cause.** `MixerModeBar` put every control on one `QHBoxLayout` row (old `src/gui/MixerModeBar.cpp:104`,
`auto* layout = new QHBoxLayout(this);`) and had no way to wrap or overflow. When the Mixer was narrower
than that row, Qt either squeezed every control below the width its text needs or (when a parent kept the
row's minimum width) pushed the end of the row past the window edge. The sweep evidence shows both:
`terra-pilot/buttons/runs/m0-train-24/inv-tab-mixer.jsonl` has "Routing Matrix..." at x=1316 w=115
(right edge 1431, past the ~1390 px window, where "Hide the bottom panels" ends at 1390), and "Pick the
channels to show" (x=810) overlapping the "All Channels" combo (708..837).

Measured on the original code in the new regression test (the failing assertion, log `18-test-4b-red.log`):
at 805 px the filter combo gets 45 of its 129 px. A debug run (`16-debug-old.log`) showed that at 536 px
"Routing Matrix..." gets 28 px, so its text can't be read.

**Fix (at the cause).** `src/gui/MixerModeBar.cpp:100-209`: a small `WrapLayout` (`QLayout` in the
file's anonymous namespace) replaces the `QHBoxLayout` (`:218`). It places controls at their size hint
across a row and wraps the rest onto the next row. It reports `hasHeightForWidth`, so `MixerView`'s
`QVBoxLayout` (`src/gui/MixerView.cpp:112-118`) gives the bar the height its rows need. Its minimum width
is the widest single control (`:126`). The old `addStretch(1)` becomes an expanding `QSpacerItem`
(`:253`) that takes its row's spare width (`place`, `:168`). So a wide bar is still one row with the modes
and "Routing Matrix..." pushed to the right edge, the same as before; the test checks this at the
one-row width. `addSegment`/`addChip` now take `QLayout*` (`include/MixerModeBar.h:87,90`,
`src/gui/MixerModeBar.cpp:268,294`); they only ever called `addWidget`.

**Siblings / callers checked.** The bar's layout is only built in its constructor. Nothing else reaches
into it: `rg "modeBar\(\)->layout|m_modeBar->layout"` over src/include/plugins finds nothing. The headers'
other includers (`MixerView.cpp`, `MixerZonesTest`, `MixerMeteringTest`, `MixerModesTest`) were rebuilt
and pass. No commands, saved data, undo or audio-thread code are touched: this is GUI layout only and
runs on the GUI thread.

## Regression test

`tests/src/gui/MixerModesTest.cpp:272` `theModeBarWrapsInsteadOfClippingWhenNarrow`. It puts a
`MixerModeBar` under a `QVBoxLayout` column, as `MixerView` does, and gives the column ½, ⅓ and 1× the
bar's one-row width. It applies the bar's layout (what its resize event does once shown) and checks that
every visible control: lies inside the width the mixer was given; keeps at least its size-hint width (is not
squeezed); and overlaps no other control. At 1× it checks one row and Routing Matrix... at the right edge.
The test is deterministic: no timers, no waits, no showing a window.

## Evidence (logs in `~/.local/share/zene-bunny-fix/qwen/runs/gui-1030-tab-mixer-routing-matrix.r1/logs/`)

Build env: `CCACHE_DISABLE=1`, because the configured ccache temp dir
(`~/.local/share/zene-bunny-fix/dsh/home/ccache-tmp-r6`) is not writable from this lane (`01-build-red.log`
failed with ccache "Permission denied"; `03-build-red.log` was the same build with ccache off, EXIT=0).

| Step | Command | EXIT |
| --- | --- | --- |
| RED (original code, final test) | `git apply -R logs/fix.patch` | 0 |
| | `cmake --build build --target zene MixerModesTest -j4 > logs/17-build-4b-red.log 2>&1` | 0 |
| | `cd build/tests && ctest -R '^MixerModesTest$' --output-on-failure > logs/18-test-4b-red.log 2>&1` | **8** (FAIL: `mixerModeFilter is squeezed to 45 of its 129 px in a 805-wide mixer`, MixerModesTest.cpp:297) |
| GREEN (fix re-applied) | `git apply logs/fix.patch` | 0 |
| | `cmake --build build --target zene MixerModesTest MixerZonesTest MixerMeteringTest -j4 > logs/19-build-4b-green.log 2>&1` | 0 |
| | `cd build/tests && ctest -R '^Mixer(Modes\|Zones\|Metering)Test$' --output-on-failure > logs/20-test-4b-green.log 2>&1` | 0 (100% tests passed out of 3) |
| Gates | `feature_check.py` (see below) | 1 (crashed after the simple gates; not a gate failure) |
| | `bash tests/complexity-gate.sh --check > logs/23-complexity.log` | 1 (red on the train on purpose; 7 REGRESSION lines, none in my files) |
| | `bash tests/file-length-gate.sh --check > logs/24-file-length.log` | 1 (red on the train on purpose; 26 REGRESSION lines, none in my files) |

`fix.patch` = `git diff -- src/gui/MixerModeBar.cpp include/MixerModeBar.h`.

The test went through two wrong versions before this one (logged honestly). The first RED (`04-test-red.log`)
failed on an overlap only because an unshown bar never lays out its children. After adding an explicit
layout pass, the test passed on the original code too (`10-test-4b-red.log`, `14-test-4b-red.log`, EXIT=0),
because the old layout keeps controls inside the bar and squeezes them instead. Adding the "not squeezed
below its size hint" check gave the true RED above.

### Gates

`python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` exits 1 with `PermissionError`.
It creates its log dir at `<worktree>/../<name>-feature-check`, which is outside this lane's write-jail
(`21-feature-check.log`). So I ran a copy with **only** the log dir and `ROOT` path changed
(`runs/.../feature_check_logs_in_run.py`; `diff` against the original is just those two lines). That copy ran
all six simple gates and they passed:

| Gate | Result (`logs/feature-check/*.log`) |
| --- | --- |
| fork-sources | PASS: every tracked source in scope is registered |
| all-sources-reproduce | REPRODUCES |
| no-upstream-regression | PASS |
| check-namespace | 0 errors |
| no-tautology | PASS |
| doc-figures | PASS |

It then stopped (`22-feature-check.log`, EXIT=1): the base's ratchet listings
(`gate-baselines/af010bc3cc....json`) are not cached, and building them needs `git checkout` in the shared
`gate-baselines/tree`, which bunny-guard refuses in a lane. So I ran the two ratchet gates directly in the
worktree. Neither lists any item in the three changed files (`rg "REGRESSION" ... | rg "MixerModeBar|MixerModesTest"` → no
match). So there is no new complexity or file-length item in my files. The new functions are small
(`rowAt`/`place`/`arrange`, each a few branches), and `MixerModeBar.cpp` is 449 lines (< 500).

No new files, so nothing to register. `src/gui/MixerModeBar.cpp`, `include/MixerModeBar.h` and the test
are fork-new files already in `tests/fork-sources.txt` (lines 992, 2112), so no upstream-modifications
entry is needed.

## Files committed

- `src/gui/MixerModeBar.cpp`
- `include/MixerModeBar.h`
- `tests/src/gui/MixerModesTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-gui-1030-tab-mixer-routing-matrix.md`
