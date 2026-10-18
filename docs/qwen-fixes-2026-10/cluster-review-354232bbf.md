# Cluster review-354232bbf

## Verdict: FIXED

MIDI export now creates its temporary output exclusively (no-clobber) and propagates failures, so a
colliding file is never overwritten and the previous export bytes survive a failed replacement. Atomic
QSaveFile replacement is retained.

## Evidence

### Bug
`plugins/MidiExport/MidiExport.cpp` opened the export path with `QFile::open(QIODevice::WriteOnly)`,
truncating any existing file, and `Song::exportProjectMidi` discarded the result. The handler in
`src/core/ControlCommandsProjectLifecycle.cpp` then treated any existing file as fresh output.

### Fix
- `plugins/MidiExport/MidiExport.cpp`: open with `QIODevice::WriteOnly | QIODevice::NewOnly` (exclusive
  create) and propagate open/write failures.
- `include/Song.h` / `src/core/Song.cpp`: `exportProjectMidi` returns `bool`.
- `src/core/ControlCommandsProjectLifecycle.cpp`: use the export result, refuse on failure, preserve
  the target, remove only an owned temp file, retain `controlReplaceFileAtomically` (QSaveFile).

### Regression
`tests/src/core/MidiExportTest.cpp::collidingPathIsNotClobberedByExport` creates a file at the export
path, runs the export, and asserts the pre-existing bytes survive.

### RED
`git checkout -- include/Song.h plugins/MidiExport/MidiExport.cpp src/core/ControlCommandsProjectLifecycle.cpp src/core/Song.cpp`
`cmake --build build --target midiexport MidiExportTest -j2` — EXIT=0
`ctest -R MidiExportTest` (from build/tests) — EXIT=8 (collidingPathIsNotClobberedByExport FAILED)

### GREEN
`git apply fix.patch`
`cmake --build build --target zene midiexport MidiExportTest -j2` — EXIT=0
`ctest -R MidiExportTest` (from build/tests) — EXIT=0 (1/1 passed)

### Family tests
`ctest -R "MidiExportTest|ControlSuccessPathsTest"` — EXIT=0 (2/2 passed)

### Gates
`bash tests/complexity-gate.sh --check` — EXIT=1 (pre-existing ratchet regressions; no new regression
from changed functions)
`bash tests/file-length-gate.sh --check` — EXIT=1 (pre-existing ratchet regressions; no new regression
from changed files)

### Files committed
- include/Song.h
- plugins/MidiExport/MidiExport.cpp
- src/core/ControlCommandsProjectLifecycle.cpp
- src/core/Song.cpp
- tests/src/core/MidiExportTest.cpp
- tests/upstream-modifications.txt
- docs/qwen-fixes-2026-10/cluster-review-354232bbf.md
