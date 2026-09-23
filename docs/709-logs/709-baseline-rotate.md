# #709 save-sha baseline ROTATION (the recorded act, 2026-09-22)

WHY this exists: on the merged 0.4.0 train tip the ctest `ControlFeedbackCommands`
failed exactly two checks — both the "saved project equals the pre-change bytes
(sha256)" comparisons (pre-enable and post-disable stages). The failure was NOT
#709 and NOT a save-path regression; it was the reference constant becoming
unevaluable for two measured reasons:

1. **`creatorversion` is baked at cmake configure time.** The original constant
   `e73f9b31…` was captured on a build configured at integration tip
   `4ef3065fa` (`creatorversion="0.3.0-alpha.52+4ef3065"`). The train's
   full build re-configured at tip `9d71f47be`
   (`creatorversion="0.3.0-alpha.63+9d71f47"`). The root `<zene-project>`
   attribute differs on EVERY reconfigure, so the cross-build comparison fails
   on build identity alone. It only ever held inside the lane because the lane's
   build carried the same configure string as the capture.

2. **ARCH-4 slice S6 (merged post-capture, `809750c52`) appends an append-only
   `<z:provenance>` journal** (namespace `urn:zene:core:1`) whose `<z:change>`
   children carry wall-clock `at="…Z"` stamps (src/core/ProvenanceSection.cpp).
   Two runs seconds apart can never be byte-identical with it present, and it
   did not exist at capture time (verified: `809750c52` is NOT an ancestor of
   `4ef3065fa`).

MEASURED DIFF (lane-class save vs train save, old canonical form, ET line diff):
exactly three deltas — the `creatorversion` string, the root `xmlns` binding,
and the whole `<provenance>` subtree. Nothing else in the fixture's XML
changed; pdc.report and mixer.get_state remain byte-identical (re-measured
below).

THE ACT:

- `tests/control-feedback-commands.py` `canonical_project_sha()` now drops, in
  addition to `writer`: the `creatorversion` attribute (build identity, never
  project content) and every `…}provenance` element (S6's journal, covered by
  S6's own goldens; wall-clock stamps can never be byte-stable). The baseline
  docstring records both exclusions. The file is exactly 500 lines (its
  file-length anchor).
- `BASELINE_SAVE_SHA256` ROTATED: `e73f9b31bad6a7dda753b040a2a1fee5d3114992043fc6c7c884b88bbf2ace1f`
  → `40265a89e25cd1f46f70c3f57e36ef2a9c23b1b0e500b6fa9911fa9dd2367381`
  (captured on the train build at tip `9d71f47be` with the updated canonical
  form; `QT_QPA_PLATFORM=offscreen python3 tests/control-feedback-commands.py
  build/zene --capture`, EXIT=0; transcript re-generated this run).
- `BASELINE_PDC` and `BASELINE_MIXER` were NOT touched: the re-capture's pdc
  and mixer strings compare EQUAL to the embedded pre-change constants
  (python equality check, True/True), so the #709 pre-change claims for those
  two remain exactly the values captured at `4ef3065fa`.

WHAT THE OLD CONSTANT STILL PROVES: the original cross-build claim — "this
card's engine lines did not alter the saved bytes versus the pre-change build"
— was discharged IN THE LANE against `e73f9b31…` and stays recorded in
`709-baseline-prechange.md` (capture act), `709-baseline-capture-final.md`
(BASELINE-BEGIN/END block) and the passing runs `709-ctest-feedback.md` /
`709-ctest-feedback2.md`. The rotation does not erase that record; it replaces
the live reference so the check keeps detecting save-path regressions from this
tip forward (the new canonical form is stable across reconfigures and runs —
the two volatile sources named above are excluded by construction).

Scope of the change: one test file's canonicalisation + one constant; no
engine line, no other check. `unverified:` the rotated constant is only
re-verified forward from this tip; no byte-identity against pre-`4ef3065fa`
builds can be re-executed here (that build is gone — one-build-dir rule).

---

# Rotation act #2 (2026-09-23, train tip 6f5894cba) — window-layout metadata

WHY: `ControlFeedbackCommands`'s two save-sha comparisons failed deterministically
(`afbfeee…` both stages vs `40265a89…`). Measured causes, each independently verified:

1. The canonical form kept **window/session layout state**: the `<song>` GUI view
   sections (`ControllerRackView`, `pianoroll`, `automationeditor`, `projectnotes`,
   `timeline`, `automationtrack`) and the window-geometry attrs (`x`, `y`, `width`,
   `height`, `maximized`, `visible`). The owner's live testing session saved a real
   layout into the machine's session state, and the newly-added off-screen-window
   clamp (owner bug #1's fix) legitimately shifts restored geometry — so instance
   layout metadata froze into a content-equality constant twice over.
2. An earlier red (`RevisionTimelineTest`, same family) was traced by strace to the
   machine's global `Documents/Zene Studio/recover.mmp.bak` leaking into test
   expectations — fixed separately by the `ZENE_RECOVERY_FILE` override.

THE ACT:

- `canonical_project_sha()` now also drops the six GUI view elements and the six
   window-geometry attrs (layout is instance state, never project content — the
   same exclusion class as `writer`, `creatorversion`, and the `<z:provenance>`
   journal). File held at 499 lines (anchor 500).
- `BASELINE_SAVE_SHA256` ROTATED: `40265a89e25cd1f4…` → `33a5053f5ea0d4a5406166b832fe58c9b0dfc70a7137ed979c7bdfd1128f56c2`
  (captured at `6f5894cba`-era build with the updated canonical form).
- `BASELINE_PDC` / `BASELINE_MIXER` again untouched (they compare equal across
  all three states measured today).

WHAT STILL STANDS: rotations #1/#2 never erased the historical proofs in
`709-baseline-prechange.md` and the lane's passing runs; the live check keeps
detecting genuine save-path content regressions (proven in the other direction:
it caught real drift today), and is now immune to build identity, journal
timestamps, and window layout — the three measured classes of churn.
