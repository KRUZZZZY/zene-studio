FALSE-POSITIVE

# Cluster new-p23-88 — DrumSynth no-filter path (Ot uninitialised, DF[1200] overrun)

Finding (Luna patrol, `src/core/DrumSynth.cpp:~652`): "`Ot` is never initialised before the overtone/filter
handling, and when `MainFilter <= 0` the else branch executes `DF[t - tpos] += Ot` once with `t` at `tplus+1`
instead of looping over the block … writes DF[1200] out of bounds."

Verdict: **FALSE-POSITIVE**. Both halves of the claim are contradicted by the code on branch
`bugs/qwen-new-p23-88` (base `050/m0-train-17`, `639e1d23e`). No product file is changed.

## Evidence (file:line in this worktree)

1. **`Ot` is initialised.** `src/core/DrumSynth.cpp:407`:
   `float Ophi1, Ophi2, OF1, OF2, OL, Ot = 0 /*PG: init */, OBal1, OBal2, ODrive;`
   It is a function-local in `GetDSFileSamples` (starts line 384), so every call starts from 0.
   `Ot` is assigned only inside the overtone block: `src/core/DrumSynth.cpp:873` (`Ot = 0.0f;` every sample),
   then 877–906. Nothing else writes it. So with `OON == 0` (`OON = chkOn[2]`, line 533) it stays 0 for the whole
   call, and with `OON == 1` it is reset and re-derived on every sample before the filter/else code reads it at
   911–968. There is no read of an unset `Ot`. Every `OMode` case (switch 874–908) also leaves `Ot` assigned,
   including the default fall-through, which keeps the 873 value.

2. **The no-filter branch loops over the block; it is not a single write.** The per-sample loop is
   `src/core/DrumSynth.cpp:816` `for (t = tpos; t <= tplus; t++) {` and it closes at line 970 `}`.
   The if / `else if (MainFilter == 2)` / `else` chain at 911 / 938 / 966 sits inside that loop, so the
   `else` at 966–969 (`DF[t - tpos] = DF[t - tpos] + Ot;`) runs once for every `t` in the block.
   The `else` is entered for `MainFilter <= 0` (and any other value), as the finding says, and it does iterate.

3. **No out-of-bounds write.** `DF` is `thread_local float DF[1200];` (`src/core/DrumSynth.cpp:86`).
   Each block sets `tplus = tpos + 1199;` (line 687) and advances `tpos = tpos + 1200;` (line 1027), so
   `t - tpos` ranges over 0..1199, which is in bounds. The `tplus+1` index the finding describes never occurs:
   `t <= tplus` ends the loop at `t - tpos == 1199`.

4. **What is stale.** The comment `// PG: Ot is uninitialized` at `src/core/DrumSynth.cpp:965` predates the
   initialiser. `git blame` puts both the comment (965) and the `Ot = 0 /*PG: init */` initialiser (407) in commit
   `26646c653` (2024-06-22, Monospace-V, initial import), so the initialiser was present from the start. The
   comment is misleading but harmless; it is not a defect and I did not edit it (a comment-only product change
   would be unrelated churn for a false-positive).

Intended contract: the DF buffer is one 1200-sample block; the per-sample loop is the no-filter path; `Ot` is a
function-scope value that the overtone block always sets before use. The code matches that contract.

## Why no regression test

Step 2 of the procedure: a not-real verdict skips the RED/GREEN cycle, because there is no buggy behaviour to
reproduce and a test that passes both with and without a change is not regression proof. No product files
changed, so step 4b (fix patch round-trip) does not apply.

## Commands run (LOG FOLDER: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-88.r1/logs`)

| Step | Command | EXIT | Log |
| --- | --- | --- | --- |
| Evidence (grep of 407/816/965–968/86/687/1027) | `grep -n …` and `sed -n` on `src/core/DrumSynth.cpp` | 0 | `evidence-1.log` |
| Gate script as written | `python3 …/qwen/feature_check.py <worktree>` | 1 | `gate-1.log` (crashed: `PermissionError` creating `…/zene-qwen-new-p23-88-feature-check`, which is outside the write-jail; the script writes its logs to `<worktree>/..`) |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 | `gate-2-fork-sources.log` |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 | `gate-2-all-sources.log` |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 | `gate-2-no-upstream.log` |
| check-namespace | `tests/scripted/check-namespace` | 0 | `gate-2-check-namespace.log` |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 | `gate-2-no-tautology.log` |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 | `gate-2-doc-figures.log` |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 | `gate-2-complexity.log` (7 REGRESSION lines, all already in the base listing `gate-baselines/639e1d23e….json`) |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 | `gate-2-file-length.log` (22 REGRESSION lines, all already in the base listing) |
| ratchet compare | python compare of the REGRESSION keys against the cached base JSON | 0 | `gate-2-ratchet-compare.log` (`complexity … not in base 0`, `file-length … not in base 0`) |

The two ratchet gates exit 1 on the unchanged train base as well. Under the feature_check rule ("fail only on items NEW
in your files"), they pass: the only file this lane touches is the report below, which has no complexity or
file-length items.

I did not build or run `DrumSynthTest`/`DrumSynthLevelTest`. The verdict rests on the code evidence above, not on a
runtime observation, and the test binaries are not built in this worktree.

## Self-check

- **Regression test fails with the change reverted and passes with it:** not applicable. No product change, and the
  verdict is FALSE-POSITIVE, so there is no buggy behaviour for a test to reproduce. No test was added. Step 4b did
  not run.
- **Test is deterministic:** not applicable (no test added).
- **Fix is at the cause and complete:** not applicable (no fix). The sibling checks are recorded in evidence items 1–3:
  every write to `Ot` (873–906), every `DF` index (`t - tpos`, 0..1199), and the no-filter loop (816–970).
- **No collateral damage:** not applicable (no product change; saved projects and the `DrumSynth` output path are untouched).
- **Verdict evidence (FALSE-POSITIVE):** `src/core/DrumSynth.cpp:407` (`Ot = 0` initialiser), `:816`–`:970` (no-filter
  `else` is inside the per-sample loop), `:86` / `:687` / `:1027` (`DF[1200]`, `t - tpos` in range). Evidence items 1–4 above.
- **Gates:** `feature_check.py` could not run in this lane. It creates `<worktree>/../zene-qwen-new-p23-88-feature-check`
  (`gate-1.log`, `PermissionError`), which is outside the write-jail. I ran each of its gate commands directly in the
  worktree (table above): six simple gates EXIT 0, two ratchets EXIT 1 with every REGRESSION line already in the base
  listing (0 new). The `gates (feature_check.py)` item in `submit_check.py` therefore stays red for a reason outside the code.
