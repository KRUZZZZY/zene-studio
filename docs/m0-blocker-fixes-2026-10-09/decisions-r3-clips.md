# M0 blocker fixes: linked clips and contained crossfades

Date: 2026-10-09

Branch: `bugs/m0-decisions-r3-clips`

Design: [decisions-r3-clips.md](../m0-design/decisions-r3-clips.md)

| Item | Verdict | Fix commit | Regression tests | Red exit | Green exit | Gate exits |
|---|---|---|---|---:|---:|---|
| Linked MIDI clips share notes, name and colour while each instance keeps its own start, length and loop window. Trim, resize and split affect only the selected instance; both split pieces stay linked. | FIXED | `0ce68cced` | `LinkedClipEditsTest`, `LinkedClipGroupEditsTest`, `MidiClipLinkedEditTest`, `ClipLinkTest` | 8 (`linked-red-test.log`; former group-wide geometry assertions fail) | 0 (`focused-green-final4.log`) | complexity 0; file length 0; fork sources 0; no-upstream 0; rt-safety 0 |
| Make Unique detaches one selected instance with its content copy and one-step undo; linked MIDI clips show an accessible chain marker in the arranger. | FIXED | `0ce68cced` | `ClipLinkTest::makeUniqueDetachesOnlyTheSelectedInstance`, `ClipLinkTest::makeUniqueIsOneUndoStep`, `MidiClipLinkedEditTest`, `ControlCommandsSnapshot`, `ReversibilityContractTest` | 8 (`make-unique-red-test.log`; only the Make Unique handler was temporarily disabled) | 0 (`make-unique-green-test.log`, then `focused-green-final4.log`) | complexity 0; file length 0; fork sources 0; no-upstream 0; rt-safety 0 |
| Contained crossfades clamp to clip edges by default. Alt on a same-track drop or `clip.crossfade mode="container"` opts into the V-shaped fade, sized from the inner clip's authored fades; old unmarked offset pairs retain their previous rendering. | FIXED | `0ce68cced` | `ClipOffsetFadeTest`, `ClipOffsetFadePairTest`, `ClipEditsTest`, `ControlCommandsSnapshot`, `ReversibilityContractTest` | 8 (`crossfade-red-test.log`) | 0 (`focused-green-final4.log`) | complexity 0; file length 0; fork sources 0; no-upstream 0; rt-safety 0 |

## Additional checks

- Build of `zene` and the eight focused test targets: exit 0 (`build-gate-fix.log`). A final `ClipLinkTest` rebuild after the reversible Make Unique red check also exited 0.
- The final focused ctest selection passed all 9 tests; the whole suite was not run.
- `python3 tests/doc-figures-gate.py`: exit 0; the live snapshot contains 824 commands across 76 groups. `ControlCommandsSnapshot` passed.
- `bash tools/dawproject-proof.sh`: exit 0. The local no-WASM histogram is 824 rows = 412 `true_inverse` + 37 `snapshot` + 24 `irreversible` + 351 `not_mutating`; the published full configuration includes the nine WASM rows, for 833 rows = 413 + 40 + 24 + 356. `ReversibilityContractTest` passed against the published figure.
- `python3 tests/rt-safety-sweep.py --check`: exit 0; no new allowlist entries.
- Crossfade render tests preserve the legacy envelope bit-for-bit and exercise the explicit gap marker with the allocation probe.
- The original linked-clip assertions that required geometry to propagate to the group were changed to assert the owner's 2026-10-09 per-instance rule. Content propagation assertions remain group-wide.

## Round 2

Independent review: `/tmp/claude-1000/-home-kruzzzzy-Documents-AI-KOS-PROJECT-projects-zene-studio-program/719627a5-c809-46ec-b4a4-231af5d313ec/scratchpad/luna-review-decisions-r3-clips/review.md`.

| Item | Fix commit | Test | Red exit | Green exit | Gate exits |
|---|---|---|---:|---:|---|
| Make Unique context action, accessible linked state and visible arranger marker | `dca34051f` | `MidiClipLinkedEditTest::linkedClipWidgetShowsMarkerAndAccessibleState`; `MidiClipLinkedEditTest::makeUniqueContextMenuActionDetachesOnlyTheSelectedClip` | 8 (marker suppressed; action removed) | 0 (each; final focused run 0) | complexity 0; file length 0; fork sources 0; no-upstream 0; rt-safety 0 |
| Contained crossfade gap silences pre-faded containers; Alt-drag selects the V-fade; unmarked legacy offset pairs keep their old envelope | `73e87cbba` (render fix), `dca34051f` (Alt-drag widget regression) | `ClipOffsetFadeTest::aContainerCrossfadeGapSilencesAPreFadedContainer`; `ClipOffsetFadePairTest::anUnmarkedPreFadedOffsetPairKeepsUnityBetweenItsRamps`; `MidiClipLinkedEditTest::altDragOnTheRealSampleClipWidgetUsesContainerCrossfade` | 8 (pre-faded leak); 8 (legacy pair incorrectly silenced); 8 (Alt dispatch disabled) | 0 (each; final focused run 0) | complexity 0; file length 0; fork sources 0; no-upstream 0; rt-safety 0 |
| ACCEPT-NOTE: linked MIDI loop windows remain instance-only | `dca34051f` | `ClipLinkTest::loopWindowEditsStayWithTheAddressedLinkedClip` | 8 (temporary group-wide loop propagation) | 0 (final focused run 0) | complexity 0; file length 0; fork sources 0; no-upstream 0; rt-safety 0 |

Round-2 verification: target build (`zene`, `ClipOffsetFadeTest`, `ClipOffsetFadePairTest`, `MidiClipLinkedEditTest`, `ClipLinkTest`) exit 0; the four named ctests, run from `build/tests`, exit 0 together. Each negative-control change was restored before its green build and ctest. The complete suite was not run.
