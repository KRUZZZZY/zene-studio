# Cluster new-p10-348: project lead validation

**Verdict: FIXED**

`SampleClip::loadSettings` now checks `toULongLong` conversion success. `SampleClip::restoreState` validates the restored value after `Clip::restoreState` loads the clip's warp settings, and clears a lead that is not strictly less than the final `clipFramesPerTick()`. This keeps the frame-exact lead below one source tick without checking against stale tempo state.

Evidence:

- [src/core/SampleClip.cpp:407] validates the lead after base clip restoration; [src/core/SampleClip.cpp:644] checks conversion success while reading `lead`.
- [tests/src/tracks/SampleClipWindowTest.cpp:170] covers a valid lead, `uint64` max, a lead at or above one tick, and malformed text.
- [tests/src/core/ArrangingCatchUpTest.cpp:225] uses a valid one-frame lead for its existing save/load persistence check.

## Validation evidence

| Check | Exact command | Result |
|---|---|---|
| RED on original code | `ctest -R '^SampleClipWindowTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-348.r1/logs/test-1.log 2>&1; echo EXIT=$?` | `EXIT=8`; 12 passed, 1 failed. The max lead loaded as `18446744073709551615` instead of 0. |
| RED after removing only the product patch | `ctest -R '^SampleClipWindowTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-348.r1/logs/test-5.log 2>&1; echo EXIT=$?` | `EXIT=8`; the same max-lead assertion failed. |
| GREEN, affected test family | `ctest -R '^(SampleClipWindowTest|SampleClipStretchTest|SampleClipRubberBandTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-348.r1/logs/test-8.log 2>&1; echo EXIT=$?` | `EXIT=0`; 3/3 ctest suites passed. |
| Existing lead save/load case | `./ArrangingCatchUpTest theLeadSavesLoadsAndIsResetWhenAbsent > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-348.r1/logs/test-7.log 2>&1; echo EXIT=$?` | `EXIT=0`; 3 passed, 0 failed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-348.r1/logs/gate-1.log 2>&1; echo EXIT=$?` | `EXIT=0`. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-348.r1/logs/gate-2.log 2>&1; echo EXIT=$?` | `EXIT=0`. |

The reversal proof used:

```sh
git diff -- src/core/SampleClip.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-348.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-348.r1/logs/fix.patch; echo EXIT=$?
cmake --build build --target zene SampleClipWindowTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-348.r1/logs/build-3.log 2>&1; echo EXIT=$?
ctest -R '^SampleClipWindowTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-348.r1/logs/test-5.log 2>&1; echo EXIT=$?
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-348.r1/logs/fix.patch; echo EXIT=$?
cmake --build build --target zene SampleClipWindowTest ArrangingCatchUpTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-348.r1/logs/build-4.log 2>&1; echo EXIT=$?
```

The commands above returned `EXIT=0`, `EXIT=0`, `EXIT=8`, `EXIT=0`, and `EXIT=0`, respectively. The final build command was `cmake --build build --target zene SampleClipWindowTest SampleClipStretchTest SampleClipRubberBandTest ArrangingCatchUpTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-348.r1/logs/build-5.log 2>&1; echo EXIT=$?` (`EXIT=0`). The baseline and intermediate builds also returned 0:

```sh
cmake --build build --target SampleClipWindowTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-348.r1/logs/build-1.log 2>&1; echo EXIT=$?
cmake --build build --target zene SampleClipWindowTest ArrangingCatchUpTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-348.r1/logs/build-2.log 2>&1; echo EXIT=$?
```

Both returned `EXIT=0`. The focused reruns were also green: `ctest -R '^SampleClipWindowTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-348.r1/logs/test-3.log 2>&1; echo EXIT=$?` (`EXIT=0`), `./ArrangingCatchUpTest theLeadSavesLoadsAndIsResetWhenAbsent > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-348.r1/logs/test-4.log 2>&1; echo EXIT=$?` (`EXIT=0`), and after reapplying the patch, `ctest -R '^SampleClipWindowTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-348.r1/logs/test-6.log 2>&1; echo EXIT=$?` (`EXIT=0`).

An additional combined run, `ctest -R '^(SampleClipWindowTest|ArrangingCatchUpTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-348.r1/logs/test-2.log 2>&1; echo EXIT=$?`, returned `EXIT=8`: `ArrangingCatchUpTest::queuedInstrumentHandlesAreRemovedBeforeTheirTrackCanDie` expected `tripleoscillator` but received `dummyinstrument`. The focused lead persistence case passed as recorded above; the three sample-clip suites passed together.

## Files committed

- `src/core/SampleClip.cpp`
- `tests/src/tracks/SampleClipWindowTest.cpp`
- `tests/src/core/ArrangingCatchUpTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-348.md`
