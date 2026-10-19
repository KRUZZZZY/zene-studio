# Cluster review-4ec3067be — Follow Action integer fields silently coerce fractional or huge numbers

## Verdict: ALREADY-FIXED

The finding is **real as originally reported** but has already been fixed on this branch, in
commit `5784cb570` ("fix(rework 4ec3067be): follow-action jump_to truncated fractions and wrapped
large numbers"), which is an ancestor of HEAD. No code change was needed in this pass; the
working tree is byte-identical to HEAD (`git diff --stat` empty before and after).

Because a stale-but-passing test proves nothing, I did **not** stop at reading the commit message.
I reverse-applied the committed product hunk, rebuilt and ran the regression to confirm it goes
RED without the fix, then restored it and confirmed it goes back GREEN. The fix is live in this
tree and the existing test genuinely catches the defect.

## The code the finding points at

`src/core/ControlCommandsSessionFollow.cpp`

- `intField()` — **ControlCommandsSessionFollow.cpp:131-150** — the helper the finding describes.
  It now validates before any conversion: `!std::isfinite(number) || number != std::floor(number)`
  (ControlCommandsSessionFollow.cpp:141) plus an `int`-range check
  (ControlCommandsSessionFollow.cpp:142-143), and only then does `static_cast<int>(number)`
  (ControlCommandsSessionFollow.cpp:148). A bad value is refused with a named `invalid_args`
  message (ControlCommandsSessionFollow.cpp:145).
- `followActionTargetFromJson()` — **ControlCommandsSessionFollow.cpp:194-207** — the only caller;
  `jump_to` goes through `intField` at ControlCommandsSessionFollow.cpp:198, and the pre-existing
  grid range check still runs afterwards at ControlCommandsSessionFollow.cpp:199-204.

The finding's wording ("`intField` checks `isDouble` then `toInt`") describes the *pre-`5784cb570`*
state. That is exactly the state I reproduced for the RED run below.

## Regression coverage

`SessionCommandArgsTest::aWrongTypedActionFieldIsRefused`
(`tests/src/core/SessionCommandArgsTest.cpp:169-177`) sends `jump_to` of `0.5` and `1e30` inside
an untyped `follow_actions` entry of `session.set_slot` and requires `ControlErrorKind::InvalidArgs`
for both. `session.set_slot`'s args schema does not pre-validate `follow_actions` entries, so the
handler path is the one under test.

## Evidence

| Step | Command | EXIT | Result |
|---|---|---|---|
| GREEN on HEAD (fix present) | `cmake --build build --target SessionCommandArgsTest` | 0 | `build-2.log` |
| GREEN on HEAD (fix present) | `ctest -R SessionCommandArgsTest --output-on-failure` (from `build/tests`) | 0 | `test-1.log` — 1/1 passed, `SessionCommandArgsTest` Passed |
| RED (fix reverse-applied) | `git apply -R <logs>/fix.patch` then `cmake --build build --target SessionCommandArgsTest` | 0 | `build-3.log` |
| **RED assertion** | `ctest -R SessionCommandArgsTest --output-on-failure` (from `build/tests`) | **8** | `test-2.log` — `FAIL! SessionCommandArgsTest::aWrongTypedActionFieldIsRefused()`, `Actual ...errorKind: 0`, `Expected (ControlErrorKind::InvalidArgs): 3` |
| GREEN (fix restored) | `git apply <logs>/fix.patch` then `cmake --build build --target SessionCommandArgsTest` | 0 | `build-4.log` |
| GREEN (fix restored) | `ctest -R SessionCommandArgsTest --output-on-failure` (from `build/tests`) | 0 | `test-3.log` — 1/1 passed |
| Affected family | `ctest -R "FollowActionEditorTest\|SessionFollowTest\|SessionSceneFollowTest\|SessionCommandArgsTest" --output-on-failure` (from `build/tests`) | 0 | `test-4.log` — **4 tests run, 4 passed**, 0 failed |

The RED run fails on exactly the assertion the finding describes: with the old
`isDouble`-then-`toInt` helper, `jump_to` 0.5 / 1e30 returned `errorKind 0` (Ok) where
`InvalidArgs (3)` was expected. This is the proof that the test catches the bug rather than
passing regardless of the fix.

### Follow Action test family — 4/4 passed (`test-4.log`)

| Test | Result |
|---|---|
| `FollowActionEditorTest` (#363) | Passed |
| `SessionFollowTest` (#461) | Passed |
| `SessionSceneFollowTest` (#463) | Passed |
| `SessionCommandArgsTest` (#467) | Passed |

## Ratchet gates

| Gate | Command | EXIT |
|---|---|---|
| Complexity | `bash tests/complexity-gate.sh --check` | 1 |
| File length | `bash tests/file-length-gate.sh --check` | 1 |

**Both gates fail, and this is PRE-EXISTING at HEAD, not caused by this pass.** The working tree
was byte-identical to HEAD throughout, so these failures are the branch's state, unrelated to this
finding. Per the rules I did **not** re-anchor either gate (no limit or baseline change).

I ran each gate twice: once before writing this report and once after the final edit. Both runs
gave identical exit codes (1 and 1), and `cluster-review-4ec3067be.md` appears in neither gate's
output — so adding the report changed nothing. The final post-edit runs are
`gate-complexity-2.log` / `gate-filelength-2.log`.

The reported regressions are all in files this cluster did not touch — e.g. complexity names
`dotCentreAlong@tests/src/gui/XyPadPairTest.cpp` (CCN 16),
`parseNotes@src/core/DawProjectReadTracks.cpp` (CCN 13), `lmms::adjust@src/core/ControlCommandsMixerTools.cpp`
(CCN 12); file-length names `tests/src/core/DeviceDspTest.cpp` (752), `tests/src/gui/ArrangerTimelineTest.cpp`
(689), `src/core/TakeLane.cpp` (520). `src/core/ControlCommandsSessionFollow.cpp` appears in neither
regression list — the complexity gate in fact lists
`lmms::followActionFromJson@src/core/ControlCommandsSessionFollow.cpp` under **improved**.

Logs: `gate-complexity-1.log`, `gate-filelength-1.log`.

## Files committed

- `docs/qwen-fixes-2026-10/cluster-review-4ec3067be.md` (this report, new file)
- `tests/fork-sources.txt` (registration for the new report file)

**No product or test source file was changed in this pass** — the fix landed previously in
`5784cb570`.

## Notes for the next patrol round

The defect was real and is now closed on this branch, so the same finding should not be re-raised
for `ControlCommandsSessionFollow.cpp`. Two things worth a separate ticket, both outside this
one-bug scope:

1. **The two ratchet gates are red on this branch.** Any future fix here will trip the
   pre-commit gate until those unrelated regressions are cleared.
2. **The sibling scene path.** `ControlCommandsSessionSceneFollow.cpp` is the R5.2 scene-side
   half; it was not in scope for this finding and I did not audit whether its `jump_to`-equivalent
   parsing shares `intField` or repeats the old pattern.
