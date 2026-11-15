IMPLEMENTED

# Cluster report: Q050-344 Record ARA and Melodyne-style pitch-editing deferral

Branch `feat/050-Q050-344-oc`. Docs-only unit plus one hermetic docs gate. The owner decision of
2026-11-14 defers ARA2 hosting and Melodyne-style pitch editing; no 0.5.0 lane exists for them.

## Acceptance

| # | Criterion | Command | Exit / result | Log |
| --- | --- | --- | --- | --- |
| 1 | Four candidates deferred in both `docs/FEATURE-LIST-0.3.0.md` and `docs/KNOWN-LIMITATIONS.md` | `python3 tests/ara-deferral-docs-gate.py --root .` (committed docs) | exit 0, `ara-deferral-docs-gate: OK` | `logs/green-direct-2.log` |
| 1 | Gate self-test (15 cases) | `python3 tests/ara-deferral-docs-gate.py --self-test` | `15/15 cases PASS` | `logs/green-selftest-2.log` |
| 1 | CTest `AraDeferralDocs`, `AraDeferralDocsSelfTest` | ctest | `100% tests passed, 0 tests failed out of 2` | `logs/006-q050-344-p2-ctest.log` |
| 2 | No lane or support claim remains (gate sweep, checks 4 and 5). RED on pre-edit docs | gate on pre-edit docs | exit 1, `4 problem(s)` | `logs/red-direct-1.log` |
| 2 | CTest RED on pre-edit docs | ctest | `AraDeferralDocs (Failed)` | `logs/red-ctest-1.log` |
| 2 | Doc revert then restore | `git diff HEAD~1 -- docs > /tmp/q344.patch; git apply -R`; gate; `git apply`; gate | revert exit 1 (4 problems); restore exit 0 (`OK`) | `logs/revert-3.log`, `logs/restore-3.log` |

Commits: `f44ddd0095` (gate, red on current docs), `7579e79eef` (gate locators accept ARA2 headings),
`7df6f0d579` (docs deferral).

## Notes

- `data/manual/index.html` is unchanged. The manual is swept by the gate and carries no ARA claim, and
  no user-visible behaviour exists to document.
- Previous rounds failed on provider quota, not on code.
- New commands: none. Format changes: none. No audio-thread code touched;
  `AudioAnalysisTest` and `AudioAutomationToolsTest` are untouched.
- Reversibility, snapshot and A16 are unchanged because no command changed.

## Self-check

Completion check run before the commit: `logs/submit-3-pre.log` (`RESULT: FAIL - 3 item(s)`). Gate run:
`logs/feature-3-pre.log` (`RESULT: FAIL`, only `complexity` failing). Both are re-run after the commit.

- Each new test fails with the product change reverted: no product code changed, so there is nothing
  to revert. The gate's own RED is the doc revert: `logs/revert-3.log` exit 1 (4 problems), restored
  with `logs/restore-3.log` exit 0.
- Each acceptance criterion proven by a test run here: criterion 1, `AraDeferralDocs` exit 0
  (`logs/green-direct-2.log`) and `AraDeferralDocsSelfTest` 15/15 PASS (`logs/green-selftest-2.log`);
  criterion 2, the gate sweep exit 0 on committed docs and exit 1 on pre-edit docs
  (`logs/red-direct-1.log`).
- Reversibility: no command added or changed, so there is no reversibility class to declare. Each
  commit is a single revertable docs or test change.
- Audio thread: nothing added. Submit check PASS "no allocation, lock, signal or I/O added inside
  audio-path functions".
- Old projects and save/load: no project format or load/save code touched.
- Completeness: user path is the deferral text in README, the manual and `docs/FEATURE-LIST-0.3.0.md` /
  `docs/KNOWN-LIMITATIONS.md`; agent path has no `ara.*` commands, and the gate checks that no lane or
  command claim exists. Edge cases tried: a deleted input (self-test case g, exit 2), a dropped lane
  phrase (case h, exit 1), and the ARA2 heading locators (case i, exit 0).
- GUI path and GUI test: not applicable. Docs-only unit with no GUI action; the GUI and command-surface
  WARN lines in the submit check are expected for that reason.
- User manual WARN: `data/manual/index.html` is intentionally unchanged (see Notes).
- Qt 5.15 / MSVC / macOS portability: no C++ or Qt code changed. The gate is Python stdlib file reads;
  the submit check's Windows-path PASS line covers the path check.
- Complexity FAIL (`feature_check.py`): new functions `check` (CCN 13) and `check_public` (CCN 12) in
  `tests/ara-deferral-docs-gate.py`. This is in the test helper, not docs or registration, so it is left
  unfixed in this unit. Splitting those two functions is the one-step fix if the lead wants it.
