new-p19-7: FIXED

Evidence: [src/core/ControlCommandsNoteRandom.cpp:390](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p19-7/src/core/ControlCommandsNoteRandom.cpp:390) now records an empty argument object for `control.undo`. [src/core/ControlCommandsNoteTransform.cpp:180](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p19-7/src/core/ControlCommandsNoteTransform.cpp:180) uses the same argument-free inverse for transpose, velocity offset, and velocity scale. Regression methods are in [ControlNoteScaleVerbsTest.cpp:364](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p19-7/tests/src/core/ControlNoteScaleVerbsTest.cpp:364) and [ControlNoteScaleVerbsTest.cpp:660](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p19-7/tests/src/core/ControlNoteScaleVerbsTest.cpp:660).

Regression matrix: the normal path applies a seeded roll and each transform, reissues the recorded inverse from the transaction, and checks the original note state. The failure path was the extra `clip`/`scope`/transform arguments rejected by `control.undo`'s empty-object schema; the new tests fail on that validation error. Exhaustion and reconfiguration paths do not exist for these synchronous command handlers. No audio-thread code changed; these commands and tests run on the control/UI thread. The existing Qt test target is configured for the project's supported build and offscreen environment.

RED commands and exit codes:

- `./ControlNoteScaleVerbsTest randomizeInverseCanBeReissuedThroughControlUndo > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p19-7.r1/logs/002-test-p19-randomize-red.log 2>&1; echo EXIT=$?` — 1; failure: `args: unexpected property 'clip'`.
- `./ControlNoteScaleVerbsTest transformInverseCanBeReissuedThroughControlUndo > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p19-7.r1/logs/003-test-p19-transform-red.log 2>&1; echo EXIT=$?` — 1; failure: `args: unexpected property 'clip'`.
- `ctest -R '^ControlNoteScaleVerbsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p19-7.r1/logs/004-ctest-p19-red.log 2>&1; echo EXIT=$?` — 8; one CTest test ran and failed.

GREEN and reverse-patch proof:

- `git diff -- src/core/ControlCommandsNoteRandom.cpp src/core/ControlCommandsNoteTransform.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p19-7.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p19-7.r1/logs/fix.patch` — 0.
- `cmake --build build --target ControlNoteScaleVerbsTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p19-7.r1/logs/009-build-p19-proof-red.log 2>&1; echo EXIT=$?` — 0.
- Reversed-patch runs of `randomizeInverseCanBeReissuedThroughControlUndo` and `transformInverseCanBeReissuedThroughControlUndo` — 1 each; both fail again on the rejected inverse arguments (logs 010 and 011).
- `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p19-7.r1/logs/fix.patch` — 0.
- `cmake --build build --target ControlNoteScaleVerbsTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p19-7.r1/logs/016-build-p19-final.log 2>&1; echo EXIT=$?` — 0.
- `ctest -R '^ControlNoteScaleVerbsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p19-7.r1/logs/017-ctest-p19-final-green.log 2>&1; echo EXIT=$?` — 0; 1/1 CTest test passed.

Gates: feature check log `018-feature-check-p19.log`, exit 1. `no-upstream-regression`, `check-namespace`, `no-tautology`, `doc-figures`, complexity, and file-length passed. `fork-sources` and `all-sources-reproduce` failed on the pre-existing `tests/all-sources.txt` recipe mismatch for SfzPlayer's vendored Abseil entries; that manifest was not changed by this fix.

new-p22-42: FIXED

Evidence: [src/core/ControlCommandsNoteRandom.cpp:170](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p19-7/src/core/ControlCommandsNoteRandom.cpp:170) rejects non-finite seeds before the range check and `uint32_t` cast. `readSeed` is shared by `note.randomize` and `note.random_seed_set` at lines 268 and 346. The regressions call both handlers directly to bypass schema validation and assert `InvalidArgs`; the seed setter also verifies the persisted seed stays unchanged.

Regression matrix: the normal finite-seed paths remain covered by the repeatability and persisted-seed tests. The failure path directly supplies NaN to both handlers, bypassing schema validation. Missing, fractional, and out-of-range values remain covered by the existing command tests. These handlers have no exhaustion or reconfiguration path. No audio-thread code changed; the calls run on the control/UI thread. The configured C++20 Qt test target built successfully.

RED commands and exit codes:

- `./ControlNoteScaleVerbsTest nonfiniteSeedIsRefusedByRandomizeHandler > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p19-7.r1/logs/020-test-p22-randomize-red.log 2>&1; echo EXIT=$?` — 1; handler accepted NaN.
- `./ControlNoteScaleVerbsTest nonfiniteSeedIsRefusedBySeedSetHandler > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p19-7.r1/logs/021-test-p22-seedset-red.log 2>&1; echo EXIT=$?` — 1; handler accepted NaN.

GREEN and reverse-patch proof:

- `git diff -- src/core/ControlCommandsNoteRandom.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p19-7.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p19-7.r1/logs/fix.patch` — 0.
- `cmake --build build --target ControlNoteScaleVerbsTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p19-7.r1/logs/025-build-p22-proof-red.log 2>&1; echo EXIT=$?` — 0.
- Reversed-patch runs of `nonfiniteSeedIsRefusedByRandomizeHandler` and `nonfiniteSeedIsRefusedBySeedSetHandler` — 1 each; both handlers accept NaN again (logs 026 and 027).
- `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p19-7.r1/logs/fix.patch` — 0.
- `cmake --build build --target ControlNoteScaleVerbsTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p19-7.r1/logs/028-build-p22-proof-green.log 2>&1; echo EXIT=$?` — 0.
- Direct runs of both seed regressions — 0 each (logs 029 and 030).

## Self-check

- Normal and failure paths: seeded rolls/transforms, finite and non-finite seeds, and inverse replay are covered. The p19 inverse tests fail with the product patch reversed; both p22 handler tests fail with the finite check reversed.
- Exhaustion/reconfiguration: these command handlers have no such path or bounded resource state.
- Audio-thread safety: no audio-thread or per-period code changed.
- Supported-platform build: the configured C++20 build compiled `zene` and the existing Qt test target; its offscreen CTest run passed.
- Test family count: `ControlNoteScaleVerbsTest` passed as 1/1 in log `031-ctest-p22-green.log`.
