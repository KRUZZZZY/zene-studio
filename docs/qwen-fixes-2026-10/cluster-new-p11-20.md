# Cluster new-p11-20

**Verdict: FALSE-POSITIVE**

The finding says `detach(copy)` does not compile because `detach` requires two arguments. The translation unit includes `ControlProjectTransplant.h` at `src/core/ControlCommandsImportTracks.cpp:46`. That header declares `detach(QDomElement element, bool keepFolders = false)` at `include/ControlProjectTransplant.h:42`, so the call at `src/core/ControlCommandsImportTracks.cpp:164` is valid and uses the intended `false` value. The definition at line 82 matches that declaration. No production or test change is warranted.

## Evidence

| Item | Evidence / result |
|---|---|
| Declaration | `include/ControlProjectTransplant.h:42` declares the second parameter with default `false`. |
| Translation unit | `src/core/ControlCommandsImportTracks.cpp:46` includes that declaration before the helper definition and call. |
| Definition and call | `src/core/ControlCommandsImportTracks.cpp:82` defines the matching two-parameter function; line 164 calls it with the default. |
| RED regression | N/A; the finding is false, so the procedure's false-positive branch skips adding/running a regression. Exit: N/A. |
| GREEN regression | N/A; no fix was applied. Exit: N/A. |
| Affected-family tests | N/A; 0 tests executed because the finding is false. |
| Reverse-fix proof | N/A; there is no product fix to remove/reapply. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-20.r1/logs/complexity-gate-1.log 2>&1; rc=$?; echo EXIT=$rc` — gate exit 0. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-20.r1/logs/file-length-gate-1.log 2>&1; rc=$?; echo EXIT=$rc` — gate exit 0. |
| Committed file | `docs/qwen-fixes-2026-10/cluster-new-p11-20.md` (this report only). |

No build or ctest was run: step 2 classifies the finding as false-positive and directs the work to the report step. The required fff grep tool/command was unavailable in this environment; the gate logs were searched with these fallback commands, each returning 1 for no matches:

```text
rg -n -i error /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-20.r1/logs
rg -n FAILED /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-20.r1/logs
rg -n Assertion /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-20.r1/logs
```

The source evidence searches returned exit 0:

```text
rg -n 'void detach\(QDomElement element, bool keepFolders = false\)' include/ControlProjectTransplant.h
rg -n 'ControlProjectTransplant.h' src/core/ControlCommandsImportTracks.cpp
rg -n 'void detach\(QDomElement element, bool keepFolders\)|detach\(copy\)' src/core/ControlCommandsImportTracks.cpp
```
