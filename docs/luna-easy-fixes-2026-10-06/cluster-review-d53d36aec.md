# Cluster review d53d36aec

- Verdict: **FIXED**. The pinned finding was present at the starting revision.
- Source: [ControlScaleSupport.cpp](../../src/core/ControlScaleSupport.cpp#L78) now rejects non-finite and out-of-window values before conversion.
- Regression: [ControlNoteScaleVerbsTest.cpp](../../tests/src/core/ControlNoteScaleVerbsTest.cpp#L503) covers `1e300`, valid neighboring degrees, and an out-of-`int`-range conversion.
- RED: `cmake --build build --target zene ControlNoteScaleVerbsTest` EXIT=0; `ctest -R '^ControlNoteScaleVerbsTest$' --output-on-failure` from `build/tests` EXIT=8 (1/1 failed; 14 Qt cases passed, regression observed `FE_INVALID`).
- GREEN: same build EXIT=0; same ctest EXIT=0 (1/1 passed); direct `./ControlNoteScaleVerbsTest` from `build/tests` EXIT=0 (15 passed, 0 failed).
- Gates: `bash tests/complexity-gate.sh --check` EXIT=0; `bash tests/file-length-gate.sh --check` EXIT=0.
- Evidence transcript: [cluster-review-d53d36aec-transcript.md](cluster-review-d53d36aec-transcript.md).
- Unverified: the `FE_INVALID` assertion is limited to Linux x86-64; other platforms and the full suite were not run.
