# Cluster new-p9-109

**Verdict: NEEDS-CONTEXT**

The zero-capacity arithmetic is present in `include/Delay.h:84-94` for `CombFeedback` and `include/Delay.h:307-317` for `AllpassDelay`: both assign `m_size = maxDelay` and then evaluate `m_position %= m_size` without rejecting zero.

I could not establish a valid caller or run the required runtime regression. A direct test include fails to compile because the inherited template constructors name nonexistent fields: `m_feedBack` in `CombFeedback` (`include/Delay.h:69-77`) and `AllpassDelay` (`:292-300`), along with other invalid initializers in the same header. The compiler reports these errors when building the test target. A source search finds only the type aliases in this header, with no project include or instantiation. The suggested `MonoDelay`, `StereoDelay`, and `DelayControls` names are reference plugin sources in `tests/CMakeLists.txt:1509-1510`, not registered ctest cases.

The behavior should be guarded if these templates are meant to be usable. Making them testable here would first require resolving the existing constructor errors or adding a new test seam, either of which expands this one-bug change. No production or test source changes are committed pending that scope decision.

## Evidence

| Check | Exact command | Result |
|---|---|---|
| Test-target build attempt (log `build-1.log`) | `cmake --build build --target zene SampleBufferResampleTest` | EXIT=2; compilation stops on invalid `Delay.h` member initializers before ctest can run. |
| Test-target build retry (log `build-2.log`) | `cmake --build build --target zene SampleBufferResampleTest` | EXIT=2; same header errors. |
| Standalone app build (log `build-3.log`) | `cmake --build build --target zene` | EXIT=0. |
| Runtime regression / GREEN | No ctest command was run; the test target did not build. | 0 affected-family tests executed; no RED or GREEN runtime result. |
| Complexity gate (log `complexity-1.log`) | `bash tests/complexity-gate.sh --check` | EXIT=0. |
| File-length gate (log `file-length-1.log`) | `bash tests/file-length-gate.sh --check` | EXIT=0. |

Build and gate logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-109.r1/logs/`.

**Files committed:** `docs/qwen-fixes-2026-10/cluster-new-p9-109.md` only.
