FALSE-POSITIVE for the defect as written; the residual "report on load" question is for the owner, not a code defect.

# Cluster new-p17-166 - "Malformed project numbers silently become zero"

Finding: `src/core/SessionClip.cpp:181-185` (historical line; current tree, see below).
Branch: `bugs/qwen-new-p17-166`, base `639e1d23e`. No product code or test changed.

## Verdict

The finding has two parts.

1. **The pattern read (`SessionClip.cpp:181-185` in the finding) is ALREADY-FIXED.** The current tree reads `pattern` through the strict helper and restores a failed conversion as -1 (`SessionClip.cpp:182-184`). This came from commit `8233fc524` ("an unreadable pattern id restored as pattern zero") and was ported by `8242786e0`.
2. **The remaining unchecked reads (`transpose`, `detune`, `loopstart`, `looplength`, `launchmode`, `legato`, `ram`, `warp`) are FALSE-POSITIVE.** Each one has a documented default of 0 or false. A malformed value therefore becomes exactly the value the strict helper would produce. The change the finding asks for would not alter any loaded value. The contract that governs this is written in the fix commit `8242786e0`: "every other attribute falls back to its own documented default."

## Evidence (file:line, current tree)

Current `ClipSlot::restoreState` (`src/core/SessionClip.cpp`):

- `:163-165` begins with `*this = ClipSlot();`, so every field starts at its member default.
- `:167-172` states the rule: strict reads, and a value that cannot be converted falls back to the attribute's own documented default.
- `:173-194` `type` and `pattern` use `intAttribute`. This is the already-fixed part.
- `:198` `launchmode` uses `attribute(...,"0").toInt()`. Default 0 is `LaunchMode::Trigger`.
- `:204` `legato` uses `toInt() != 0`. Default 0 is false.
- `:205` `loopstart` uses `toInt()`. Default 0.
- `:206` `looplength` uses `toInt()`. Default 0.
- `:208` `transpose` uses `toInt()`. Default 0.
- `:209` `detune` uses `toInt()`. Default 0.
- `:210` `ram` uses `toInt() != 0`. Default 0 is false.
- `:211` `warp` uses `toInt() != 0`. Default 0 is false.
- `:207` `gain` uses `doubleAttribute(...,0.0)`, which is already a default-fallback read.

Member defaults (`include/SessionModel.h:202-212`): `m_launchMode = Trigger`, `m_legato = false`, `m_loopStart = 0`, `m_loopLength = 0`, `m_transpose = 0`, `m_detune = 0`, `m_warp = false`, `m_ramMode = false`.

Strict helpers (`src/core/SessionModelPrivate.h`):

- `:75-83` `intAttribute` returns false and leaves `value` untouched on absent or unparseable input.
- `:88-94` `boolAttribute` returns the given default on absent or unparseable input.

Conclusion from the above: for every unchecked field, `toInt()`-on-garbage gives 0 (Qt's documented result for a failed conversion), and the documented default is also 0 or false. Replacing `toInt()` with a strict read that falls back to the same default is therefore a no-op for every input. It cannot be proven by a RED/GREEN test, and the instruction to change only buggy behaviour rules it out.

Targeted regression coverage already exists for the cases where zero is a valid target: `tests/src/core/SessionMalformedAttributeTest.cpp:26-39` (contract) and cases at `:78-98`, `:101-116`, `:121-135`. The pattern case is covered by `SessionModelTest::anUnreadablePatternIdIsNotPatternZero` (added in `8233fc524`).

## Residual (owner decision, not fixed here)

A saved file with `transpose="bad"` loads as 0. A later save writes `transpose="0"`, so the unreadable text is not preserved. `gain` already behaves the same way (`SessionClip.cpp:207`). The finding's "report or reject" option has no channel to use: `ClipSlot::restoreState` returns `void` (`src/core/SessionClip.cpp:163`) and the session loader has no load-diagnostic sink for it. Choosing between "silent default", "warning", and "reject the whole slot" is a product design question, so I did not guess one. Rejecting the whole slot would also drop its pattern reference, which is a far larger loss than the one reported.

## Commands run and EXIT codes

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-166.r1/logs/`.

| Command | EXIT | Log |
| --- | --- | --- |
| `python3 .../qwen/feature_check.py <worktree>` | 1 (before any gate ran) | `gates-1.log` |
| `bash tests/fork-sources-gate.sh` | 0 | `gate-fork-sources.log` |
| `bash tests/all-sources-reproduce.sh` | 0 | `gate-all-sources.log` |
| `bash tests/no-upstream-regression-gate.sh` | 0 | `gate-no-upstream.log` |
| `tests/scripted/check-namespace` | 0 | `gate-check-namespace.log` |
| `bash tests/no-tautology-gate.sh` | 0 | `gate-no-tautology.log` |
| `python3 tests/doc-figures-gate.py` | 0 | `gate-doc-figures.log` |
| `bash tests/complexity-gate.sh --check` | 1 (raw; 7 pre-existing items on the train) | `gate-complexity.log` |
| `bash tests/file-length-gate.sh --check` | 1 (raw; 22 pre-existing items on the train) | `gate-file-length.log` |

`feature_check.py` exited 1 with `PermissionError` at `logs.mkdir` (`feature_check.py:71`). It tries to create `<worktree>/../<name>-feature-check`, a sibling of the worktree that is outside the write-jail. I did not change the shared script. I ran its listed commands directly, from the worktree, with the same arguments, and recorded their exit codes above.

The two ratchet gates exit 1 on items that already exist on the train. feature_check's rule is that they fail only for items NEW in files the branch changed. This branch changes no files (`git diff --name-only 639e1d23e..HEAD` is empty), so that rule passes. This is a logical result from the rule, not a run of feature_check.

ctest was not run: no product code changed.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it (exit codes).** N/A. There is no product change and no new test, because the claim is a not-real verdict (step 2 skips steps 3-4b).
- **The test is deterministic.** N/A. No test was added.
- **The fix is at the cause and complete (every caller and sibling checked).** N/A for a fix. For the claim, every unchecked read in `restoreState` is listed above (`:198`, `:204`-`:211`). Each has a 0/false default, so none needs a change.
- **No collateral damage.** No change, so no collateral. Old projects load exactly as before.
- **Contract evidence for ALREADY-FIXED / FALSE-POSITIVE (file:line or contract).** `SessionClip.cpp:167-172` and `:182-184`; `include/SessionModel.h:202-212`; commit `8242786e0` message ("every other attribute falls back to its own documented default"); commit `8233fc524` for the pattern read.
- **Gates.** Six simple gates EXIT 0. Ratchets exit 1 on pre-existing train items only; no changed files. `feature_check.py` itself cannot run in this jail (see above). `submit_check.py` runs `feature_check.py` internally, so its gates line will FAIL for the same environmental reason, not because of this branch.
- **Docs.** No canary or figure claim is made in this report, so the doc-figures gate is unaffected.
