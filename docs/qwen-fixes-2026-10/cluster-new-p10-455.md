# Cluster new-p10-455: Watsyn waveform loading

**Verdict: FALSE-POSITIVE**

The “Load waveform” button does open a chooser and import a sample. The path is delegated to the graph model:

- [Watsyn.cpp](../../plugins/Watsyn/Watsyn.cpp#L1194) `WatsynView::loadClicked()` calls `setWaveToUser()` on the selected oscillator's graph model at lines 1200, 1204, 1208, or 1212. The button tooltip at line 864 describes loading a sample file.
- [Graph.cpp](../../src/gui/widgets/Graph.cpp#L586) `graphModel::setWaveToUser()` calls `FileDialog::openWaveformFile()` (line 588), decodes a selected file through `SampleBuffer::fromFile()` (line 591), and fills the graph samples using `Oscillator::userWaveSample()` (line 594).
- [FileDialog.cpp](../../src/gui/modals/FileDialog.cpp#L193) `FileDialog::openWaveformFile()` delegates to the audio-file chooser.

The finding mistook the delegation for a missing import. No product change or regression test is appropriate.

## Verification record

| Evidence | Exact command | Exit | Result |
|---|---|---:|---|
| Watsyn handler and tooltip | `rg -n "void WatsynView::loadClicked\|setWaveToUser\|setModified\|Load a waveform from a sample file" plugins/Watsyn/Watsyn.cpp` | 0 | Selected graph calls the import method. |
| Import implementation | `rg -n "QString graphModel::setWaveToUser\|openWaveformFile\|SampleBuffer::fromFile\|Oscillator::userWaveSample" src/gui/widgets/Graph.cpp src/gui/modals/FileDialog.cpp` | 0 | Chooser, file decoding, and sample transfer are present. |
| Test registration | `rg -n "add_test\(NAME Watsyn\|add_test\(Watsyn\|add_test\(NAME PluginPortsMigrationTest\|add_test\(PluginPortsMigrationTest" tests/CMakeLists.txt build/tests/CTestTestfile.cmake` | 0 | `PluginPortsMigrationTest` is registered; there is no standalone Watsyn test. |
| RED regression | Not run | N/A | Skipped because the finding is a false positive. |
| GREEN regression | Not run | N/A | No production fix was made. |
| Affected-family tests | Not run; 0 executed | N/A | No test run was needed for this false-positive finding. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-455.r1/logs/complexity-gate-1.log 2>&1; result=$?; printf 'EXIT=%d\n' "$result"; exit "$result"` | 0 | Passed. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-455.r1/logs/file-length-gate-1.log 2>&1; result=$?; printf 'EXIT=%d\n' "$result"; exit "$result"` | 0 | Passed. |

The enabled tools did not include the requested `fff grep` tool (`command -v fff` also returned exit 1), so the gate logs were checked with `rg` for `error`, `FAILED`, and `Assertion`; all six searches returned exit 1 with no matches.

## Committed files

- `docs/qwen-fixes-2026-10/cluster-new-p10-455.md`
