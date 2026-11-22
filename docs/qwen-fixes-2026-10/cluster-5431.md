FIXED

# Cluster 5431: invalid-but-well-formed version XML erased the current clips

## Decision: REAL
Before the fix, `TrackVersions::replaceClips` (src/core/TrackVersions.cpp) only checked that the XML parsed,
then ran `track->deleteClips()` and turned every child of the document root into a clip. A stored version of
`<other/>` therefore deleted all clips and returned true. `switchVersion`
(src/core/ControlCommandsTrackVersions.cpp) had the same hole: its pre-check was only `QDomDocument().setContent`,
and it ignored `replaceClips`' return value.
The RED run below reproduces it.

## Fix
- `TrackVersions::isValidClipsXml` (new, include/TrackVersions.h): the XML parses, the root is `<clips>`, and every
  child element is one of the four clip node names (midiclip, sampleclip, patternclip, automationclip), the names
  `clipsOf` writes. `<clips/>` stays valid: it is a version with no clips.
- `replaceClips` validates before `deleteClips`, so an invalid version leaves the track untouched.
- `switchVersion` uses the same validator before the undo step, so nothing is recorded for a refused switch (Refused error, as before for unparseable XML).
- No control command added. No audio-thread code touched. Parsing and validation run on the control thread, before
  the existing `requestChangesGuard`.

## Test
`ArrangeToolsTest::invalidStoredVersionXmlLeavesTheTrackUntouched` (tests/src/core/ArrangeToolsTestParts1.h).
It covers a wrong root, a wrong root with a clip child, a non-clip child, and a mixed list. It also runs the
`track.version_switch` command on a stored `<other/>` (Refused, clips and active version unchanged), and checks that
`<clips/>` is still accepted.

## Commands (logs in /home/kruzzzzy/.config/kads/zene/state/runs/5431.r1/logs)
- RED, test only, no fix: `cmake --build build --target ArrangeToolsTest` (build-1.log) EXIT=0; `cd build/tests && ctest -R ArrangeToolsTest` (test-1-red.log) EXIT=8. Failure: `'!TrackVersions::replaceClips(track, xml)' returned FALSE. (<other/>)`.
- GREEN: `cmake --build build --target zene ArrangeToolsTest` (build-2.log) EXIT=0; ctest -R ArrangeToolsTest (test-2-green.log) EXIT=0, 1/1 passed.
- 4b: `git diff -- include/TrackVersions.h src/core/TrackVersions.cpp src/core/ControlCommandsTrackVersions.cpp > logs/fix.patch && git apply -R logs/fix.patch` EXIT=0; rebuild (build-3-reverted.log) EXIT=0; ctest (test-3-red-reverted.log) EXIT=8 with the same failure. `git apply logs/fix.patch` EXIT=0; rebuild (build-4-reapplied.log) EXIT=0; ctest (test-4-green-reapplied.log) EXIT=0.
- Gates: `feature_check.py <worktree>` (gates-1.log) EXIT=0, RESULT: PASS (all 8 PASS). All changed files were already in fork-sources.txt; none are inherited files.

## Notes
- The only `ctest -R` family member that includes the changed header is ArrangeToolsTest. I did not run the full ctest suite.
- The valid-name set is the four current clip classes. Track's loader also accepts four legacy tag spellings, but `clipsOf` only writes current names, so stored versions never contain them.

## Other findings
- `replaceClips` dereferences the result of `track->createClip` without a null check. A track class that owns no clips (TrackFolder) returns null. Not touched here.
