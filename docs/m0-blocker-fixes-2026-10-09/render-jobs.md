# M0 blocker fixes — render jobs

| Cluster | Verdict | Commits | Test, red and green exits | Gate exits | Design |
|---|---|---|---|---|---|
| 917 | FIXED | Design: `b73033a8c`; fix: `f6498a18f` | Red: with only `render.queue_bounce` registration disabled, `ControlRenderJobTest` failed across all five async cases (ctest exit 8). Green: build `zene ControlRenderJobTest ControlBounceRenderTest TrackRenderMenuTest` exit 0; `ControlRenderJobTest`, `ControlBounceRenderTest`, and `TrackRenderMenuTest` each exit 0. | complexity 0; file-length 0; fork-sources 0; no-upstream-regression 0 | [render-jobs design](../m0-design/render-jobs.md) |

The live-instance commands snapshot was regenerated with 823 commands across 76 groups; descriptions were retained. Verification used Qt 6.4.2. Qt 5.15 was not available in this environment, so a Qt 5 build was not run.
