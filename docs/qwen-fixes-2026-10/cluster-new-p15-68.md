FALSE-POSITIVE
new-p15-68: FALSE-POSITIVE
new-p16-74: FALSE-POSITIVE

## Evidence

### new-p15-68

`tests/src/core/FreezeRegionRepairTest.cpp:391-403` and `:442-459` retain `Track*` values across `control.undo`. In both paths, the undo is the checkpoint made by `freeze.unfreeze`: `src/core/ControlCommandsFreeze.cpp:258-264` calls `track->addJournalCheckPoint()` and documents that one undo restores that take. `src/core/ProjectJournal.cpp:75-84,95-108` resolves the saved journal id to the live object and calls `restoreState()` on it. `SerializingObject::restoreState()` calls `loadSettings()` on that same object (`src/core/SerializingObject.cpp:69-73`); `Track::loadSettings()` loads state into the existing track (`src/core/Track.cpp:1003-1006`). This checkpoint does not replace the track, so the raw pointers remain valid in these two recovery sequences. The region test obtains `reopened` after `project.open` has already replaced the project contents.

The finding's suggested stable-id lookup would be safe, but it is not needed to avoid a dangling pointer on these command paths. No source or test change is appropriate for a false positive.

### new-p16-74

`include/Track.h:158-161` declares `getClips()` to return `const clipVector&`. Both calls in `tests/src/core/FreezeRegionRepairTest.cpp:92-95` therefore refer to the same vector stored on the live track; `std::none_of` receives a valid iterator range. No temporary clip lists are created, so no fix or regression test is appropriate.

## Test matrix

- Normal recovery: the family test covers a missing whole-track take followed by unfreeze/undo, and a missing region take followed by project reopen and unfreeze/undo.
- Failure and reconfiguration: the tests cover missing-media recovery. Neither finding involves resource exhaustion or runtime reconfiguration, so those paths do not apply.
- Audio thread: both findings concern test/control-thread code. `noClipMuted` is a read-only test helper; no audio-thread path changes.
- Supported platforms: no platform-specific source changes were made. The configured Linux C++20/Qt build was used; other platform builds were not available in this worktree.
- RED/GREEN: not applicable because both findings are false positives. The existing family test passed before and after the review; no failing regression was fabricated.

## Verification

The normal family build and CTest each passed. CTest ran 1/1 test. A temporary AddressSanitizer build of the same existing target also passed 1/1; this did not produce a RED result and is not claimed as RED/GREEN proof.

| Feature gate | Result |
| --- | --- |
| fork-sources | PASS, exit 0 |
| all-sources-reproduce | PASS, exit 0 |
| no-upstream-regression | PASS, exit 0 |
| check-namespace | PASS, exit 0 |
| no-tautology | PASS, exit 0 |
| doc-figures | PASS, exit 0 |
| complexity | PASS, no new item in changed files |
| file-length | PASS, no new item in changed files |

The initial feature check reported `0 changed files` and found 1,148 obsolete vendor entries in `tests/all-sources.txt`. I removed those paths because the documented generator at `tests/all-sources.txt:20-28` excludes `plugins/SfzPlayer/sfizz/`; the test remains registered in `tests/fork-sources.txt:2405` and `tests/all-sources.txt:2688`.

Commands and observed exit codes:

```text
cmake -S . -B build -DWANT_DEBUG_ASAN=ON > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-68.r1/logs/001-config-asan.log 2>&1; echo EXIT=$?
EXIT=97 (lane guard: configuring is the parent's job; this build tree is already configured)

cmake --build build --target FreezeRegionRepairTest -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-68.r1/logs/002-baseline-build.log 2>&1; echo EXIT=$?
EXIT=0

ctest -R '^FreezeRegionRepairTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-68.r1/logs/003-baseline-test.log 2>&1; echo EXIT=$?
EXIT=0 (run from build/tests; 1/1)

cmake --build build --target FreezeRegionRepairTest -- "CXX_FLAGS=$base_flags -fsanitize=address -fno-omit-frame-pointer" > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-68.r1/logs/004-red-asan-build.log 2>&1; echo EXIT=$?
EXIT=0 (temporary test-only instrumentation; $base_flags was read from build/tests/CMakeFiles/FreezeRegionRepairTest.dir/flags.make)

ctest -R '^FreezeRegionRepairTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-68.r1/logs/005-red-asan-test.log 2>&1; echo EXIT=$?
EXIT=0 (run from build/tests; 1/1; no sanitizer finding)

cmake --build build --target FreezeRegionRepairTest -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-68.r1/logs/006-normal-build.log 2>&1; echo EXIT=$?
EXIT=0

ctest -R '^FreezeRegionRepairTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-68.r1/logs/007-final-test.log 2>&1; echo EXIT=$?
EXIT=0 (run from build/tests; 1/1)

python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p15-68 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-68.r1/logs/008-feature-check.log 2>&1; echo EXIT=$?
EXIT=1 (before source-manifest correction; 1,148 out-of-scope vendor entries)

python3 -c 'from pathlib import Path; p=Path("tests/all-sources.txt"); lines=p.read_text().splitlines(keepends=True); removed=sum(line.startswith("plugins/SfzPlayer/sfizz/") for line in lines); p.write_text("".join(line for line in lines if not line.startswith("plugins/SfzPlayer/sfizz/"))); print(f"removed={removed}")'
EXIT=0 (removed=1148)

python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p15-68 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-68.r1/logs/010-feature-check-final.log 2>&1; echo EXIT=$?
EXIT=0 (all gates pass)

python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p15-68 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-68.r1/logs/011-feature-check-last.log 2>&1; echo EXIT=$?
EXIT=0 (all gates pass)

python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p15-68 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-68.r1/logs/012-feature-check-final-report.log 2>&1; echo EXIT=$?
EXIT=0 (all gates pass)

git add docs/qwen-fixes-2026-10/cluster-new-p15-68.md tests/all-sources.txt
git commit -m "qwen new-p15-68: record false-positive findings new-p16-74"
EXIT=0
```

## Self-check

- RED/GREEN: not applicable; both findings are false positives and no product change exists to revert. Baseline, AddressSanitizer, and final family runs each passed 1/1 (`logs/003`, `005`, `007`).
- Deterministic test: no new regression was added because neither report describes faulty behavior. No test timing, races, or seams were introduced.
- Cause and siblings: `freeze.unfreeze` checkpoints the live Track and `ProjectJournal` restores that saved object. `getClips()` returns a const reference; no iterator code change was needed.
- Collateral: no product or test source changed, so project loading, saved data, realtime behavior, and command/undo behavior are unchanged. The only source-scope adjustment removed vendor files explicitly excluded by the manifest's own recipe.
- False-positive evidence: the applicable code contracts are `src/core/ControlCommandsFreeze.cpp:258-264`, `src/core/ProjectJournal.cpp:75-108`, `src/core/SerializingObject.cpp:69-73`, `src/core/Track.cpp:1003-1006`, and `include/Track.h:158-161`.

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p15-68.md`
- `tests/all-sources.txt`
