FIXED

# Cluster 638: section gap absorption in the container deriver

Verdict: FIXED. The deriver accepted non-whitespace text between an indexed section and the next `<`, and filed that text inside the section's entry. It now refuses such text by name, and still accepts ordinary whitespace gaps.

## Cause

`src/core/ProjectContainerEntries.cpp:366-411` (`cutEntries`). Each index range was extended from the section's end to the next `<`, so the entry carries the element plus the bytes that follow it. The old code did this with no check on the bytes in between. Any text there (for example `extra` in `<trackcontainer/>extra<tempo-map v="1"/>`) was absorbed into the section's entry. The post-cut skeleton proof (`locateContentInterior`) only sees the skeleton, so it could not catch those bytes.

## Fix

- `src/core/ProjectContainerEntries.cpp:70-76`: `isXmlWhitespaceByte()`. Byte-level XML whitespace (space, tab, CR, LF). No locale and no QChar classification.
- `src/core/ProjectContainerEntries.cpp:398-411`, in `cutEntries`:
  - The existing no-`<` refusal (394-400) is unchanged.
  - The new gap guard (401-412) scans every byte between a section's end and the next `<`. Any non-whitespace byte refuses the whole derive with an error that names the section and says "not whitespace". The destination entries are left untouched.
- `src/core/ProjectContainerEntries.cpp:363-378`: doc comment only, updated to state the whitespace rule.

## Tests

`tests/src/core/ProjectContainerEntriesTest.cpp:349-399`, three new slots:

- `derivingAcceptsWhitespaceGapsBetweenIndexedSections` (349-362): space, tab and CRLF gaps derive into 3 entries.
- `derivingRefusesTextBetweenIndexedSections` (364-381): `extra` between sections is refused by name; entries untouched.
- `derivingRefusesTextAfterLastIndexedSection` (383-399): `tail` after the last section is refused by name; entries untouched.

Test matrix:

| Case | Covered by | Result |
|---|---|---|
| Normal whitespace gaps (space/tab/CRLF) | `derivingAcceptsWhitespaceGapsBetweenIndexedSections` | Passes in red and green; fails in mutant (see Evidence) |
| Non-whitespace between sections | `derivingRefusesTextBetweenIndexedSections` | Fails in red (old code accepted it); passes in green |
| Non-whitespace after last section | `derivingRefusesTextAfterLastIndexedSection` | Fails in red; passes in green |
| No-`<` exhaustion refusal | Existing guard at 394-400, unchanged | No dedicated test slot found in the test file (see Evidence gaps) |
| Reconfiguration | N/A | Not applicable |
| Audio thread | N/A | Not applicable: this is offline serialization |
| Byte-level XML whitespace (portability) | `isXmlWhitespaceByte` at 70-76 | Compares bytes, not QChars, so CRLF and tab behave the same on every platform |

## Evidence

The saved logs record ctest output lines and build/gate result lines. They do not record ctest or build exit codes. The EXIT values below are therefore not cited as log facts.

- `test-3-red.log` (ctest, old code): `Totals: 19 passed, 2 failed`. FAIL at `derivingRefusesTextBetweenIndexedSections` and `derivingRefusesTextAfterLastIndexedSection`. `derivingAcceptsWhitespaceGapsBetweenIndexedSections` passed. Exit code: not recorded in the log.
- `test-3-green-final.log` (ctest, fixed code): `Totals: 21 passed, 0 failed`; `100% tests passed out of 1`. Exit code: not recorded in the log.
- `test-3-mutant.log` (ctest, mutant): `Totals: 11 passed, 10 failed`. `derivingAcceptsWhitespaceGapsBetweenIndexedSections` fails with the guard's "not whitespace" message. Exit code: not recorded in the log.
- `test-4-family.log` (ctest, family run): `100% tests passed out of 7`. Exit code: not recorded in the log.
- `build-4-zene.log`: ends with `[100%] Built target zene`. Exit code: not recorded in the log.
- `gates-4.log`: seven gate lines `PASS`, then `RESULT: PASS`. Exit code: not recorded in the log.
- `diffcheck-4.log`: 0 bytes. Replaced by `diffcheck-5.log`, run for this report: `git diff --check` → `EXIT=0`, empty output.

## Collateral

- Diff stat: 2 files changed, 78 insertions, 1 deletion (the deleted line is the old unguarded cut).
- Product changes are in `deriveContainerEntries()`'s path through `cutEntries()`, plus the new `isXmlWhitespaceByte()` helper. The hunks are at lines 70, 363-378 (comment) and 401-412.
- `reassembleContainerDocument()` (line 572) and `reduceDocumentSections()` have no hunks and are untouched.
- Existing v2 projects are read by the reassembler, which is unchanged. The fix changes only what the deriver accepts. The closest load-path evidence is `theRoundTripSurvivesTheRealContainerPair`, which passes in green. No v2 project was loaded directly.

## Evidence gaps

- ctest exit codes are not recorded in any saved log. The packet's EXIT=8 (red), EXIT=0 (green), EXIT=8 (mutant) and EXIT=0 (family) are unverified.
- `diffcheck-4.log` is empty. The clean result is in `diffcheck-5.log`.
- `build-4-zene.log` and `gates-4.log` have no exit-code line.
- No test slot exercises the no-`<` exhaustion refusal at 394-400. That path is unchanged, but it is not covered by a test in this change.

## Self-check
