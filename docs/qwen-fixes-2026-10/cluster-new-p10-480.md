# Cluster new-p10-480

**Verdict: FALSE-POSITIVE**

The reported save/reopen failure does not occur in this tree. `Articulation.value` defaults to `0` ([include/ArticulationMap.h:47](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-480/include/ArticulationMap.h:47)). For an absent JSON field, `midiNumber()` temporarily assigns `-1` ([src/core/ArticulationMap.cpp:74](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-480/src/core/ArticulationMap.cpp:74)) and accepts the absence ([src/core/ArticulationMap.cpp:75](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-480/src/core/ArticulationMap.cpp:75)), but `fromJson()` clamps that value back to `0` before storing the entry ([src/core/ArticulationMap.cpp:98](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-480/src/core/ArticulationMap.cpp:98)). When `controller` is present, `toJson()` therefore writes `value: 0` ([src/core/ArticulationMap.cpp:58](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-480/src/core/ArticulationMap.cpp:58), [line 61](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-480/src/core/ArticulationMap.cpp:61)), which the next load accepts. No product or test files were changed.

## Verification evidence

| Check | Exact command | Result |
|---|---|---|
| RED | Not run: the alleged failure path is disproved by the current implementation; no product fix was made. | N/A |
| GREEN | Not run: without a fix there is no valid red/green regression pair. | N/A |
| Build | `cmake --build build --target NotePlaybackTest` | EXIT=0; output in `build-1.log` |
| Articulation-family CTest | `ctest -R NotePlaybackTest --output-on-failure` (from `build/tests`) | EXIT=0; 1/1 CTest tests passed; output in `test-1.log` |
| Complexity gate | `bash tests/complexity-gate.sh --check` | EXIT=0; output in `gate-3.log` |
| File-length gate | `bash tests/file-length-gate.sh --check` | EXIT=0; output in `gate-4.log` |

The `fff` grep tool was unavailable in this session. The build, test, and gate logs were searched individually with `rg` for `error`, `FAILED`, and `Assertion`; none of those terms matched. The CTest summary was checked for `passed` and reported `100% tests passed, 0 tests failed out of 1`.

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p10-480.md` (this report)
