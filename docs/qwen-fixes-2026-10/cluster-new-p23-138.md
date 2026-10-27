Verdict: FALSE-POSITIVE

new-p23-138: `LinkedClipEdits::adoptTrimmedContent` cannot empty the trimmed clip's own notes. The finding's premise ("the loop over `contentMembersOf(trimmed)` includes `trimmed` itself") is contradicted by the code. No product change is made. A regression guard test is added; it passes on the unmodified product code and fails when the self-exclusion is removed.

## Evidence

- Caller loop: [LinkedClipEdits.cpp:58](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-138/src/core/LinkedClipEdits.cpp:58) iterates `contentMembersOf(trimmed)`. The clear is at [LinkedClipEdits.cpp:62](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-138/src/core/LinkedClipEdits.cpp:62) and the read of `trimmed->notes()` at [LinkedClipEdits.cpp:63](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-138/src/core/LinkedClipEdits.cpp:63).
- Self-exclusion: [ClipLinks.cpp:119](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-138/src/core/ClipLinks.cpp:119) in `contentMembersOf()` (defined at [ClipLinks.cpp:114](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-138/src/core/ClipLinks.cpp:114)) reads `if (member == clip) { continue; }`. So `trimmed` is never in the peer list, and the clear at line 62 can only touch other clips.
- Contract: [LinkedClipEdits.h:41-45](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-138/include/LinkedClipEdits.h:41) documents that `adoptTrimmedContent` writes the re-based notes onto every *other* linked instance, with mirroring suspended by the caller.
- Only caller: [MidiClipView.cpp:496](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-138/src/gui/clips/MidiClipView.cpp:496) (`clearNotesOutOfBounds`). The clone `newClip` keeps the linkId and is built under `MirroringSuspension` ([MidiClipView.cpp:462-464](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-138/src/gui/clips/MidiClipView.cpp:462)). The old clip is detached first by `remove()` ([ClipView.cpp:272](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-138/src/gui/clips/ClipView.cpp:272), which calls `Track::removeClip` at [ClipView.cpp:287](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-138/src/gui/clips/ClipView.cpp:287)), so `group()` cannot return it.
- The finding's hazard therefore needs `trimmed` in its own peer list. That is what the mutation below simulates, and it is not reachable in the current code.

## Regression guard (test)

Added `aTrimmedMemberKeepsItsOwnNotesWhileThePeersTakeACopy` to `tests/src/core/LinkedClipEditsTest.cpp` (the existing registered test file; no new files). It builds three linked clips, puts two notes on the first, calls `ClipLinks::adoptTrimmedContent(trimmed)` under `MirroringSuspension`, and asserts `written == 2`, `trimmed` keeps 2 notes, and the other two clips each have 2 notes. It uses no clock, sleep, or race: the group is built with the existing control-registry helpers and the function is called directly.

## Commands and exit codes

All commands were run from the worktree or `build/tests` as noted.

