# Cluster new-p9-529-port

**Verdict: FIXED** (port of `c31e02811` onto `050/m0-train-5` lineage; the bug was live on this branch)

## Evidence

`plugins/Xpressive/Xpressive.cpp:171-196` (before this change) decoded each `W1sample`/`W2sample`/`W3sample` attribute with `base64::decode` and then, without checking the decoded byte count, looped over `graph.length()` floats (`Xpressive.cpp:176-182`) and called `graph.setSamples()` (`Xpressive.cpp:183`). `base64::decode<char>` (`include/base64.h:44-56`) returns the raw byte count, so a 4-byte or empty payload gave a buffer far smaller than the `GRAPH_LENGTH` floats read. `graphModel::setSamples` (`src/gui/widgets/Graph.cpp:524-529`) copies `length()` floats from the pointer.

The fix (`plugins/Xpressive/Xpressive.cpp:171-196`) calls `setSamples` only when `size == graph.length() * sizeof(float)`. Missing or truncated payloads leave the graph's current samples in place. Valid payloads keep the existing non-finite scrub, so this is behaviour-preserving for well-formed projects.

Regression: `tests/src/plugins/PluginPortsMigrationTest.cpp` slot `xpressiveTruncatedWaveSamplesAreRejected` (defined after `xpressiveLoadedWaveSamplesAreFinite`). For each of `W1sample`, `W2sample`, `W3sample` it restores (a) a one-float payload and (b) a removed attribute, then checks that the saved attribute equals the original saved attribute.

## Commands and exit codes

| Step | Command (log under `.../new-p9-529-port.r1/logs/`) | EXIT |
| --- | --- | ---: |
| Build, first try | `cmake --build build --target zene PluginPortsMigrationTest -j2 > build-1.log 2>&1` | 2 |
| Build, retry | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p9529 cmake --build build --target zene PluginPortsMigrationTest -j2 > build-2.log 2>&1` | 0 |
| RED (fix absent) | `cd build/tests && ctest -R '^PluginPortsMigrationTest$' --output-on-failure > test-red-1.log 2>&1` | 8 |
| Build with fix | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p9529 cmake --build build --target zene PluginPortsMigrationTest -j2 > build-3.log 2>&1` | 0 |
| GREEN | `cd build/tests && ctest -R '^PluginPortsMigrationTest$' --verbose --output-on-failure > test-green-1.log 2>&1` | 8 (see note) |
| Reversal patch | `git diff -- plugins/Xpressive/Xpressive.cpp > fix.patch && git apply -R fix.patch` | 0 |
| Build, reversed | `... cmake --build build ... > build-4.log 2>&1` | 0 |
| Reversed run | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > test-reversed-1.log 2>&1` | 8 |
| Reapply | `git apply fix.patch` | 0 |
| Build, reapplied | `... cmake --build build ... > build-5.log 2>&1` | 0 |
| Reapplied run | `ctest -R '^PluginPortsMigrationTest$' --verbose --output-on-failure > test-reapplied-1.log 2>&1` | 8 (see note) |
| Complexity gate | `bash tests/complexity-gate.sh --check > gate-1.log 2>&1` | 1 (see note) |
| File-length gate | `bash tests/file-length-gate.sh --check > gate-2.log 2>&1` | 1 (see note) |

### RED assertion (fix absent)

```
FAIL!  : PluginPortsMigrationTest::xpressiveTruncatedWaveSamplesAreRejected() Compared values are not the same
   Actual   (savedSample(attribute)): "AABAP2p/AADICqUPan8AAEEAQQBEAEEAUQEAAAAA..."   (heap bytes past the 4-byte payload)
   Expected (settings.attribute(attribute)): "AAAAAAAAAAAAAAAAAAAAAAAAAAAA..."
   Loc: [../../tests/src/plugins/PluginPortsMigrationTest.cpp(1080)]
Totals: 44 passed, 2 failed, 2 skipped
```

Reversal (`test-reversed-1.log`) reports the same `xpressiveTruncatedWaveSamplesAreRejected` failure. With the fix reapplied (`test-reapplied-1.log`) it passes, and the family's totals are `45 passed, 1 failed, 2 skipped`.

### GREEN and the single remaining failure

With the fix, `xpressiveTruncatedWaveSamplesAreRejected` passes. The target still exits 8 because of `nonRealtimeLadspaEffectsAreInvalid` (`'description != nullptr'`, "SWH Decimator test fixture was not loaded"). That failure is an environment issue on this machine: the SWH LADSPA fixture is not loaded in this build. It appears identically in the RED run (`test-red-1.log`) and in the GREEN run, so it is independent of this change. It is not caused by this diff and was not fixed here.

Affected family: `PluginPortsMigrationTest`, 48 test cases executed (45 pass, 1 environment failure, 2 skipped). The Xpressive `ctest -R '^Xpressive$'` target does not exist in this build tree, and `ctest -N -R 'Xpressive|ExprSynth'` lists only `ExprSynth` and `ExprSynthAllocation`, which are `Not Run` because their binaries are not built in this tree (`list-1.log`, `test-family-1.log`). No ExprSynth file was changed, so they were not built for this fix.

### Gates

Both gates exit 1. Every flagged item is in a file this diff does not touch:

- complexity (`gate-1.log`, 6 `REGRESSION` lines): `tests/src/gui/XyPadPairTest.cpp`, `src/core/DawProjectReadTracks.cpp`, `src/core/ControlCommandsMixerTools.cpp`, `src/core/ControlCommandsMixerStrip.cpp`, `src/core/ProjectRevisions.cpp`, `src/gui/ControlCommandsShortcuts.cpp`.
- file length (`gate-2.log`, 14 `REGRESSION` lines): e.g. `src/core/TakeLane.cpp (520)`, `tests/src/core/DeviceDspTest.cpp (752)`, `tests/src/gui/ArrangerTimelineTest.cpp (689)`.

The new Xpressive lambda and the new test slot are not flagged. The baseline files were not edited and no gate limits were changed. The gates are red on this line for reasons outside this bug; a ratchet refresh is an owner decision.

## Files committed

- `plugins/Xpressive/Xpressive.cpp`
- `tests/src/plugins/PluginPortsMigrationTest.cpp`
- `tests/upstream-modifications.txt` (registrations for both files above)
- `docs/qwen-fixes-2026-10/cluster-new-p9-529-port.md` (this report)

## Environment note

The first build failed in ccache (`failed to create temporary file for /run/user/1000/ccache-tmp`, `build-1.log`). The retries set `CCACHE_TEMPDIR=/tmp/ccache-tmp-p9529` for the build environment only. No repository or user configuration was changed.
