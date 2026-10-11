# Cluster new-p9-371: Loudness reports omit surround channels

**Verdict: FALSE-POSITIVE**

## Evidence

The finding assumes the exported file contains surround planes that the report
does not meter. This worktree's file-render path exports the stereo master:

- `src/core/ProjectRenderer.cpp:166` creates the encoder with
  `DEFAULT_CHANNELS`; line 183 constructs the meter with that same channel
  count.
- `src/core/ProjectRenderer.cpp:272` obtains the engine period, line 279 passes
  that exact `SampleFrame` block to the loudness report, and line 282 passes it
  to the file device.
- `include/SampleFrame.h:189` stores `DEFAULT_CHANNELS` samples per frame.
- `include/HardwareOutputBus.h:39` documents that the engine period stays
  stereo and is what exports read; its extra planar channels are for device
  outputs. The cited `writeSurround` symbol is absent from `src`
  (`rg -n "writeSurround" src` returned exit 1).

So the report meters the frames the file writer receives. There is no
multichannel file render for this reported omission to affect. No product code
or regression test was added.

## Verification

| Check | Command/result |
|---|---|
| Symbol check | `rg -n "writeSurround" src` — exit 1 (no match) |
| Regression RED/GREEN | N/A — step 2 classified the finding as false-positive; regression step skipped. |
| Affected-family test count | N/A — no ctest family was run for a false-positive finding. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-371.r1/logs/gate-3.log 2>&1; gate_exit=$?; printf 'EXIT=%s\n' "$gate_exit" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-371.r1/logs/gate-3.log; exit "$gate_exit"` — exit 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-371.r1/logs/gate-4.log 2>&1; gate_exit=$?; printf 'EXIT=%s\n' "$gate_exit" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-371.r1/logs/gate-4.log; exit "$gate_exit"` — exit 0 |

The `fff` command/tool is unavailable in this environment (`command -v fff`
returned exit 1), so the gate logs were searched with `rg`. The commands
`rg -n 'error' /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-371.r1/logs`,
`rg -n 'FAILED' /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-371.r1/logs`,
and
`rg -n 'Assertion' /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-371.r1/logs`
each returned exit 1 (no matches).

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p9-371.md`
