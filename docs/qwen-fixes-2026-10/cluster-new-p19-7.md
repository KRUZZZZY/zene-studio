FIXED
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

The initial feature check in log `018-feature-check-p19.log` exited 1 because the train-15 snapshot had reintroduced vendored SfzPlayer entries into `tests/all-sources.txt`. The manifest was regenerated from its unchanged documented recipe; the header and exclusion list were left intact.

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

Final feature-check table (log `046-feature-check-final.log`, command exit 0):

`python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p19-7 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p19-7.r1/logs/046-feature-check-final.log 2>&1; echo EXIT=$?` — 0.

| Gate | Result |
| --- | --- |
| fork-sources | PASS, exit 0 |
| all-sources-reproduce | PASS, exit 0 |
| no-upstream-regression | PASS, exit 0 |
| check-namespace | PASS, exit 0 |
| no-tautology | PASS, exit 0 |
| doc-figures | PASS, exit 0 |
| complexity | PASS, no new item |
| file-length | PASS, no new item |

The manifest correction removed only 1,148 SfzPlayer vendored entries excluded by the existing recipe; the scope definition and all gate thresholds are unchanged.

## Self-check

- RED/GREEN proof: p19 inverse calls failed with extra arguments on the original and reversed-patch implementations, then passed with empty inverse args. p22 direct handlers accepted NaN without the finite check and refused it with the fix. p15 passed on the existing ID store and failed under the temporary positional-store mutation.
- Determinism: fixed seed 424242 drives the roll inverse test; fixed transform values drive the three transform inverse paths; `std::nan("0")` is injected directly to both seed handlers; seed 2 and jitter 48 deterministically reorder the two selected notes. No clocks, sleeps, or races are used.
- Cause and sibling coverage: both `readSeed` callers (`note.randomize`, `note.random_seed_set`) are tested. All three callers of `transformTransaction` (transpose, velocity offset, velocity scale) are covered. The shared ID store used by selection, reordering, and removal paths was inspected; the p15 finding is already fixed in this branch.
- Collateral behavior: no project serialization or audio-thread code changed. The only new validation rejects non-finite seeds, which cannot be legitimate seed values; existing finite bounds and old project seed loading remain unchanged.
- Exhaustion/reconfiguration: these command handlers have no such path or bounded resource state.
- Supported-platform build: both existing Qt test targets and `zene` built with the configured C++20 toolchain; CTest ran with the configured offscreen environment.
- Test family count: 2/2 passed in log `039-ctest-family-final.log`.
- Gate registration: `tests/all-sources.txt` was regenerated from its unchanged header recipe; the 1,148 excluded vendor entries were the only difference. `tests/fork-sources-gate.sh` and `all-sources-reproduce.sh` now pass in the final 046 gate run.
- `new-p19-7` commit `13e00ec90` contains `src/core/ControlCommandsNoteRandom.cpp`, `src/core/ControlCommandsNoteTransform.cpp`, `tests/src/core/ControlNoteScaleVerbsTest.cpp`, `tests/upstream-modifications.txt`, and this report.
- `new-p22-42` commit `cafdb6221` contains `src/core/ControlCommandsNoteRandom.cpp`, `tests/src/core/ControlNoteScaleVerbsTest.cpp`, `tests/upstream-modifications.txt`, and this report.
- `new-p15-14` is an already-fixed finding; its separate regression/report commit contains `tests/src/core/NoteSelectionIdentityTest.cpp` and this report.
- The gate repair is recorded with this batch in a follow-up commit containing `tests/all-sources.txt`, `tests/upstream-modifications.txt`, and this report; it does not change product behavior or gate thresholds.
