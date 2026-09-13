# `docs/specs/` — the specifications this repository's code and tests cite

**DOC-5, 2026-09-13.** The change-plan register's row (`BACKLOG.md` § Change-plan register,
Phase 2) reads: *"`SPEC-zene-studio.md`, `AGENT-TOOLING.md`, `AGENT-SURFACE-INVENTORY.md` are
cited but not in the product repo."* The decision taken here is **commit**, not remove, and
this directory is where the bare name in a code comment now resolves:

| file | bytes | sha256 (the pinned snapshot) | cited by |
|---|---|---|---|
| `SPEC-zene-studio.md` | 16,725 | `fabc0ae8bc97e1cd0257f6cd2a2b7201b0291aed69e13d02f9dba626e18474e7` | 9 product source files — A11–A16 are the control-surface contract the registry implements — plus `tests/agent-surface-gate.py` and `tests/agent-surface-negative-control.md` |
| `AGENT-TOOLING.md` | 32,394 | `c71ffea13b7634c4cb8ba9e50664951af76650b9769fb67e69d2866022cb40c0` | 9 product source files — the command-id, schema and closed-error-set conventions |
| `AGENT-SURFACE-INVENTORY.md` | 78,465 | `319c6350edbf2b49b8e24ca6f0c3d5aa8a67ba322214015fc3f24d3b620a049d` | `docs/reports/CMDN-REPORT.md`; the command inventory the groups were built from |
| `SPEC-lua-api-v0.md` | 7,937 | `8ffcec1cd6dd50594d0776776140bcef57238054b3dc94a4f7217de8c4fa6c2c` | `CMakeLists.txt`, `docs/LUA-API-STABILISATION.md`, `docs/LUA-COMPATIBILITY-POLICY.md`, the Lua tests |
| `SPEC-neural-amp.md` | 5,649 | `a5a86245138673a85efc4373c828d5a6ee684be0d08d2f07de6e2c56146dae10` | `plugins/NeuralAmp/CMakeLists.txt`, `plugins/NeuralAmp/tests/rt_alloc_probe.cpp`, its `LICENSE-NOTICE.md` |
| `SPEC-stem-split.md` | 5,689 | `930492bd8551fff0dc4ab9e3bb9e2f3d238830ac6ec4f668b727893438403f87` | `include/StemSeparation/StemTypes.h`, `src/core/ExternalProcessStemSeparator.cpp` |
| `SPEC-wasm-sandbox.md` | 5,285 | `cd073153065a65f8352e7616c5a0b33e96add6b2609e4fc683a9b8b99cd46924` | 7 places: `modules/wasm/*.wat`, `plugins/WasmEffect/WasmEffect.cpp`, `tests/src/wasm/WasmSandboxTest.cpp` |

Each file carries an HTML comment at its head with its source path in the program workspace, the
sha256 of the bytes it was copied from, and the reason that file was chosen. 
## The convention, stated once

- **These are pinned snapshots, not live documents.** The program workspace
  (`~/Documents/AI_KOS_PROJECT/projects/lmms-fl-research`, `ableton-gap/` and `specs/`) is where
  these documents are edited. A snapshot is refreshed by a deliberate commit that updates the
  header hash in the same commit as the copy — never by editing this copy in place while the
  workspace copy says something else.
- **Citations are by bare name.** ~60 source comments and tests say `SPEC-zene-studio.md` or
  `AGENT-TOOLING.md` with no path. Rewriting them to add `docs/specs/` would be churn across code
  this release must not disturb, so the name is the citation and this directory is the resolution
  point: `git grep -F SPEC-zene-studio.md` finds the file here.
- **If the two copies diverge, that is a defect with one fix**: one commit that updates both the
  snapshot and the citation, or a recorded decision to stop citing the document at all.

## What was measured, and what is deliberately still open

A full sweep of the tracked tree (2026-09-13) found **21 names cited by product code
(`src/`, `include/`, `plugins/`, `modules/`, `tests/src/`, `recording/`) that are not in this
repository**. Seven are committed above. Ten more exist in the program workspace and are *not*
committed here, each with the number of code files that cite it:

| cited name | code files | where it lives in the workspace |
|---|---|---|
| `SPEC-stable-ids.md` | 12 | `ableton-gap/` — the stable-id scheme; `AGENT-TOOLING.md` §4 also cites it |
| `SPEC-dynamic-routing.md` | 5 | `mixer/` |
| `PART-D-SIDECHAIN.md` | 3 | workspace root |
| `WASM-SANDBOX.md` | 3 | workspace root |
| `AGENTS.md` | 3 | workspace root — **program-workspace governance**, not a product spec: committing it into the product tree would be a category error |
| `GIT-FRIENDLY-MMPZ.md` | 2 | workspace root |
| `NEURAL-AMP.md` | 2 | workspace root |
| `PATCHER-MVP.md` | 2 | workspace root |
| `RECORDING-PROTOTYPE.md` | 2 | workspace root |
| `A16-STATUS-MEASURED.md` | 1 | `ableton-gap/` |

They are recorded rather than committed because several are program-workspace working documents
(lane designs, a governance file) and folding those into the product repository is a decision
larger than the row DOC-5 names — the same reason `AGENTS.md` is called out above. The rule for
each is the rule for the seven: **commit it as a pinned snapshot, or remove the citation**; the
next session that touches one of these areas should take it, and the count is here so it cannot be
forgotten. Two further findings from the same sweep belong with them:

- `docs/CONTROL-SURFACE-FUZZ.md` is cited **with a `docs/` path** by `src/core/ControlServerSocket.cpp`
  (F1/F2) and `docs/CONTROL-SOCKET-PATH-SAFETY.md`, and no such file exists in the tree. It is a lane
  audit document that lives in the `zene-ctrl-fuzz` worktree, not in this repository; either it is
  committed or those two citations are re-pointed. Named here so the path claim is not silently false.
- `feedback/grade-B-recording.md` (workspace) is cited as bare `grade-B-recording.md` from
  `include/SampleRecordHandle.h` and `docs/RECORDING-REALTIME-FIXES.md`; the name resolves in the
  workspace and reads as a reference to the audit, not a claim about this repository. Recorded, not
  changed.
