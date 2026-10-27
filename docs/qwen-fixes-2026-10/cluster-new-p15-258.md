FIXED

# Cluster new-p15-258 - FLAC compression level applied before the encoder is opened

## Verdict

REAL, now FIXED on `bugs/qwen-new-p15-258` (base `050/m0-train-18`, `7641e9493d`).

## Evidence (original code)

- `src/core/audio/AudioFileFlac.cpp` (before the fix, lines 68-71): `sf_command(m_sf, SFC_SET_COMPRESSION_LEVEL, ...)` ran while `m_sf` was still `nullptr`.
- `src/core/audio/AudioFileFlac.cpp:73` (before the fix): `m_sf = sf_open(...)` came after the compression command, so the setting was issued against a null handle and dropped.
- The scoping brief's line numbers held: the bug is at the pre-fix lines 68-73.

## The fix

`src/core/audio/AudioFileFlac.cpp`:
- The `LMMS_HAVE_SF_COMPLEVEL` block (the compression level read from `getOutputSettings().getCompressionLevel()` and passed to `sf_command(m_sf, SFC_SET_COMPRESSION_LEVEL, ...)`) moved after the successful `sf_open` and its `m_sf == nullptr` early return, now at lines 82-86.
- The feature guard is unchanged; `SFC_SET_CLIPPING` and `sf_set_string` stay in the same order after it.
- Nothing is added to `writeBuffer` or any other audio-thread path. The command runs once, during encoder setup.

## Regression test

`tests/src/core/ExportWavDitherTest.cpp`, new slot `aFlacCompressionLevelReachesTheEncoder()` (line 394), with helpers `richSignal()` (line 114, deterministic LCG, no clock or libc rand) and `writeFlac()` (line 508).

The test writes the same 22050-frame stereo signal through the real `AudioFileFlac::getInst` path at compression level 0.0 and at 1.0, then asserts:
1. the two files differ byte-for-byte (fails before the fix: both use libsndfile's default);
2. both decode to identical samples (the level changes packing, not content).

No sleeps, timing or races. The test is registered already (`tests/CMakeLists.txt:430`, ctest `ExportWavDitherTest`), so no new registration was needed.

`tests/upstream-modifications.txt:895`: appended the new-p15-258 note to the existing `AudioFileFlac.cpp` entry.

## Commands and exit codes

Build environment note: the first build attempts failed in ccache's MOC compile step with `ccache: error: failed to create temporary file ... dsh/home/ccache-tmp-r6/...: Permission denied` (logs `build-1-red.log`, `build-2-red.log`). Build 3 set `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp` for the build command (no tracked config changed) and succeeded; all later builds used the same override.

| Step | Command (run in worktree / build/tests) | Log | Exit |
|---|---|---|---|
| RED build (test only, no fix) | `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp cmake --build build --target ExportWavDitherTest -j4` | build-3-red.log | 0 |
| RED run | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^ExportWavDitherTest$' --output-on-failure` | test-1-red.log | 8 |
| RED assertion | `FAIL! : ...aFlacCompressionLevelReachesTheEncoder() 'fastBytes != bestBytes' returned FALSE` (10 other slots passed) | test-1-red.log | - |
| GREEN build (fix applied) | `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp cmake --build build --target ExportWavDitherTest -j4` | build-4-green.log | 0 |
| GREEN run | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^ExportWavDitherTest$' --output-on-failure` | test-2-green.log | 0 (100% passed, 1/1) |
| 4b save fix | `git diff -- src/core/audio/AudioFileFlac.cpp > fix.patch` | - | 0 |
| 4b revert fix | `git apply -R fix.patch` | - | 0 |
| 4b MUTANT build | `cmake --build build --target ExportWavDitherTest -j4` | build-5-mutant-red.log | 0 |
| 4b MUTANT run | `ctest -R '^ExportWavDitherTest$'` | test-3-mutant-red.log | 8 (same FAIL! assertion) |
| 4b restore fix | `git apply fix.patch` | - | 0 |
| 4b RESTORED build | `cmake --build build --target ExportWavDitherTest -j4` | build-6-restored-green.log | 0 |
| 4b RESTORED run | `ctest -R '^ExportWavDitherTest$'` | test-4-restored-green.log | 0 (1/1 passed) |
| Export family build | `cmake --build build --target ExportDitherTest ExportNoiseShaperTest ExportWavWriteErrorTest -j4` | build-7-family.log | 0 |
| Export family run | `ctest -R 'Export' --output-on-failure` | test-5-family.log | see below |

Export family (`ctest -R Export`): 13 tests matched. The 4 built here passed: ExportDitherTest (#103), ExportNoiseShaperTest (#104), ExportWavDitherTest (#105), ExportWavWriteErrorTest (#106). The other 7 are "Not Run" because their executables are not built in this tree (SurroundExportTest, MidiExportTest, StemExportTest, ControlStemExportTest, ExportQualityControlsTest, ExportChannelsTest, ExportProjectDialogPresetTest). None of them touches the FLAC encoder setup; `SurroundExportTest.cpp:297/301` checks that FLAC surround export is refused.

Gates: `feature_check.py` could not write its own log folder (`PermissionError` on `zene-qwen-new-p15-258-feature-check`, outside the write-jail; log `gates-1.log`, exit 1). I ran the same gate commands directly from the worktree instead:

| Gate | Command | Log | Exit |
|---|---|---|---|
| fork-sources | `bash tests/fork-sources-gate.sh` | gate-fork-sources.log | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | gate-all-sources.log | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | gate-no-upstream.log | 0 |
| check-namespace | `tests/scripted/check-namespace` | gate-check-namespace.log | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | gate-no-tautology.log | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | gate-doc-figures.log | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | gate-complexity.log | 1 (train red by design) |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | gate-file-length.log | 1 (train red by design) |

For the two ratchets, the feature-check rule is "fail only on an item new in a file this branch changed". The changed files are `src/core/audio/AudioFileFlac.cpp`, `tests/src/core/ExportWavDitherTest.cpp` and `tests/upstream-modifications.txt`. No `REGRESSION:` line in either log names any of them (`grep -n "AudioFileFlac\|ExportWavDitherTest"` on both logs: no match). Every reported item is in another file. By that rule both ratchets PASS for this branch, so the overall result is PASS. This is my reading of the rule, not the script's own output.

## Scope and not-done

- Only FLAC export is touched. WAV and the other formats are unchanged.
- Not run: the seven unbuilt Export-family tests (listed above), and a full `ctest` or `tests/run-all-gates.sh`. Not run here because of the shared build queue and the 30 GB disk limit. The FLAC path is not referenced by the unbuilt tests except through the refusal case above.
- Not verified on a real platform: `LMMS_HAVE_SF_COMPLEVEL` was enabled on this build (`CMakeLists.txt:446-451` check). Builds without the feature are unchanged (the block is still compiled out).
