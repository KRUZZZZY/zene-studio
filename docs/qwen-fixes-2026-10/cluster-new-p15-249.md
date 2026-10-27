FIXED

new-p15-249: saving an emptied articulation map left the old `articulations` attribute on the track element, so a reload restored the removed entries.

Evidence: `ArticulationMap::save` returned early for an empty map without touching the element ([src/core/ArticulationMap.cpp:104-113](src/core/ArticulationMap.cpp), the early return is now lines 107-111 and removes the attribute). The track calls it with its own element at [src/tracks/InstrumentTrack.cpp:1116](src/tracks/InstrumentTrack.cpp). Before the fix, saving a non-empty map, clearing it and saving into the same element left `articulations` on the element; `load()` then read it back (`reloaded.empty()` returned FALSE, [tests/src/gui/ArticulationMapEditorTest.cpp:308](tests/src/gui/ArticulationMapEditorTest.cpp)).

Scope note: I reproduced the element-reuse sequence from the finding. I did not establish a production path that saves a track into an element that already holds the attribute, so user-visible impact in a normal project save is not shown. Confidence in the finding stays low for that reason; the fix makes the element match the map.

Regression: `ArticulationMapEditorTest::anEmptiedMapLeavesNoStaleAttributeOnAReusedElement` ([tests/src/gui/ArticulationMapEditorTest.cpp:294-310](tests/src/gui/ArticulationMapEditorTest.cpp)). It is headless (no GUI objects, no engine), deterministic, and has no timing.

The test sits in ArticulationMapEditorTest.cpp, not ArticulationPlaybackTest.cpp: my first placement pushed ArticulationPlaybackTest.cpp over the 500-line file-length limit (not in `tests/file-length-baseline.tsv`), so I moved it. That file is back to its HEAD content.

## Evidence

| Evidence | Command | Result |
| --- | --- | --- |
| Build with ccache temp dir redirected (see note below) | `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-p15 cmake --build build --target zene ArticulationPlaybackTest ArticulationMapEditorTest -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-249.r1/logs/build-8-green-editor.log 2>&1` | EXIT=0 |
| RED: fix reversed, editor test builds | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-249.r1/logs/fix.patch`; `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-p15 cmake --build build --target zene ArticulationMapEditorTest -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-249.r1/logs/build-9-red-editor.log 2>&1` | EXIT=0 (`git apply -R` EXIT=0) |
| RED test | `cd build/tests && ctest -R '^ArticulationMapEditorTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-249.r1/logs/test-10-red-ctest.log 2>&1` | EXIT=8, `0% tests passed, 1 tests failed out of 1`; `FAIL! ... anEmptiedMapLeavesNoStaleAttributeOnAReusedElement() 'reloaded.empty()' returned FALSE` |
| Fix re-applied | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-249.r1/logs/fix.patch`; rebuild `cmake --build build --target zene ArticulationMapEditorTest -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-249.r1/logs/build-11-green-reapplied.log 2>&1` | EXIT=0 (`git apply` EXIT=0) |
| GREEN test | `cd build/tests && ctest -R '^ArticulationMapEditorTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-249.r1/logs/test-11-green-ctest.log 2>&1` | EXIT=0, `100% tests passed out of 1` |
| Affected family (`-R Articulation`, ctest lists 2 tests) | `cd build/tests && ctest -R 'Articulation' > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-249.r1/logs/test-12-family.log 2>&1` | EXIT=0, `100% tests passed out of 2` |
| Full editor binary | `./ArticulationMapEditorTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-249.r1/logs/test-13-editor-full.log 2>&1` | EXIT=0, 9 PASS lines, 0 FAIL |
| Full playback binary | `./ArticulationPlaybackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-249.r1/logs/test-13-playback-full.log 2>&1` | EXIT=0, 17 PASS lines, 0 FAIL |
| Gate: fork-sources | `bash tests/fork-sources-gate.sh > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-249.r1/logs/gate-20-fork-sources.log 2>&1` | EXIT=0 |
| Gate: all-sources-reproduce | `bash tests/all-sources-reproduce.sh > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-249.r1/logs/gate-21-all-sources.log 2>&1` | EXIT=0 |
| Gate: no-upstream-regression | `bash tests/no-upstream-regression-gate.sh > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-249.r1/logs/gate-22-no-upstream.log 2>&1` | EXIT=0 |
| Gate: check-namespace | `tests/scripted/check-namespace > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-249.r1/logs/gate-23-check-namespace.log 2>&1` | EXIT=0 |
| Gate: no-tautology | `bash tests/no-tautology-gate.sh > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-249.r1/logs/gate-24-no-tautology.log 2>&1` | EXIT=0 |
| Gate: doc-figures | `python3 tests/doc-figures-gate.py > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-249.r1/logs/gate-25-doc-figures.log 2>&1` | EXIT=0 |
| Ratchet: complexity (`--check`) | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-249.r1/logs/gate-26-complexity.log 2>&1` | EXIT=1; no REGRESSION line names `Articulation` (grep count 0) |
| Ratchet: file-length (`--check`) | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-249.r1/logs/gate-27-file-length.log 2>&1` | EXIT=1; no REGRESSION line names `Articulation` (grep count 0) |

