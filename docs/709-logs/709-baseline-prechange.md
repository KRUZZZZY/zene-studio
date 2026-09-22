# #709 pre-change baseline capture (the negative control's reference values)

Captured 2026-09-22 against the PRE-CHANGE build: HEAD `4ef3065fa5a46808c334f1818138d1d20f466c40`
(0.4.0/train-w1), no engine line of card #709 written yet (only the new, not-yet-registered
`tests/control-feedback-commands.py` + `src/core/ControlCommandsFeedback.cpp` existed; neither is
compiled in, see the `git status` below).

Command (unpiped):

    QT_QPA_PLATFORM=offscreen python3 tests/control-feedback-commands.py build/zene --capture
    > docs/709-logs/709-baseline-capture.raw 2>&1; echo EXIT=$?      # EXIT=0

Values (also in `709-baseline-prechange-values.json`, embedded as the script's constants):

    BASELINE_SAVE_SHA256 = e73f9b31bad6a7dda753b040a2a1fee5d3114992043fc6c7c884b88bbf2ace1f
    BASELINE_PDC    = the fixture's pdc.report, canonical JSON (json.dumps sort_keys, tight)
    BASELINE_MIXER  = the fixture's mixer.get_state, canonical JSON (same rule)

The saved project is NOT byte-stable raw: LMMS's container is zlib (4-byte prefix) with
QHash attribute order and a per-instance random `writer` uuid. The save measure is therefore
sha256 of the DECOMPRESSED XML after (a) dropping the `writer` attribute (per-instance uuid,
present on every element that carries it) and (b) sorting attributes - verified stable across
two consecutive pre-change saves (`709-baseline-capture.raw` vs `709-baseline-capture-rerun.raw`:
pdc STABLE, mixer STABLE, canonical save STABLE; the raw save sha differs only for the reasons
above).

Fixture (identical in capture and in the full run, deterministic ids on a fresh instance):
bus.create -> ch-9 (bus), mixer.add_channel -> ch-10, plugin.load amplifier dev-0 -> fx-11 on
the BUS (the loop's effect), mixer.send_to ch-9 -> ch-10 (unity), then the closing
mixer.send_to ch-10 -> ch-9 which the pre-change engine REFUSES:

    "message":"routing ch-10 to ch-9 would close a feedback path (the mixer refuses it)"

git status at capture time (engine untouched):

    ?? docs/709-logs/  ?? src/core/ControlCommandsFeedback.cpp  ?? tests/control-feedback-commands.py
    (docs/KNOWN-LIMITATIONS.md and docs/RELEASE-NOTES-v0.3.0-alpha.md were also edited - docs only)

Captured values live in `709-baseline-prechange-values.json` and in the raw transcript
`709-baseline-capture.raw` (BASELINE-BEGIN/END block).
