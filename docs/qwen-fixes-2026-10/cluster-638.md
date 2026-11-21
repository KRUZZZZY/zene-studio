FIXED

# Cluster 638: section gap absorption in the container deriver

Verdict: FIXED. The deriver accepted non-whitespace text between an indexed section and the next `<`, and filed that text inside the section's entry. It now refuses such text by name, and still accepts ordinary whitespace gaps.

## Cause

`src/core/ProjectContainerEntries.cpp:379-414` (`cutEntries`). Each index range was extended from the section's end to the next `<`, so the entry carries the element plus the bytes that follow it. The old code did this with no check on the bytes in between. Any text there (for example `extra` in `<trackcontainer/>extra<tempo-map v="1"/>`) was absorbed into the section's entry. The post-cut skeleton proof (`locateContentInterior`) only sees the skeleton, so it could not catch those bytes.

## Fix

- `src/core/ProjectContainerEntries.cpp:70-76`: `isXmlWhitespaceByte()`. Byte-level XML whitespace (space, tab, CR, LF). No locale and no QChar classification.
- `src/core/ProjectContainerEntries.cpp:401-412`, in `cutEntries`:
  - The existing no-`<` refusal (394-400) is unchanged.
  - The new gap guard (401-412) scans every byte between a section's end and the next `<`. Any non-whitespace byte refuses the whole derive with an error that names the section and says "not whitespace". The destination entries are left untouched.
- `src/core/ProjectContainerEntries.cpp:363-378`: doc comment only, updated to state the whitespace rule.
- `include/ProjectContainer.h:223-253`: `deriveContainerEntries` contract, comment only. It now states that the bytes from a section's end to the next `<` must be XML whitespace, and lists "a section followed by non-whitespace text before the next `<`" in REFUSALS. This matches the gap guard at 401-412.

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
| No-`<` exhaustion refusal | Existing guard at 394-400, unchanged | No dedicated test slot found in the test file (see Limits) |
| Reconfiguration | N/A | Not applicable |
| Audio thread | N/A | Not applicable: this is offline serialization |
| Byte-level XML whitespace (portability) | `isXmlWhitespaceByte` at 70-76 | Compares bytes, not QChars, so CRLF and tab behave the same on every platform |

## Evidence

The final-state logs under `~/.config/kads/zene/state/runs/638.r1/logs/` each end with an `EXIT=` line for the command they name. Run-3 and run-4 logs (`test-3-*.log`, `test-4-family.log`, `build-4-zene.log`, `gates-4.log`) are kept for their counts only. They do not end in EXIT lines and are not cited as exit-code evidence.

- Red (old code): `test-5b-red.log` (`ctest -R ProjectContainerEntriesTest -V`): `Totals: 19 passed, 2 failed`, failing at the two refusal slots. EXIT=8. Preceding build `build-5b-red.log`: EXIT=0.
- Green (fixed code): `test-5b-green.log` (same command): `Totals: 21 passed, 0 failed`. EXIT=0. Preceding build `build-5b-green.log` (`cmake --build build --target zene ProjectContainerEntriesTest`): EXIT=0.
- Mutant (temporary `isXmlWhitespaceByte` → `return false`): `test-5c-mutant.log`: `Totals: 11 passed, 10 failed`, including `derivingAcceptsWhitespaceGapsBetweenIndexedSections`, which fails with the guard's "not whitespace" message. EXIT=8. The product file was then restored with `cp`, `cmp` EXIT=0, and rebuilt. Green rerun `test-5c-green.log`: `Totals: 21 passed, 0 failed`. EXIT=0.
- Header rebuild: `build-5b-header.log` (`cmake --build build --target zene` plus the ProjectContainer, RevisionTimeline, DataFileContainerRecovery and DocumentIndex/DocumentSections/PartialLoad targets): EXIT=0.
- Family: `test-5b-family.log` (`ctest -R "ProjectContainerEntriesTest|ProjectContainerTest|ProjectContainerSaveTest|ProjectContainerSaveAsGuiTest|DocumentIndexTest|DocumentSectionsTest|PartialLoadTest" --output-on-failure`): `100% tests passed out of 7`, with `ProjectContainerSaveTest` passing. EXIT=0.
- Gates: `gates-5b.log` (`python3 feature_check.py <tree>`): `RESULT: PASS`. EXIT=0.
- Whitespace: `diffcheck-5b.log` (`git diff --check`): empty output. EXIT=0.

