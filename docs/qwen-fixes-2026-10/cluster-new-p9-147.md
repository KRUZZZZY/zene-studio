# Cluster new-p9-147

Verdict: FALSE-POSITIVE

## Evidence

In `src/gui/tracks/FadeButton.cpp`, `setState()` and `setMuted()` only store values (lines 73–81). The paint path reads those values through `activeColor()` only while `m_stateTimer` is valid and under `FadeDuration` (lines 128–132 and 183–196). Every paint during that 300 ms fade schedules another update after 20 ms (line 132). `activate()` restarts the timer and calls `update()` (lines 83–88), so a later activation paints with the current state and mute values.

Outside that fade, `paintEvent()` uses `m_holdColor` or `m_inactiveColor` (lines 134–150), independent of state and mute. The header documents the normal and corrupted colors as activation colors (lines 47–55). Therefore, at rest, changing either property does not change the currently displayed color; during the only interval where it can affect the color, the existing fade repaint timer refreshes the button. The reported stale-display condition does not occur.

## Verification

| Check | Command / result |
| --- | --- |
| Regression RED | Not run: finding classified as a false positive; no test was added. |
| Regression GREEN | Not run: no regression test or product change. |
| Reverse-patch proof | Not applicable: no product patch. |
| Affected-family test count | 0; no ctest family was run. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-147.r1/logs/gate-1.log 2>&1; gate_status=$?; printf 'EXIT=%s\n' "$gate_status"` — EXIT=0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-147.r1/logs/gate-2.log 2>&1; gate_status=$?; printf 'EXIT=%s\n' "$gate_status"` — EXIT=0 |

The fff grep tool was unavailable in this session. The gate logs were searched with `rg -ni` for `error`, `FAILED`, and `Assertion`; none matched.

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p9-147.md`
