# M0 blocker fixes — render jobs

| Cluster | Verdict | Commits | Test, red and green exits | Gate exits | Design |
|---|---|---|---|---|---|
| 917 | FIXED | Design: `b73033a8c`; fix: `f6498a18f` | Red: with only `render.queue_bounce` registration disabled, `ControlRenderJobTest` failed across all five async cases (ctest exit 8). Green: build `zene ControlRenderJobTest ControlBounceRenderTest TrackRenderMenuTest` exit 0; `ControlRenderJobTest`, `ControlBounceRenderTest`, and `TrackRenderMenuTest` each exit 0. | complexity 0; file-length 0; fork-sources 0; no-upstream-regression 0 | [render-jobs design](../m0-design/render-jobs.md) |

The live-instance commands snapshot was regenerated with 823 commands across 76 groups; descriptions were retained. Verification used Qt 6.4.2. Qt 5.15 was not available in this environment, so a Qt 5 build was not run.

## Round 2

| Review item | Outcome | Evidence |
|---|---|---|
| Stale project/session target | FIXED | `Song::documentIdentity()` is captured when the job is queued and checked before output installation. Reloading the same project with the same track id during a job fails without changing the replacement project or destination; covered by `ControlRenderJobTest`. |
| One undo step at the 100-step cap | FIXED | Completion merges checkpoints with the captured `ProjectJournal::topStepSerial()` via `mergeCheckpointsSince()`. The capped multi-track range-bounce test verifies one retained step and that one undo restores source mutes and removes the landed track. |
| Output-only bounce reversibility | FIXED | `render.queue_bounce` accepts project-edit operations only. Output-only track bounce goes through `render.queue_add` (`not_mutating`); the regression checks the registry class and unchanged undo depth. |
| Command-count canary | FIXED | The live snapshot and README agree on 823 ids / 76 groups. `ControlCommandsSnapshot` and `python3 tests/doc-figures-gate.py` pass. |
| Legacy synchronous bounce/freeze/flatten/range replies | LIMITATION DOCUMENTED | `ControlRegistry::invoke()` returns a `ControlResult` synchronously, and `ControlServer::dispatchLine()` immediately serializes it; there is no deferred reply callback or retained reply sink. `mastering.run` uses a changed ACK-and-poll contract. The legacy child wait remains plain and adds no `processEvents()` or nested event loop, so a call dispatched on the GUI thread still blocks it. Use the render queue for responsive UI; a later reply requires transport support. |
| Cancellation cleanup | FIXED | Both task-cancel and agent-cancel tests wait for the fake child PID, cancel, then assert the child is gone and the per-job scratch directory was removed. The task-cancel case also verifies the prior destination bytes and project state remain intact. |
| Round-two verification | PASS | Initial red `ControlRenderJobTest` exit 8 exposed the cap-undo, project-identity, and old output-route failures. Final build exit 0; `ControlRenderJobTest` 1/1 and compatibility/snapshot tests 3/3 pass. Complexity, file-length, fork-sources, no-upstream-regression, doc-figures, and `git diff --check` all exit 0. |

Round-two implementation: `5e7b5d037` (`fix(render-jobs): close round 2 review gaps (cluster 917)`). The design details are in [render-jobs design](../m0-design/render-jobs.md).
