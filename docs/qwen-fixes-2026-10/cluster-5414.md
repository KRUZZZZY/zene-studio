new-p16-248: FIXED — REAL; evidence: src/core/Track.cpp:482-488 checks `toInt(&ok)` and the `Track::Type` range before factory dispatch; regression `UnclaimedTrackTypeTest::malformedTrackTypesArePreservedNotCoerced` preserves absent and `oops` types and their subtrees. RED proof: `git diff -- src/core/Track.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5414.r1/logs/fix.patch` (0), `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5414.r1/logs/fix.patch` (0), `cmake --build build --target zene UnclaimedTrackTypeTest` (0), then from build/tests `./UnclaimedTrackTypeTest malformedTrackTypesArePreservedNotCoerced` (1; 3 tracks observed, expected 1; test-4-red.log). GREEN: `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5414.r1/logs/fix.patch` (0), same build (0), same test (0; test-5-green.log), `ctest -R '^UnclaimedTrackTypeTest$' --output-on-failure` from build/tests (0; 1/1, ctest-1.log). feature_check exit 0. Files committed: src/core/Track.cpp, tests/src/core/UnclaimedTrackTypeTest.cpp, tests/upstream-modifications.txt, this report.

## Evidence table

| Bug | RED test (exit) | GREEN test (exit) | Affected CTest family | Gates | Committed files |
|---|---|---|---|---|---|
| new-p16-248: FIXED | `./UnclaimedTrackTypeTest malformedTrackTypesArePreservedNotCoerced` (1; 3 tracks) | same command (0) | `ctest -R '^UnclaimedTrackTypeTest$' --output-on-failure` (0; 1/1) | feature_check 0; submit_check pending | src/core/Track.cpp; tests/src/core/UnclaimedTrackTypeTest.cpp; tests/upstream-modifications.txt; report |

## Self-check

- `feature_check.py`: exit 0; every gate passed, complexity and file-length reported no new item.
- `submit_check.py`: pending final report/commit check.
- Build reconfiguration: `cmake -S . -B build` exited 97 because the lane guard said configuring is the parent's job; all requested targets built successfully in the supplied configured tree.
