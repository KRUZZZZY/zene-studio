ALREADY-FIXED

# Cluster new-p22-106: non-finite imported channel values

## Verdict

The product fix is already on this branch. Commit `d02589fdd5` (`fix(new-p14-70): default malformed imported channel volume and pan`) is an ancestor of `HEAD` and is the last commit that touched `src/core/ControlProjectTransplantChannels.cpp`. The change in this cluster is one regression row in `ImportTrackIoTest`, which pins that behaviour for the `nan` and `1e999` inputs.

## Code evidence

- `src/core/ControlProjectTransplantChannels.cpp:63-73`: `plainValue()` reads a channel's volume or pan.
  - The `parsed` lambda (`:65-69`) returns the fallback unless the text parses to a finite number. The guard is at `:68` (`ok && std::isfinite(value)`).
  - Attribute branch: `:70`.
  - Child-element branch: `:72`.
- Callers of `plainValue`:
  - `:84-85` writes `volume` and `pan` as attributes on the output.
  - `:114-115` sets `m_volumeModel` and `m_panModel` from the description.
- `git merge-base --is-ancestor d02589fdd5 HEAD` reports YES.
- `git log -1 -- src/core/ControlProjectTransplantChannels.cpp` reports `d02589fdd5`.

## Test

- Slot `malformedImportedChannelValuesFallBackToTheDefaults()` starts at `tests/src/core/ImportTrackIoTest.cpp:430`.
- Existing rows: `:434-438` and `:441-444` (`loud`/`sideways`, `inf`/`-inf`, `0.5`/`0.25`, and the child-element rows).
- New row: `:439-440`, `rewriteDrumsChannel(file, "nan", "1e999", false)` followed by `expectImportedChannelState(file, 1.0, 0.0)`.
- The diff adds 2 lines to `tests/src/core/ImportTrackIoTest.cpp` and changes nothing else.

## Commands and results

Run logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p22-106.r1/logs/`.

| Step | Result |
|---|---|
| `fix.patch` | 2004 bytes (`fix.patch.log` EXIT=0) |
| apply-reverse | EXIT=0 (`apply-reverse.log`) |
| build-2 | EXIT=0 (`build-2.log`) |
| test-2 (fix reverted) | EXIT=8 (`test-2.log`) |
| apply-restore | EXIT=0 (`apply-restore.log`) |
| build-3 | EXIT=0 (`build-3.log`) |
| test-3 (fix restored) | EXIT=0 (`test-3.log`) |
| test-4 | EXIT=0, "100% tests passed out of 3" (`test-4.log`) |
| `product-after.patch` | 0 bytes (`product-after.log` EXIT=0) |
| feature-check | PASS, EXIT=0 (`feature-check.log`) |

With the fix reverted, `test-2.log` shows six `FAIL!` lines for `malformedImportedChannelValuesFallBackToTheDefaults()`, from `ImportTrackIoTest.cpp:218` and `:219`. Among them are `Actual (volume): nan` and `Actual (pan): 1`, which come from the new `nan`/`1e999` row. I did not map the other four FAIL lines (volume 0, volume 2, pan -1, volume 0) to individual rows.

## Registration

- `tests/fork-sources.txt:1970`: `src/core/ControlProjectTransplantChannels.cpp`
- `tests/fork-sources.txt:2823`: `tests/src/core/ImportTrackIoTest.cpp`
- `tests/upstream-modifications.txt:52`: an existing entry for `tests/src/core/ImportTrackIoTest.cpp` ("Train 43: construct the identical document list through Qt5-compatible append calls."). This change does not touch that file.

## Self-check

1. **Reverted fail and pass exit codes.** With the fix reverted, `test-2` exits 8 (`apply-reverse` and `build-2` both exit 0). With the fix restored, `test-3` exits 0 (`apply-restore` and `build-3` both exit 0), and `test-4` exits 0 with 3 of 3 tests passing.
2. **Deterministic fixtures, no sleeps.** A grep of the test file for `sleep`, `qWait`, `QThread`, `usleep` and `msleep` found no matches. The fixtures are `QDomDocument` and `QFile` (`ImportTrackIoTest.cpp:142-147` and `:185-187`).
3. **Cause complete across both branches and both callers.** Both the attribute branch (`:70`) and the child branch (`:72`) go through the same `parsed` guard (`:68`). Both callers (`:84-85` and `:114-115`) read through `plainValue`, so a single guard covers all four paths.
4. **No collateral damage.** The product guard is unchanged on this branch: the working tree is clean at HEAD, and the product file is not in this commit's diff. The test change is 2 lines, one `rewriteDrumsChannel` call and one `expectImportedChannelState` call at `:439-440`.
5. **ALREADY-FIXED file:line evidence.** `d02589fdd5` is an ancestor of `HEAD` and the last commit touching `src/core/ControlProjectTransplantChannels.cpp`. The guard is at `src/core/ControlProjectTransplantChannels.cpp:68`.
