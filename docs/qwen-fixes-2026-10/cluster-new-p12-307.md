# Cluster new-p12-307

Verdict: **FALSE-POSITIVE**

The drag handler can call `range.set` with `start > end`, but that does not reject or
deactivate the selection. `setRange` forwards both ticks to `ArrangerRange::set`
([ControlCommandsRange.cpp:147](../../src/core/ControlCommandsRange.cpp#L147)), which
stores the lesser tick as `m_start` and the greater as `m_end`
([ArrangementEdits.cpp:445](../../src/core/ArrangementEdits.cpp#L445)). The range is
active whenever `m_end > m_start` ([ArrangementEdits.h:191](../../include/ArrangementEdits.h#L191)).
A backwards drag therefore produces the same valid selection as a forward drag. The
reported invalid-range behavior is not present, so no production change or regression
test was made.

| Evidence | Command / result |
| --- | --- |
| RED regression | N/A — finding is false-positive; no failing behavior exists to regress. |
| GREEN regression | N/A — no regression test was added. |
| ArrangerTimelineTest affected-family count | 0 tests run; test family not run for the false-positive. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-307.r1/logs/gate-3.log 2>&1; code=$?; echo EXIT=$code; exit $code` — EXIT=0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-307.r1/logs/gate-4.log 2>&1; code=$?; echo EXIT=$code; exit $code` — EXIT=0 |
| Files committed | `docs/qwen-fixes-2026-10/cluster-new-p12-307.md` |

Source inspection commands (both EXIT=0):

```sh
rg -n -C 5 'ControlResult setRange|ArrangerRange::instance\(\)\.set' src/core/ControlCommandsRange.cpp
rg -n -C 5 'void ArrangerRange::set|m_start = std::max|m_end = std::max|bool active\(' src/core/ArrangementEdits.cpp include/ArrangementEdits.h
```
