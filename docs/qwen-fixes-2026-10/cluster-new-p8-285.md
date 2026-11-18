# Cluster new-p8-285

Verdict: **FALSE-POSITIVE**

## Evidence

The finding describes `end < start` as an invalid range that reaches `trimWav()` after a full render. The current API explicitly defines `end <= start` as a whole-track request in `include/BounceInPlace.h:65-73`. Accordingly, `src/core/BounceInPlace.cpp:346-359` makes `trimmed` false for that input, skips `writeRange()` and `trimWav()`, and renders the whole track. It cannot reach the negative-frame-count refusal described by the finding.

Explicit range arguments on the per-track command paths are rejected earlier: `src/core/ControlBounceSupport.cpp:67-85` returns an error when `end <= start` or `end > MaxSongLength`; the bounce, freeze, and flatten handlers parse the range before calling `BounceInPlace` (`src/core/ControlCommandsBounceLand.cpp:63-69`, `src/core/ControlCommandsFreeze.cpp:135-143,175-186`, and `src/core/ControlCommandsTrackFlatten.cpp:79-90`). The selection-bounce path receives an `ArrangerRange`; `ArrangerRange::set()` normalizes endpoint order (`src/core/ArrangementEdits.cpp:445-450`), and `range.bounce` refuses an inactive range before rendering (`src/core/ControlCommandsRangeBounce.cpp:92-101`; `include/ArrangementEdits.h:191`).

The proposed change would contradict the public whole-track sentinel contract. No production behavior was changed, and no regression test was added for the reported scenario.

## Validation record

| Check | Command | Exit | Result |
| --- | --- | ---: | --- |
| RED regression on original code | Not run: the scenario is covered by the existing whole-track contract and command validation | N/A | No valid regression can assert the finding without changing that contract |
| GREEN regression after fix | Not run: no fix was applicable | N/A | — |
| Affected-family tests | No `ctest` command run | N/A | 0 tests executed |
| Complexity gate | `bash tests/complexity-gate.sh --check` (output: `gate-complexity-1.log`) | 0 | Passed |
| File-length gate | `bash tests/file-length-gate.sh --check` (output: `gate-filelength-1.log`) | 0 | Passed |

The requested `fff` log-search tool was not available in this session. Log searches used `rg` instead; `error`, `FAILED`, and `Assertion` had no matches in the gate logs (each search exited 1, which means no matches).

Relevant inspection commands and exit codes:

```text
rg -n "BounceInPlace::renderTrack|BounceInPlace::renderTracks" src include  -> 0
nl -ba include/BounceInPlace.h | sed -n '60,82p'; nl -ba src/core/BounceInPlace.cpp | sed -n '188,210p;330,365p;374,386p'; nl -ba src/core/ControlBounceSupport.cpp | sed -n '61,95p'  -> 0
nl -ba include/ArrangementEdits.h | sed -n '182,197p'; nl -ba src/core/ControlCommandsRangeBounce.cpp | sed -n '90,103p'; nl -ba src/core/ControlCommandsRange.cpp | sed -n '137,150p'; nl -ba src/core/ArrangementEdits.cpp | sed -n '445,460p'  -> 0
nl -ba src/core/ControlCommandsBounceLand.cpp | sed -n '58,70p'; nl -ba src/core/ControlCommandsFreeze.cpp | sed -n '135,144p;175,186p'; nl -ba src/core/ControlCommandsTrackFlatten.cpp | sed -n '77,91p'  -> 0
```

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p8-285.md`
