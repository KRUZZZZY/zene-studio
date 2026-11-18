# Cluster new-p11-7

**Verdict: FALSE-POSITIVE**

## Finding and evidence

The finding points to `src/gui/clips/ClipView.cpp:1393-1396`. When edge snapping is enabled, `draggedClipPos()` passes the already grid-snapped `newPos.getTicks()` as `proposed` to `snapToEdgesHit()`, then assigns `edge.start` even for `Kind::None`.

That assignment does not move the clip on a miss in the current implementation. `src/core/ArrangementEdits.cpp:383-408` initializes the returned `SnapHit` as `{proposed, proposed, Kind::None, ...}` and only changes it when a valid target is within range. The contract is also stated in `include/ArrangementEdits.h:155-157,172-173`: without a nearby target, the proposed start is unchanged. Thus the empty hit's `start` is the current grid-snapped position, not tick 0. No production or test change is warranted for this finding.

## Verification evidence

| Check | Exact command | Result |
|---|---|---|
| RED regression | N/A; skipped because source inspection disproves the finding, so no regression was added. | N/A |
| GREEN regression | N/A; no fix or regression test was needed. | N/A |
| Affected-family tests | Not run; false-positive verdict, no affected behavior or test change. | N/A |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-7.r1/logs/gate-complexity-2.log 2>&1; code=$?; printf 'EXIT=%s\n' "$code"` | `EXIT=0` |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-7.r1/logs/gate-file-length-1.log 2>&1; code=$?; printf 'EXIT=%s\n' "$code"` | `EXIT=0` |

The first complexity-gate invocation used `gate-complexity-1.log`, but its wrapper returned without a captured exit marker. It was not used as evidence; the command above was rerun to completion and returned `EXIT=0`.

The requested `fff grep` tool was not available in this session. The completed gate logs were searched with `rg -n -i` for each of `error`, `FAILED`, and `Assertion`; all six searches returned exit code 1 (no matches).

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p11-7.md`

Commit message: `Document cluster new-p11-7 false positive`
