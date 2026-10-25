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

new-p15-14: ALREADY-FIXED

Evidence: [src/core/ControlEditSupport.cpp:273](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p19-7/src/core/ControlEditSupport.cpp:273) stores selected `Note::id()` values; `noteSelected` and `selectedNoteIndices` resolve those IDs against the current order at lines 282–297. `note.select_where` writes the same identity store at [ControlCommandsNoteShape.cpp:495](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p19-7/src/core/ControlCommandsNoteShape.cpp:495), and note removal and move explicitly rely on IDs at [ControlCommandsNotes.cpp:198](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p19-7/src/core/ControlCommandsNotes.cpp:198) and line 275. The positional-store defect is not present in this branch.

The deterministic regression in [NoteSelectionIdentityTest.cpp:126](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p19-7/tests/src/core/NoteSelectionIdentityTest.cpp:126) selects the first of two notes, rolls them with seed 2 and position jitter 48 until their order reverses, then checks both `selected` and `selected_notes` still name the original ID. It passed on the branch's ID-based store. To prove sensitivity, I temporarily fault-injected the old positional behavior into `ControlEditSupport.cpp`; the test failed at the selected-flag assertion, and the source was restored before the final build.

Commands and exit codes:

- `./NoteSelectionIdentityTest randomizeReorderingNotesKeepsTheSelectionOnItsIdentity > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p19-7.r1/logs/033-test-p15-baseline-green.log 2>&1; echo EXIT=$?` — 0.
- `git diff -- src/core/ControlEditSupport.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p19-7.r1/logs/p15-mutation.patch; echo EXIT=$?` — 0; records the temporary positional-store mutation.
- `./NoteSelectionIdentityTest randomizeReorderingNotesKeepsTheSelectionOnItsIdentity > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p19-7.r1/logs/035-test-p15-mutation-red.log 2>&1; echo EXIT=$?` — 1 under the positional-store mutation.
- `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p19-7.r1/logs/p15-mutation.patch; echo EXIT=$?` — 0; restores the branch's identity store.
- `cmake --build build --target ControlNoteScaleVerbsTest NoteSelectionIdentityTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p19-7.r1/logs/038-build-family-final.log 2>&1; echo EXIT=$?` — 0.
- `ctest -R '^(ControlNoteScaleVerbsTest|NoteSelectionIdentityTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p19-7.r1/logs/039-ctest-family-final.log 2>&1; echo EXIT=$?` — 0; 2/2 CTest tests passed.

Final feature-check table (log `040-feature-check-final.log`, command exit 1):

| Gate | Result |
| --- | --- |
| fork-sources | FAIL, exit 1 |
| all-sources-reproduce | FAIL, exit 1 |
| no-upstream-regression | PASS, exit 0 |
| check-namespace | PASS, exit 0 |
| no-tautology | PASS, exit 0 |
| doc-figures | PASS, exit 0 |
| complexity | PASS, no new item |
| file-length | PASS, no new item |

The two failures are unrelated to these fixes: the gate logs show `tests/all-sources.txt` lists SfzPlayer vendored Abseil sources excluded by its own documented recipe. That manifest and recipe were unchanged. Altering the gate scope would re-anchor it, so the pre-existing mismatch is recorded here rather than changed as part of this bug batch.

## Self-check

- Normal and failure paths: seeded rolls/transforms, finite and non-finite seeds, inverse replay, and note reordering are covered. The p15 regression fails under the positional-store mutation and passes after restoring the branch implementation.
- Exhaustion/reconfiguration: these command handlers have no such path or bounded resource state.
- Audio-thread safety: no audio-thread or per-period code changed.
- Supported-platform build: both existing Qt test targets and `zene` built with the configured C++20 toolchain; CTest ran with the configured offscreen environment.
- Test family count: 2/2 passed in log `039-ctest-family-final.log`.
- `new-p19-7` commit `13e00ec90` contains `src/core/ControlCommandsNoteRandom.cpp`, `src/core/ControlCommandsNoteTransform.cpp`, `tests/src/core/ControlNoteScaleVerbsTest.cpp`, `tests/upstream-modifications.txt`, and this report.
- `new-p22-42` commit `cafdb6221` contains `src/core/ControlCommandsNoteRandom.cpp`, `tests/src/core/ControlNoteScaleVerbsTest.cpp`, `tests/upstream-modifications.txt`, and this report.
- `new-p15-14` is an already-fixed finding; its separate regression/report commit contains `tests/src/core/NoteSelectionIdentityTest.cpp` and this report.
