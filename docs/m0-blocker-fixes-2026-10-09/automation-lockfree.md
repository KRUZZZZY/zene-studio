# M0 automation lock-free lane report — 2026-10-09

Branch `design/m0-automation-lockfree`, starting at `c405233f8`.

| Item | Verdict | Fix commit | Regression / render comparison | Red exit | Green exit | Gate exits (complexity / file-length / fork-sources / no-upstream) | Design |
|---|---|---|---|---:|---:|---|---|
| 2647 — general `AutomatableModel::valueBuffer()` lock | BLOCKED: a complete render-model registry and coherent lock-free old/new value publication are prerequisites; removing the mutex only from consumers leaves shared-controller, linked-model, and `m_oldValue` races. The existing sole-reader sidechain path is already fixed in the base. | — | Not run; no product change | — | — | 0 / 0 / 0 / 0 | [automation-lockfree.md](../m0-design/automation-lockfree.md) |
| 5528 — `AutomationClip` audio reads lock the clip | BLOCKED: `getTimeMap()` still exposes mutable storage to editor, command, node, transform, and piano-roll writers, including edits without the clip lock. A snapshot from existing clip methods would be stale; the writer API and safe snapshot retirement must land together. | — | Not run; no product change | — | — | 0 / 0 / 0 / 0 | [automation-lockfree.md](../m0-design/automation-lockfree.md) |
| 5388 — render-thread automation recording | BLOCKED: moving `recordValue()` to a main-thread queue delays publication of edits read by the next sample-accurate ramp, so musical timestamps alone do not establish bit-identical rendering. The audio-visible recording bridge and the second `NotePlayHandle::recordValue()` writer also need coverage. | — | Not run; no product change | — | — | 0 / 0 / 0 / 0 | [automation-lockfree.md](../m0-design/automation-lockfree.md) |

The design doc was committed before any product change. This lane made no product-source changes and ran no tests or render comparisons. Required gates were run for both documentation commits; every gate returned exit 0.

## Commits

- `3c3399d1b` — `docs(m0): design lock-free automation paths`
- Report commit — records the three BLOCKED verdicts above.
