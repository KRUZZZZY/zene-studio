# Cluster new-p12-270 — Empty automation patterns are shifted as if they target base notes

**Verdict: FIXED**

## Finding

`src/core/UpgradeExtendedNoteRange.cpp` — in `fixAutomationTracks`, when an automation
track contains at least one pattern that targets a base note and no pattern that targets
anything else, the "keep and fix the track" branch called `fixAutomationPattern` on
**every** pattern of the track, including patterns whose `analyzeAutomationPattern`
reports no targets at all. Targetless patterns have no base-note automation, so no value
was changed for them; shifting them by +12 corrupts stored points if the pattern is ever
linked to a target later. The duplicate/split path elsewhere explicitly preserves
targetless patterns (the comment at `src/core/UpgradeExtendedNoteRange.cpp:311-313`
pre-fix: "these might also be automation patterns without any targets. We will keep
these"), so the simple branch was inconsistent with it.

- Trigger: an automation track with one base-note-targeted pattern and one targetless
  pattern containing `<time value="3"/>`. Loading changed `3` to `15`.

## Evidence (file:line, pre-fix)

- `src/core/UpgradeExtendedNoteRange.cpp:271-277` (pre-fix) — loop over all
  `automationpattern` elements calling `fixAutomationPattern(...)` unconditionally.
- `src/core/UpgradeExtendedNoteRange.cpp:58-83` — `analyzeAutomationPattern` returns
  `hasBaseNoteAutomations == false` for a targetless pattern.
- `src/core/UpgradeExtendedNoteRange.cpp:303-307` (pre-fix) — the duplication path leaves
  targetless patterns alone, the intended behaviour.

## Fix

Extracted `fixBaseNoteOnlyAutomationPatterns(track, automatedBaseNoteIds)` (`src/core/UpgradeExtendedNoteRange.cpp:174-189`):
it walks the automation patterns and calls `fixAutomationPattern` only when
`analysis.hasBaseNoteAutomations` is true. The simple branch now calls it instead of the
unconditional loop. Extracting the helper (rather than inlining an extra `if`) keeps the
whole-tree complexity ratchet clean: `fixAutomationTracks` CCN went 17 -> 16, the new
helper is CCN 3. No header changed.

## Regression tests

`tests/src/core/UpgradeExtendedNoteRangeTest.cpp` — new helper builds a song with an
automated element-based base note (id `7`) plus an automation track holding one pattern
that targets it and one targetless pattern; two new slots:

- `baseNoteAutomationIsStillShifted()` — base-note pattern `10` -> `22` (guards the fix
  from over-correcting).
- `targetlessAutomationValueIsPreserved()` — targetless pattern `3` stays `3`
  (pre-fix it became `15`).

## Evidence table

| Step | Command (cwd) | Exit |
| --- | --- | --- |
| RED before fix | `ctest -R '^UpgradeExtendedNoteRangeTest$' --output-on-failure` (build/tests) | 8 — `Actual (upgradedTargetlessValue): 15 / Expected (3): 3` |
| Build test target | `cmake --build build --target UpgradeExtendedNoteRangeTest` (root) | 0 |
| GREEN after fix | `ctest -R '^UpgradeExtendedNoteRangeTest$' --output-on-failure` (build/tests) | 0 — `100% tests passed out of 1` |
| Proof: revert fix | `git apply -R .../logs/fix.patch` (root) | 0 |
| Proof: rebuild | `cmake --build build --target UpgradeExtendedNoteRangeTest` (root) | 0 |
| Proof: test must FAIL | `ctest -R '^UpgradeExtendedNoteRangeTest$' --output-on-failure` (build/tests) | 8 |
| Proof: re-apply fix | `git apply .../logs/fix.patch` (root) | 0 |
| Proof: rebuild | `cmake --build build --target UpgradeExtendedNoteRangeTest` (root) | 0 |
| Proof: test must PASS | `ctest -R '^UpgradeExtendedNoteRangeTest$' --output-on-failure` (build/tests) | 0 |
| Full app | `cmake --build build --target zene` (root) | 0 |

Affected-family test count: 1 ctest test (`UpgradeExtendedNoteRangeTest`, 6 test slots
plus `initTestCase`/`cleanupTestCase`), `100% passed`.

## Gates

| Gate | Command | Exit |
| --- | --- | --- |
| Complexity (fork scope, required) | `bash tests/complexity-gate.sh --check` | 1 |
| File length (required) | `bash tests/file-length-gate.sh --check` | 1 |

Both gates already fail identically on a pristine `HEAD` archive
(`/tmp/opencode/pristine-270`, `git archive HEAD`); the `REGRESSION` line sets are
byte-identical between the pristine tree and the fixed tree (`diff` empty) and neither
lists `src/core/UpgradeExtendedNoteRange.cpp` or its test. These are pre-existing branch
baseline drift, not caused by this fix, and the baselines were deliberately not
re-anchored. Whole-tree complexity scope (`GATE_SCOPE=all`) likewise shows an identical
`REGRESSION` set; with the helper extraction `fixAutomationTracks` CCN dropped 17 -> 16
so it introduces no new complexity entry.

## Files committed

- `src/core/UpgradeExtendedNoteRange.cpp`
- `tests/src/core/UpgradeExtendedNoteRangeTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p12-270.md`