## Collateral

- Code-only diff stat: `git diff --numstat 5f1dda45f0^ HEAD -- include/ProjectContainer.h src/core/ProjectContainerEntries.cpp tests/src/core/ProjectContainerEntriesTest.cpp`: three files, 88 insertions, 7 deletions (header 10/6; product 26/1, where the deleted line is the old unguarded cut; test 52/0). This report itself is documentation-only and is not counted in that stat.
- Product changes are in `deriveContainerEntries()`'s path through `cutEntries()`, plus the new `isXmlWhitespaceByte()` helper. The hunks are at lines 70-76 (helper), 363-378 (comment) and 401-412 (gap guard).
- `reassembleContainerDocument()` (line 572) and `reduceDocumentSections()` have no hunks and are untouched.
- Existing v2 projects are read by the reassembler, which is unchanged. The fix changes only what the deriver accepts. The v2 load path is covered by `ProjectContainerSaveTest` in the family run (`test-5b-family.log`: 7/7, EXIT=0, with `ProjectContainerSaveTest` passing). That test file has the v2 load/reassembly slots `legacyAndV2FixturesLoadAndResaveToTheSameProject` and `theContainerReassemblesToTheLegacyDocument`. `theRoundTripSurvivesTheRealContainerPair` is the closest container round-trip check.

## Limits

- No test slot exercises the no-`<` exhaustion refusal at 394-400. That path is unchanged, but it is not covered by a test in this change.
- The v2 load path is covered by the passing `ProjectContainerSaveTest` family run (`test-5b-family.log`), not by a new test in this change.

## Self-check

1. **Regression test fails with the change reverted and passes with it.** Yes, by result counts. `test-3-red.log` (old code): 19 passed, 2 failed, and the two refusal slots are the failures. `test-3-green-final.log` (fixed code): 21 passed, 0 failed. Final-state exit codes: red EXIT=8 (`test-5b-red.log`) and green EXIT=0 (`test-5b-green.log`); see Evidence.
2. **Deterministic.** The three new slots use only in-memory `QByteArray`/`QString` inputs built by `documentWith()`. They use no clock, sleep, thread or file I/O. The seam is the `document`, `contentElementName` and `entries` arguments of `deriveContainerEntries`. The file-based test `aFailedDocumentReadIsNamedAsARead` is pre-existing and unchanged.
3. **Fix at the cause; callers and siblings checked.** The cause is the unguarded extension in `cutEntries()`, and the fix is at its site (`ProjectContainerEntries.cpp:401-412`). In `src/`, `deriveContainerEntries` has one caller, at `ProjectContainerEntries.cpp:632`, inside the round-trip function that then calls `reassembleContainerDocument` (649). The range scanner that `DocumentIndex.h:259` refers to is unchanged. The public header contract (`include/ProjectContainer.h:223-253`) now states the whitespace-only gap rule and lists the new refusal in REFUSALS, so it matches the guard at 401-412.
4. **No collateral damage.** `reassembleContainerDocument()` and `reduceDocumentSections()` are unchanged. The guard refuses only non-whitespace bytes in the gaps. The real container pair round-trips in `theRoundTripSurvivesTheRealContainerPair` (green). Existing v2 projects load through the unchanged reassembler, and that load path is covered by the passing `ProjectContainerSaveTest` slots `legacyAndV2FixturesLoadAndResaveToTheSameProject` and `theContainerReassemblesToTheLegacyDocument` (`test-5b-family.log`, EXIT=0). The no-`<` refusal is unchanged and has no dedicated test slot.
5. **Verdict is FIXED, not ALREADY-FIXED or FALSE-POSITIVE.** The red run (item 1) shows the old code accepting text between sections, which is the reported defect.
6. **Sibling commands.** Not applicable. This change edits no command file. The `app.*` and `arrangement.*` list printed by `submit_check` comes from the branch diff against master (10256 files), not from this commit. The fix commit (`5f1dda45f0`) touches `ProjectContainerEntries.cpp`, its test and this report. The header contract file `include/ProjectContainer.h` changed in `6d1893cd06` (comment-only contract edit) and is one of the three files in the code-only stat.
