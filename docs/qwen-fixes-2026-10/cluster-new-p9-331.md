# Cluster new-p9-331

**Verdict: NEEDS-CONTEXT** — the finding is real, but a safe pool policy cannot be chosen from the current contract.

## Evidence

| Location | Evidence |
|---|---|
| `plugins/Organic/Organic.cpp:223-285` | The first `playNoteImpl` period allocates an `oscPtr` and left/right `Oscillator` chains with `new`. |
| `plugins/Organic/Organic.cpp:310-318` | `deleteNotePluginData` deletes both oscillator chains and the outer state. `NotePlayHandle::~NotePlayHandle` calls this hook at `src/core/NotePlayHandle.cpp:148-150`; finished-note release in the audio engine calls `NotePlayHandleManager::release` at `src/core/AudioEngine.cpp:470-475`, which invokes the destructor at `src/core/NotePlayHandle.cpp:754`. |
| `include/NotePlayHandle.h:388-389`, `src/core/NotePlayHandle.cpp:741-748,761-775` | Note-handle storage starts at 256 and expands by 16 when exhausted. There is no maximum active-note count or existing per-instrument note-state reservation hook. |
| `tests/CMakeLists.txt:1821-1825`, `tests/src/plugins/PluginPortsHarness.h:974-1015` | The related `PluginPortsMigrationTest` renders and cleans up an Organic note. It has no allocation assertion. No CTest named `Organic` is registered in `build/tests/CTestTestfile.cmake`. |

A fixed Organic state pool would need a capacity and an exhaustion behavior. The note-handle manager has no configured upper bound, so refusing or stealing a note when an Organic pool fills changes current playback behavior. Dynamically extending the pool on demand would retain audio-thread allocation. The missing decision is whether Organic may acquire a documented polyphony limit and what to do at capacity, or whether the core should provide a note-state reservation/reclamation lifecycle that preserves the current dynamically growing note handling.

## Verification record

| Check | Exact command / result |
|---|---|
| RED regression | **Not run.** A first-use allocation probe alone would not resolve the missing pool-exhaustion policy, so no incomplete RED-only test was committed. |
| GREEN regression | **Not run.** There is no new regression to rerun. |
| Revert-fix proof | **Not run.** No production patch was made. |
| Related family | `cmake --build build --target PluginPortsMigrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-331.r1/logs/build-1.log 2>&1; build_status=$?; echo EXIT=$build_status` — **EXIT=0**. |
| Related family | From `build/tests`: `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-331.r1/logs/test-1.log 2>&1; test_status=$?; echo EXIT=$test_status` — **EXIT=0**, 1 CTest target passed. This exercises the Organic note path but does not detect its allocations. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-331.r1/logs/gate-5.log 2>&1; gate_status=$?; echo EXIT=$gate_status` — **EXIT=0**. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-331.r1/logs/gate-6.log 2>&1; gate_status=$?; echo EXIT=$gate_status` — **EXIT=0**. |
| Log scans | The `fff grep` tool/command was unavailable, so `rg -n` was used one term at a time. `rg -n "error" /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-331.r1/logs/build-1.log`, `rg -n "FAILED" /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-331.r1/logs/build-1.log`, `rg -n "Assertion" /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-331.r1/logs/build-1.log`, and those same three searches against `test-1.log` each returned **EXIT=1** (no matches). `rg -n "tests passed" /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-331.r1/logs/test-1.log` — **EXIT=0**, showing `100% tests passed, 0 tests failed out of 1`. `rg -n "Organic" build/tests/CTestTestfile.cmake` — **EXIT=1**, no CTest with that name. |

No RED/GREEN or retraction proof is claimed. The only file to commit for this NEEDS-CONTEXT report is `docs/qwen-fixes-2026-10/cluster-new-p9-331.md`.
