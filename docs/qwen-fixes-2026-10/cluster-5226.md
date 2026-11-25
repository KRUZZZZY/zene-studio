FIXED

# Cluster 5226 - XML character offsets used as byte offsets (ProjectContainerEntries)

## Verdict evidence
REAL. `walkToContentStart()` in `src/core/ProjectContainerEntries.cpp` took
`xml.characterOffset()` (UTF-16 units) and passed it straight to
`bytes.lastIndexOf( '<', afterTag - 1 )` over the UTF-8 `QByteArray`. With non-ASCII
text before `<song>` the offset lands too early, `lastIndexOf` selects an earlier `<`,
and the content start is misplaced. The sibling scanner `bytePositionOf()` in
`src/core/DocumentIndex.cpp:389` already converts character to byte offsets for the same
reason; this caller never did.

Observed failure without the fix (`test-red.log`, `test-red2.log`): `deriveContainerEntries`
returned FALSE with "the <song> element is empty, so there is no interior to place
sections into" (test line 183).

## Fix
`src/core/ProjectContainerEntries.cpp`: new anonymous-namespace helper
`bytePositionOfCharacter()` (counts UTF-8 lead bytes, a 4-byte lead = 2 UTF-16 units,
then skips the continuation bytes of the last character), used in `walkToContentStart()`.
Load/save path only; no audio thread, no commands, no undo changes. The other
`indexOf( '<' )` calls in the file are pure byte searches and need no conversion.
ASCII documents give an identical offset, so existing projects behave as before.
The file is already registered in tests/fork-sources.txt (fork-NEW), so no registration
change was needed.

## Regression test
`tests/src/core/ProjectContainerEntriesTest.cpp`, slot
`nonAsciiBeforeTheContentElementDoesNotMisplaceTheSplice`: 40 two-byte characters and
three 4-byte emoji in the `creator` attribute before `<song>`; derives, checks the
`tempo-map` entry, reassembles and requires a byte-identical document. Deterministic.

## Commands (logs in /home/kruzzzzy/.config/kads/zene/state/runs/5226.r1/logs)
- RED build `cmake --build build --target zene ProjectContainerEntriesTest -j4` (build-red.log): EXIT=0
- RED test `cd build/tests && ctest -R ProjectContainerEntriesTest --output-on-failure` (test-red.log): EXIT=8 (the new slot fails; 21 passed, 1 failed)
- GREEN build (build-green.log): EXIT=0
- Family build `cmake --build build --target DocumentIndexTest DocumentSectionsTest ProjectContainerTest ProjectContainerEntriesTest -j4` (build-family.log): EXIT=0
- Family test `ctest -R 'ProjectContainerEntriesTest|DocumentIndexTest|DocumentSectionsTest|ProjectContainerTest'` (test-family.log): EXIT=0, 4/4 passed
- 4b revert: `git diff -- src/core/ProjectContainerEntries.cpp > .../fix.patch && git apply -R .../fix.patch`: EXIT=0; rebuild (build-red2.log) EXIT=0; `ctest -R ProjectContainerEntriesTest` (test-red2.log): EXIT=8 (FAIL, as required)
- 4b re-apply: `git apply .../fix.patch`: EXIT=0; rebuild (build-green2.log) EXIT=0; `ctest -R ProjectContainerEntriesTest` (test-green2.log): EXIT=0, 1/1 passed
- Gates `python3 .../feature_check.py <worktree>` (gates.log): EXIT=0, `RESULT: PASS`

Not verified: a Qt 5 / MSVC build (the helper uses only QByteArray and plain integer
arithmetic, nothing Qt-6-only). TSan not run (no threads touched).

Delegation: Haiku ran the RED build/test, the GREEN and family builds/tests, the 4b revert/re-apply proof and the gates, and reported exit codes; I checked the log tails and git diff myself.

## Self-check
(see below, filled in after submit_check)

## Other findings
None.
