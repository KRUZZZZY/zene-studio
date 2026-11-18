# cluster new-p8-120 — Availability check ignores the configured model path

Verdict: **FIXED**

## Finding

`src/core/ExternalProcessStemSeparator.cpp:239` — `isAvailable()` checked
`StemModelStore::isModelPresent(defaultModelPath(), ...)` while `separate()`
(line 275) checks the instance's `m_model`. An instance configured with a
custom model path therefore reported availability from the class default, not
from the model it actually uses.

## Evidence

- `src/core/ExternalProcessStemSeparator.cpp:239` (before): `isModelPresent(defaultModelPath(), &localError)`
  vs `:275` in `separate()`: `isModelPresent(m_model, &error)`.
- `include/StemSeparation/ExternalProcessStemSeparator.h:69` declared `isAvailable` `static`,
  so it could not see `m_model` at all. The constructor (`src/core/ExternalProcessStemSeparator.cpp:131`)
  resolves `m_model = modelPath.isEmpty() ? defaultModelPath() : modelPath`.

## Fix

Made `isAvailable` a `const` instance method checking `m_model` (header declaration and
definition changed together), and updated the four static call sites to default-constructed
instances — behavior-preserving there, since a default instance's `m_model` is exactly
`defaultModelPath()`:

- `include/StemSeparation/ExternalProcessStemSeparator.h` — `static bool isAvailable(...)` → `bool isAvailable(...) const`
- `src/core/ExternalProcessStemSeparator.cpp` — definition gains `const`, checks `m_model`
- `src/core/ControlStemSupport.cpp:406` — `ExternalProcessStemSeparator().isAvailable(error)`
- `src/gui/StemSplitController.cpp:96` and `:109` — same instance form
- `tests/src/core/StemSplitPipelineTest.cpp:96` (`initTestCase`) — same instance form

Regression test added to the existing test file `tests/src/core/StemSplitPipelineTest.cpp`
(`testAvailabilityHonoursConfiguredModelPath`): with the class default absent but the
instance configured with the present stub model, availability must report available;
with the class default present but the instance's model absent, it must report unavailable.
Both facets fail on the original code and pass after the fix.

## Commands and exit codes (logs in /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-120.r1/logs)

| Step | Command | EXIT | Log |
|---|---|---|---|
| build test target (original code) | `cmake --build build --target StemSplitPipelineTest` | 0 | build-1.log |
| RED regression | `ctest -R StemSplitPipelineTest --output-on-failure` (from build/tests) | 8 | test-1.log |
| rebuild zene (header changed) | `cmake --build build --target zene` | 0 | build-2.log |
| rebuild test target (fixed code) | `cmake --build build --target StemSplitPipelineTest` | 0 | build-3.log |
| GREEN regression | `ctest -R StemSplitPipelineTest --output-on-failure` (from build/tests) | 0 | test-2.log |
| complexity gate | `bash tests/complexity-gate.sh --check` | 0 | gate-1.log |
| file-length gate | `bash tests/file-length-gate.sh --check` | 0 | gate-2.log |
| family run (siblings unbuilt) | `ctest -R Stem --output-on-failure` (from build/tests) | 8 | test-3.log |
| build sibling targets | `cmake --build build --target StemJobManagerTest` / `StemModelStoreTest` / `StemExportTest` | 0/0/0 | build-4/5/6.log |
| family run (all built) | `ctest -R Stem --output-on-failure` (from build/tests) | 0 | test-4.log |

RED assertion recorded in test-1.log:

    Test withModel.isAvailable(&error)' returned FALSE. (configured model present: Model file not found: .../no-such-model.onnx)

GREEN: 1/1 `StemSplitPipelineTest` Passed (test-2.log); family 6/6 Passed (test-4.log).

## Files committed

- include/StemSeparation/ExternalProcessStemSeparator.h
- src/core/ExternalProcessStemSeparator.cpp
- src/core/ControlStemSupport.cpp
- src/gui/StemSplitController.cpp
- tests/src/core/StemSplitPipelineTest.cpp
- docs/qwen-fixes-2026-10/cluster-new-p8-120.md