| Step | Command | EXIT |
| --- | --- | --- |
| Build (first attempt) | `cmake --build build --target LinkedClipEditsTest -j4` (log `build-1.log`) | 2 (ccache could not create its temp file, `Permission denied`; not a code error) |
| Build | `CCACHE_DISABLE=1 cmake --build build --target LinkedClipEditsTest -j4` (log `build-2.log`) | 0 |
| Baseline test | `cd build/tests && ctest -R '^LinkedClipEditsTest$' --output-on-failure` (log `test-1.log`) | 0 (1/1 passed) |
| Baseline test, verbose | `cd build/tests && ./LinkedClipEditsTest -v2` (log `run-baseline.log`) | 0 (`Totals: 11 passed, 0 failed`) |
| Mutation: record diff | `git diff -- src/core/ClipLinks.cpp > mutation.patch` (after removing the `member == clip` skip at ClipLinks.cpp:119) | 0 |
| Mutant build | `CCACHE_DISABLE=1 cmake --build build --target LinkedClipEditsTest -j4` (log `build-3-mutant.log`) | 0 |
| Mutant test (RED for the premise) | `cd build/tests && ./LinkedClipEditsTest -v2` (log `run-red-mutant.log`) | 1 (`FAIL!` at `QCOMPARE(written, 2)`: 3 vs 2; `Totals: 10 passed, 1 failed`) |
| Restore | `git apply -R mutation.patch` | 0 (`git diff -- src/core/ClipLinks.cpp` is empty; line 119 back to the skip) |
| Restored build | `CCACHE_DISABLE=1 cmake --build build --target LinkedClipEditsTest -j4` (log `build-4-restored.log`) | 0 |
| Final test | `cd build/tests && ctest -R '^LinkedClipEditsTest$' --output-on-failure` (log `test-2-green.log`) | 0 (1/1 passed) |
| Final test, verbose | `cd build/tests && ./LinkedClipEditsTest -v2` (log `run-green.log`) | 0 (`Totals: 11 passed, 0 failed`) |
| Gate: fork-sources | `bash tests/fork-sources-gate.sh` | 0 |
| Gate: all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 |
| Gate: no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 |
| Gate: check-namespace | `tests/scripted/check-namespace` | 0 |
| Gate: no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| Gate: doc-figures | `python3 tests/doc-figures-gate.py` | 0 |
| Gate: complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 (regressions only in other files; see below) |
| Gate: file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 (regressions only in other files; see below) |

Test count: the `LinkedClipEditsTest` family ran 1 ctest test (`ctest` reported 1/1 passed); the binary executed 11 test functions, all passing, including the new one (`run-green.log`).

### Gate notes

- `feature_check.py` could not run as written. It creates `<worktree>-feature-check` next to the worktree (`feature_check.py:71`), and that parent directory is not writable from this session (`PermissionError`, log `submit-1.log` and the first feature-check attempt). I ran the same commands it runs (SIMPLE and RATCHET lists, `feature_check.py:22-29`) directly from the worktree and recorded each exit code above.
- The complexity ratchet and file-length ratchet fail on items in other files (for example `WasmSandbox::loadModuleBytes`, `ControlCommandsMixerTools.cpp adjust`, `ControlShutdownHookTest.cpp` at 672 lines). None of them is in `LinkedClipEditsTest.cpp` or in the function I added, so my change introduces no new over-limit item. Per the brief these are red on the train on purpose.

## Self-check

- **Regression test fails with the product change reverted and passes with it.** Not applicable: there is no product change to revert. The guard passes on the unmodified code (`run-baseline.log`, `run-green.log`). The mutation that realises the finding's premise (skip removed) makes it fail with exit 1 (`run-red-mutant.log`), which shows the guard detects the failure mode the finding describes.
- **Deterministic.** Yes. No clock, sleep, or race. The inputs are fixed note keys and positions, and the group is built through the existing `clip.add` and `clip.link_create` helpers.
- **Fix at the cause and complete.** No fix was made, because the claimed cause does not occur. The only caller is `MidiClipView.cpp:496`, and `contentMembersOf` (the single source of peers) excludes `trimmed`. I did not add a defensive snapshot of `trimmed->notes()`: it would change no observable behaviour, and the brief asks for a fix only for a real defect.
- **No collateral damage.** The product tree is unchanged (`git status` shows only the test file). The mutation was applied, built, and reverted with `git apply -R`; `src/core/ClipLinks.cpp` matches HEAD.
- **Verdict evidence.** `LinkedClipEdits.cpp:58` (loop), `ClipLinks.cpp:119` (self-skip), `LinkedClipEdits.h:41-45` (contract), `MidiClipView.cpp:462-496` (sole caller, clone under suspension).
- **Build workaround.** The first build failed in ccache (`Permission denied` on its temp dir under `~/.local/share/zene-bunny-fix/dsh/...`). Builds used `CCACHE_DISABLE=1`. This affects compile caching only, not results.