`feature_check.py` itself could not run here: it creates `<worktree>/../<name>-feature-check/` for its logs (`feature_check.py:71`), which is outside the write-jail (PermissionError, `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-249.r1/logs/gate-1-feature-check.log`). The table above runs the same six simple gates and two ratchets with the same commands in the worktree. The ratchets' baseline comparison was not run, because it writes `gate-baselines/` under the shared tool directory. The two ratchet exits are red on items in files this branch did not change; I checked the REGRESSION lines by name.

The ccache note: the first build failed with `ccache: error: failed to create temporary file ... ccache-tmp-r6 ... Permission denied` (`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-249.r1/logs/build-1-red.log`), before any of my code compiled. Re-running with `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-p15` (on disk, allowed) succeeded. No compile flags or sources changed.

Flake observed, not caused by this change as far as I can tell: with the fix applied, `ArticulationPlaybackTest::aStackedChordFollowsItsArticulation` failed once (`Actual (sounded.held): -1`, `Expected (0): 0`, ArticulationPlaybackTest.cpp:590, `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-249.r1/logs/test-2-green.log`, ctest EXIT=8). It passed in the other 21 runs of the playback test with the fix applied and in the unfixed run (`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-249.r1/logs/test-1-red.log`). That test calls `play()` for 2000 ms of wall-clock time and reads the dummy instrument's `saveState`; it never calls `ArticulationMap::save`. I did not reproduce it on the unfixed tree beyond that one run, so I have not shown it is pre-existing.

Sibling not changed: `MidiInputTransform::save` ([src/core/MidiInputTransform.cpp:219-222](src/core/MidiInputTransform.cpp)) has the same early return (`if (!active()) { return; }`) without removing its `inputtransform` attribute, so the same stale-attribute shape exists there. It is a different feature and outside this finding, so I left it alone; it needs its own cluster.

Files committed for this verdict: `src/core/ArticulationMap.cpp`, `tests/src/gui/ArticulationMapEditorTest.cpp`, and this report. Both product and test files are already in `tests/fork-sources.txt` and `tests/all-sources.txt`, so no registration changed. No new control command, no undo or realtime path touched.

## Self-check

- Realtime: `ArticulationMap::save` runs on the save path (UI thread), not on the audio thread. The change adds one `removeAttribute` call on the element; nothing allocates, locks or logs on the audio thread.
- Undo: `save` is not an undo step; no command or undo code changed.
- Commands: none added, so `ControlCommandsSnapshot` is untouched.
- Registration: no new files; both edited files are already registered (`tests/fork-sources.txt:1342` for `src/core/ArticulationMap.cpp`, `tests/fork-sources.txt:2690` for `tests/src/gui/ArticulationMapEditorTest.cpp`).
- Old projects: a track with a non-empty map is unchanged. A track with no map gets a `removeAttribute` call on an element that has no such attribute, which is a no-op. `load()` is unchanged.
- The regression fails without the fix (RED, ctest EXIT=8) and passes with it (GREEN, ctest EXIT=0).
