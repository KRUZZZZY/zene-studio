Verdict: FIXED

new-p16-322: FIXED. A valid consolidation range whose frame count is far past what a render can hold (a whole-song range at a slow tempo, or a tick range near the full width of tick_t) allocated its zero-filled mix before anything checked its size. `ClipConsolidate::render()` now bounds the frame count before the allocation and refuses it with an error; nothing is allocated and no file is written.

Evidence (current line numbers; the finding's line 137 has moved):

- The unbounded allocation was `std::vector<SampleFrame> mix(static_cast<std::size_t>(std::llround(ticks * framesPerTick)), ...)`. Frames = ticks × `Engine::framesPerTick(rate)`, and `framesPerTick` = rate × 240 / 192 / tempo ([Engine.cpp:137-138](src/core/Engine.cpp:137)). The tempo floor is 10 BPM ([Song.h:78](include/Song.h:78)), so the schema-legal whole-song range `0..MaxSongLength` (1,919,808 ticks, [Song.h:81](include/Song.h:81)) is about 1.15 × 10^10 frames at 48 kHz / 10 BPM (6000 frames a tick, ~92 GB of mix) and about 8.2 × 10^8 frames (~6.6 GB) at the default 140 BPM. The export path `writeClip` ([ControlCommandsLibraryClips.cpp:167](src/core/ControlCommandsLibraryClips.cpp:167)) and `clip.consolidate` ([ControlCommandsConsolidate.cpp:212](src/core/ControlCommandsConsolidate.cpp:212)) both reach `render()`.
- Fix: `MaxRenderBytes` = 2 GiB of float stereo audio and `MaxRenderFrames` = that / `sizeof(SampleFrame)` ([ClipConsolidate.cpp:52-53](src/core/ClipConsolidate.cpp:52)); the frame count is computed and compared before the `mix` allocation ([ClipConsolidate.cpp:143-151](src/core/ClipConsolidate.cpp:143)). The refusal sets `result.ok = false` and an error naming the limit, so both callers report a refusal and write nothing (`clip.consolidate` renders first, so the clips are unchanged).
- Limit choice (a judgement call, see the report's Notes): 2 GiB of audio is 268,435,456 frames, about 93 min at 48 kHz and about 47 min at 96 kHz. The peak is about 4 GiB because the mix and the interleaved copy `writeWav` makes are held at once. Any render longer than that is now refused where it used to allocate. Ranges below it are unchanged.
- Regression tests: [ClipConsolidateTest.cpp:261-300](tests/src/core/ClipConsolidateTest.cpp:261) add `aRangeTooLargeToRenderIsRefusedBeforeItIsAllocated` (render over the full tick_t width, `[INT32_MIN+1, INT32_MAX)`) and `theWholeSongRangeOfACommandIsRefusedAndChangesNothing` (the user path: `clip.consolidate` over `0..MaxSongLength`, asserting `Refused`, the limit in the message, no file, and the clip unchanged). Both catch `std::bad_alloc` so an unfixed render is reported as a failed assertion. Both are cheap after the fix (refused before allocation).

| Step | Command (run from `build/tests` for ctest; `cmake --build build` from the worktree) | Result |
| --- | --- | --- |
| Build test, attempt 1 | `cmake --build build --target ClipConsolidateTest -j4 > .../build-1.log 2>&1; echo EXIT=$?` | EXIT=2 — ccache could not create a temp file in the sandbox's ccache dir (`Permission denied`); not a compile error |
| Build test, attempt 2 (RED build, unfixed product code) | `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp cmake --build build --target ClipConsolidateTest -j4 > .../build-2.log 2>&1; echo EXIT=$?` | EXIT=0 |
| RED run (unfixed code) | `(ulimit -v 4194304; ctest -R '^ClipConsolidateTest$' --output-on-failure > .../test-1-red.log 2>&1); echo EXIT=$?` | **EXIT=8**: `8 passed, 2 failed` |
| RED assertions | from `test-1-red.log` | `FAIL! aRangeTooLargeToRenderIsRefusedBeforeItIsAllocated() render() allocated the whole tick range instead of refusing it` (ClipConsolidateTest.cpp:274); `FAIL! theWholeSongRangeOfACommandIsRefusedAndChangesNothing() 'refused.errorMessage.contains("limit")' returned FALSE. (command 'clip.consolidate' failed: std::bad_alloc ...)` (ClipConsolidateTest.cpp:291) |
| Fix applied | `CCACHE_TEMPDIR=... cmake --build build --target ClipConsolidateTest -j4 > .../build-3.log 2>&1; echo EXIT=$?` | EXIT=0 |
| GREEN run (fixed code) | `cd build/tests && ctest -R '^ClipConsolidateTest$' -V > .../test-3-green-verbose.log 2>&1; echo EXIT=$?` | **EXIT=0**: `Totals: 10 passed, 0 failed` (init, 6 existing cases, 2 new, cleanup) |
| 4b step 1: revert the product diff | `git diff -- src/core/ClipConsolidate.cpp > .../fix.patch && git apply -R .../fix.patch` | EXIT=0 |
| 4b step 2: rebuild unfixed, run | `cmake --build build --target ClipConsolidateTest -j4 > .../build-4-unfixed.log 2>&1` EXIT=0; `(ulimit -v 4194304; cd build/tests && ctest -R '^ClipConsolidateTest$' -V > .../test-4-unfixed-4b.log 2>&1); echo EXIT=$?` | build EXIT=0; **test EXIT=8**: `8 passed, 2 failed` (same two assertions as RED) |
| 4b step 3: re-apply the fix | `git apply .../fix.patch` | EXIT=0 |
| 4b step 4: rebuild fixed, run | `cmake --build build --target ClipConsolidateTest -j4 > .../build-5-fixed.log 2>&1` EXIT=0; `cd build/tests && ctest -R '^ClipConsolidateTest$' -V > .../test-5-fixed-4b.log 2>&1; echo EXIT=$?` | build EXIT=0; **test EXIT=0**: `10 passed, 0 failed` |
| Affected family, fixed code | `cmake --build build --target RangeEditsTest ProvenanceVersionTest ArrangeToolsTest -j4 > .../build-6-family.log 2>&1` EXIT=0; `cd build/tests && ctest -R 'ClipConsolidateTest|RangeEditsTest|ProvenanceVersionTest|ArrangeToolsTest' -V > .../test-6-family.log 2>&1; echo EXIT=$?` | build EXIT=0; **test EXIT=0**: 4 of 4 ctest tests pass; cases: ClipConsolidateTest 10, RangeEditsTest 14, ArrangeToolsTest 24, ProvenanceVersionTest 6 (54 cases in total) |
| Gate: fork-sources | `bash tests/fork-sources-gate.sh > .../gate-fork-sources.log 2>&1` | EXIT=0 |
| Gate: all-sources-reproduce | `bash tests/all-sources-reproduce.sh > ...` | EXIT=0 |
| Gate: no-upstream-regression | `bash tests/no-upstream-regression-gate.sh > ...` | EXIT=0 |
| Gate: check-namespace | `tests/scripted/check-namespace > ...` | EXIT=0 |
| Gate: no-tautology | `bash tests/no-tautology-gate.sh > ...` | EXIT=0 |
| Gate: doc-figures | `python3 tests/doc-figures-gate.py > ...` | EXIT=0 |
| Gate: complexity (ratchet) | `bash tests/complexity-gate.sh --check > .../gate-complexity.log 2>&1` | EXIT=1. Its REGRESSION lines are `WasmSandbox::loadModuleBytes`, `dotCentreAlong`, `parseNotes`, `setStrip`, `adjust`, `landPairState`, none in a file this branch changes (`ClipConsolidate.cpp` and `ClipConsolidateTest.cpp` are not listed); red on the train on purpose |
| Gate: file-length (ratchet) | `bash tests/file-length-gate.sh --check > .../gate-file-length.log 2>&1` | EXIT=1. Its REGRESSION lines are in other files (Vst3Host, TakeLane, WasmSandbox, and several test files); none in a file this branch changes; red on the train on purpose |
| Wrapper `feature_check.py` | `python3 .../feature_check.py <worktree>` | EXIT=1 with `PermissionError` creating `<worktree>-feature-check/` outside the write-jail (its logs and ratchet baselines live outside the worktree). The table could not be produced; the constituent gate commands were run directly as above, with the same exit codes |

Files committed for this verdict: `src/core/ClipConsolidate.cpp`, `tests/src/core/ClipConsolidateTest.cpp`, and this report. No new file, so no registration change was needed; both files are already registered in `tests/fork-sources.txt` and `tests/all-sources.txt`. No header changed, so no header-dependent rebuild was needed.

Notes:

- The RED run is under `ulimit -v 4194304` (4 GiB of address space). On this 30 GB machine the unfixed render would otherwise zero-fill terabytes; with the cap each unfixed allocation fails at once with `std::bad_alloc`. The cap is for the RED proof only; the committed tests are cheap in the fixed code and do not need it.
- The 2 GiB limit is a choice, not a number taken from the finding: the finding asks for "a practical limit". It is one constant (`MaxRenderBytes`) and can be changed in one place. Its effect on the export path is that a clip longer than about 93 min at 48 kHz (47 min at 96 kHz) now exports with a refusal where it used to allocate.
- `ccache` needed `CCACHE_TEMPDIR` pointing at `$TMPDIR` in this sandbox; this is an environment setting, not a product change.
