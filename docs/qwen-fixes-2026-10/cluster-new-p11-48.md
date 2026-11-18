# cluster new-p11-48 — Snapping an empty clip reports a selection error

**Verdict: FIXED**

## Finding
`scale.snap_notes` on a clip with no notes, using the default `scope: "clip"`,
returned a selection-specific refusal — "clip-N has 0 notes and none is selected,
so scope 'selection' would edit nothing" — blaming a scope that was never asked
for.

## Evidence
- `src/core/ControlCommandsScaleEdit.cpp:87-92` (pre-fix): a single
  `if (notes.empty())` branch returned the selection message regardless of the
  requested `scope`.
- The distinction already exists in `note.transform`:
  `src/core/ControlCommandsNoteTransform.cpp:105-110` refuses an empty clip with
  "%1 has no notes: there is nothing to transform" before the selection check.

## Fix
`src/core/ControlCommandsScaleEdit.cpp` — inside `if (notes.empty())`, check
`clip->notes().empty()` first and report
"%1 has no notes: there is nothing to snap"; the selection message is kept for
the clip-has-notes-but-selection-is-empty case (behaviour preserved). No new
files: `src/core/ControlCommandsScaleEdit.cpp` is already registered in
`tests/fork-sources.txt` (line 1270) and the test file in `tests/all-sources.txt`
(line 2380), so no manifest changes were needed.

## Regression
New slot `snapOnAnEmptyClipNamesTheEmptyClipAsTheReason()` in
`tests/src/core/ControlNoteScaleVerbsTest.cpp` (after W9): sets the scale
context, snaps an empty clip with the default scope, and asserts
`Refused` + the message names "no notes" + does not blame "none is selected".

## Commands and exit codes
Logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-48.r1/logs`

| Step | Command (run from) | EXIT |
|---|---|---|
| RED build | `cmake --build build --target ControlNoteScaleVerbsTest > LOGS/build-1.log 2>&1` (repo root) | 0 |
| RED run | `ctest -R ControlNoteScaleVerbsTest --output-on-failure > LOGS/test-1.log 2>&1` (build/tests) | 8 |
| GREEN build | `cmake --build build --target ControlNoteScaleVerbsTest > LOGS/build-2.log 2>&1` (repo root) | 0 |
| GREEN run | `ctest -R ControlNoteScaleVerbsTest --output-on-failure > LOGS/test-2.log 2>&1` (build/tests) | 0 |
| Proof: strip fix | `git diff -- src/core/ControlCommandsScaleEdit.cpp > LOGS/fix.patch && git apply -R LOGS/fix.patch` (repo root) | 0 |
| Proof rebuild | `cmake --build build --target ControlNoteScaleVerbsTest > LOGS/build-3.log 2>&1` (repo root) | 0 |
| Proof run (must FAIL) | `ctest -R ControlNoteScaleVerbsTest --output-on-failure > LOGS/test-3.log 2>&1` (build/tests) | 8 |
| Proof: restore fix | `git apply LOGS/fix.patch` (repo root) | 0 |
| Proof rebuild | `cmake --build build --target ControlNoteScaleVerbsTest > LOGS/build-4.log 2>&1` (repo root) | 0 |
| Proof run (must PASS) | `ctest -R ControlNoteScaleVerbsTest --output-on-failure > LOGS/test-4.log 2>&1` (build/tests) | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check` | 0 |

RED assertion (test-1.log):
`FAIL! : ControlNoteScaleVerbsTest::snapOnAnEmptyClipNamesTheEmptyClipAsTheReason() 'emptyClip.errorMessage.contains(QStringLiteral("no notes"))' returned FALSE.`

ctest family: 1 test (`ControlNoteScaleVerbsTest`, #45); 15 slots executed
(14 PASS + 1 FAIL in RED; 15 PASS in GREEN — "100% tests passed, 0 tests failed
out of 1").

## Files committed
- `src/core/ControlCommandsScaleEdit.cpp` (the fix)
- `tests/src/core/ControlNoteScaleVerbsTest.cpp` (the regression slot)
- `docs/qwen-fixes-2026-10/cluster-new-p11-48.md` (this report)
