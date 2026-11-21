# Cluster new-p11-22-port: frame trim options on MIDI clips

**Verdict: FIXED** (port of `bugs/qwen-new-p11-22` commit `5a62cbb70` onto `050/m0-train-5`, hand-written, not merged or cherry-picked).

Gate note: both ratchet gates exit 1. Every failing item is in a file this change does not touch and is absent from the committed baselines, so the failures predate this change. See "Gates" below.

## Evidence

- **Bug on the newest line (unfixed before this change):** `wanted()` returns false for any non-`SampleClip` at `src/core/ControlCommandsClipFrames.cpp:118`. In the original `clip.trim` handler (`src/core/ControlCommandsClipTrim.cpp:178` at `HEAD`, before this change), that false result sends a MIDI clip down the tick-only path, which ignores `start_frames`, `end_frames` and `zero_crossing` and still answers success. So the finding reproduces; it is not ALREADY-FIXED.
- **Fix:**
  - `src/core/ControlCommandsClipTrim.cpp:139-143`: `frameTrimOptionRequested()` detects the three frame-specific keys.
  - `src/core/ControlCommandsClipTrim.cpp:185-191`: the handler refuses those keys with `ControlErrorKind::InvalidArgs` when the resolved clip is not a `SampleClip`. Sample clips and ordinary MIDI tick trims (no frame keys) keep their existing paths.
  - `src/core/ControlCommandsClipTrim.cpp:60`: `#include "SampleClip.h"`.
- **Regression test:** `tests/src/core/ControlVerbInverseTest.cpp:224-243` (`clipTrimRefusesFrameOptionsOnMidiClips`). It builds an instrument track with a MIDI clip, resolves it with `control::resolveMidiClip`, sends `clip.trim` with a valid `start` plus `start_frames: 12`, and asserts refusal with `InvalidArgs`, unchanged position and unchanged length. The include `#include "MidiClip.h"` was added to the same file.
- **Registration:** `src/core/ControlCommandsClipTrim.cpp` is already in `tests/fork-sources.txt:1338`. No new files were added and no headers changed, so no registration edits were needed.
- **Design choice:** explicit `zero_crossing` (true or false) on a MIDI clip is refused, same as the original fix. Omitting the key is fine. Refusing `zero_crossing: false` is slightly stricter than the finding requires, but it matches the accepted fix and keeps the rule simple: any explicit frame-specific option on a non-sample clip is refused.

## Verification

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-22-port.r1/logs`

ccache note: the first build failed with `ccache: error: failed to create temporary file for /run/user/1000/ccache-tmp/...: Permission denied` (`build-1.log`, EXIT=2). That is a sandbox write-jail issue, not a code error. All later builds set `CCACHE_TEMPDIR=/tmp/ccache-tmp-qwen-p11`.

| Step | Command (run from the worktree unless noted) | Log | Exit | Result |
| --- | --- | --- | ---: | --- |
| Build, pre-fix (test added) | `cmake --build build --target zene ControlVerbInverseTest -j2` | `build-1.log` | 2 | ccache permission error (environment, see above) |
| Build, pre-fix | `CCACHE_TEMPDIR=/tmp/ccache-tmp-qwen-p11 cmake --build build --target zene ControlVerbInverseTest -j2` | `build-2.log` | 0 | built |
| **RED** (pre-fix) | `cd build/tests && ctest -R ControlVerbInverseTest --output-on-failure` | `test-1.log` | **8** | `FAIL! : clipTrimRefusesFrameOptionsOnMidiClips() '!trimmed.ok' returned FALSE`; Totals 9 passed, 1 failed |
| Build, with fix | `CCACHE_TEMPDIR=... cmake --build build --target zene ControlVerbInverseTest -j2` | `build-3.log` | 0 | built |
| **GREEN** (with fix) | `cd build/tests && ctest -R ControlVerbInverseTest --output-on-failure` | `test-2.log` | **0** | 1/1 passed |
| Proof: save fix | `git diff -- src/core/ControlCommandsClipTrim.cpp > logs/fix.patch` | - | 0 | patch saved |
| Proof: remove fix | `git apply -R logs/fix.patch` | - | 0 | product fix removed |
| Proof: rebuild without fix | `CCACHE_TEMPDIR=... cmake --build build --target zene ControlVerbInverseTest -j2` | `build-4.log` | 0 | built |
| **RED** (proof, no fix) | `cd build/tests && ctest -R ControlVerbInverseTest --output-on-failure` | `test-3.log` | **8** | same failing assertion as the first RED: `'!trimmed.ok' returned FALSE`; 9 passed, 1 failed |
| Proof: restore fix | `git apply logs/fix.patch` | - | 0 | fix restored (`git diff --stat`: 2 files changed, 38 insertions, 1 deletion) |
| Proof: rebuild with fix | `CCACHE_TEMPDIR=... cmake --build build --target zene ControlVerbInverseTest -j2` | `build-5.log` | 0 | built |
| **GREEN** (proof, fix restored) | `cd build/tests && ctest -R ControlVerbInverseTest -V --output-on-failure` | `test-4.log` | **0** | `PASS : clipTrimRefusesFrameOptionsOnMidiClips()`; Totals 10 passed, 0 failed; 1/1 CTest passed |

**Affected family** (every test that calls `clip.trim`, and the only test that uses the frame keys besides the new one):

| Step | Command | Log | Exit | Result |
| --- | --- | --- | ---: | --- |
| Build the family | `CCACHE_TEMPDIR=... cmake --build build --target zene ArrangingCatchUpTest LinkedClipEditsTest LinkedClipGroupEditsTest ControlChordCommandsTest ControlVcaLockedEditsTest ControlVerbInverseTest -j2` | `build-6.log` | 0 | built |
| Run the family | `cd build/tests && ctest -R "ArrangingCatchUpTest\|LinkedClipEditsTest\|LinkedClipGroupEditsTest\|ControlChordCommandsTest\|ControlVcaLockedEditsTest\|ControlVerbInverseTest"` | `test-5.log` | 8 | **6 CTest targets run: 5 passed, 1 failed** |
| Failure detail | `cd build/tests && ctest -R ArrangingCatchUpTest --output-on-failure` | `test-6.log` | 8 | ArrangingCatchUpTest: 11 passed, 1 failed |

The one failure is `ArrangingCatchUpTest::queuedInstrumentHandlesAreRemovedBeforeTheirTrackCanDie`: `Actual "dummyinstrument" / Expected "tripleoscillator"`, preceded by `The plugin "tripleoscillator" wasn't found or could not be loaded! Reason: "Plugin not found."`. The test does not call `clip.trim` (its `clip.trim` calls are at `ArrangingCatchUpTest.cpp:200` and `:218`, inside other test functions). I read this as a missing-plugin environment problem in this build. I did not run it on the unmodified base to confirm, so this is unverified. The frame-trim tests in the same file all pass: `aSplitCutsAtAFrameBetweenTwoTicks`, `zeroCrossingSnapMovesTheCutOntoACrossing`, `aFrameCutThatCannotBeIsRefused`, `aTrimMovesTheStartToAFrameAndUndoesWholly`, `anEndFramesTrimWithoutAnEndIsRefused`, `theLeadSavesLoadsAndIsResetWhenAbsent`.

Other families: no header changed, so no further dependents needed rebuilding.

## Gates

| Gate | Command | Log | Exit |
| --- | --- | --- | ---: |
| Complexity | `bash tests/complexity-gate.sh --check` | `gate-1.log` | 1 |
| File length | `bash tests/file-length-gate.sh --check` | `gate-2.log` | 1 |

- Complexity: 6 `REGRESSED` items, all in other files: `XyPadPairTest.cpp dotCentreAlong` (CCN 16), `DawProjectReadTracks.cpp parseNotes` (13), `ControlCommandsMixerTools.cpp adjust` (12), `ControlCommandsMixerStrip.cpp setStrip` (11), `ProjectRevisions.cpp rotateProjectRevision` (11), `ControlCommandsShortcuts.cpp importedOverrides` (11).
- File length: 14 `REGRESSION` items, all new-file-over-500 in other files (for example `src/core/TakeLane.cpp (520)`, `tests/src/core/DeviceDspTest.cpp (752)`).
- Neither log names `ControlCommandsClipTrim.cpp` or `ControlVerbInverseTest.cpp`. None of the failing function or file names appears in `tests/complexity-baseline.tsv`. `git diff --name-only HEAD` lists only the two files changed here. The failing set is independent of this diff.
- I did not change any gate limit, baseline or threshold. I did not fix the other files: that would be other clusters' work and outside this bug.

## Files committed

- `src/core/ControlCommandsClipTrim.cpp`
- `tests/src/core/ControlVerbInverseTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p11-22-port.md`
