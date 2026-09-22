# #709 verification record (040/feat-709, 2026-09-22)

Every command below was run UNPIPED (`cmd > log 2>&1; echo EXIT=$?`), in
`/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-709`,
from the integration tip `4ef3065fa5a46808c334f1818138d1d20f466c40`.
`.log` files were held on disk only and are NOT committed (owner decision
REPO-1/CP-1: a run's output is evidence by hash, not by megabytes - the hashes
are at the bottom); the durable evidence kept in this directory is the baseline
capture (`.raw`), its values (`.json`), this file and `709-baseline-prechange.md`.

## Build (targeted, -j2, never the default target)

    bash tools/local-ci.sh --configure-only --build-dir build          EXIT=0
    cmake --build build --target zene -j2      (pre-change baseline)   EXIT=0
    cmake --build build --target amplifier -j2 (fixture effect)        EXIT=0
    cmake --build build --target zene ReversibilityContractTest ControlRegistryTest
        PdcMixerTest MixerRoutingBackwardCompatTest PhaseDSidechainTest
        MixerConcurrencyTest RoutingGraphLiveTest ReversibilityUndoTest -j2
                                                                        EXIT=0
    cmake --build build --target zene -j2      (stable-id fix)         EXIT=0
    cmake --build build --target <153 plugin targets> -j2              EXIT=0
    cmake --build build --target cmt -j2       (fixture LADSPA set)    EXIT=0
    df -h /home before every phase: 84G -> 70G free (never near the 12G floor)

## The negative control's reference (captured BEFORE any engine line of #709)

    QT_QPA_PLATFORM=offscreen python3 tests/control-feedback-commands.py \
        build/zene --capture > docs/709-logs/709-baseline-capture.raw 2>&1
                                                                        EXIT=0
    (run twice more: pdc STABLE, mixer STABLE, canonical save sha STABLE -
     see 709-baseline-prechange.md; values embedded in the test)

## Registered proof and the ctest battery

    cd build/tests && ctest -R ControlFeedbackCommands --output-on-failure
                                                                        EXIT=0
        25/25 checks: default-mode refusal; pdc.report / mixer.get_state /
        saved bytes == pre-change baseline BEFORE the mode, and byte for byte
        AGAIN after disable; enable carries the suspension + REAPER warning;
        loop accepted only inside; compensation_frames=0 at the point of use;
        re-refusal after disable; one control.undo restores mode + send;
        A16 true_inverse transactions. (Two earlier iterations failed EXIT=8
        with test-side defects - float32 amount compare, index-vs-stable-id in
        feedback.get_state, a project.save step shadowing the undo - each fixed
        and re-run; the engine halves of those runs were already green.)

    cd build/tests && ctest -R "ControlFeedbackCommands|ControlRoutingCommands|
        ControlBusCommands|ControlPdcCommands|ControlCommandsSnapshot|
        PdcMixerTest|MixerRoutingBackwardCompatTest|PhaseDSidechainTest|
        MixerConcurrencyTest|RoutingGraphLiveTest|ReversibilityContractTest|
        ControlRegistryTest|ReversibilityUndoTest|agent_surface"
                                                                        EXIT=0
        100% tests passed, 0 tests failed out of 14

    bash tools/local-ci.sh --no-build --build-dir build                 EXIT=1
        run 1: 5 real failures (ControlSocketIntegration,
        ControlFreezeCommandsTranscript, ControlMeterCommands,
        ControlGoldenAudio, ControlHeadlessProjectOpen) + 171 "Not Run".
        Re-run of the 5 alone (not a load artefact) still failed -> measured
        cause: the targeted build had never built the plugin targets those
        fixtures need ("plugin.list is not broken down by format:
        {'builtin': 2}", Root84-TrancyLoop playing silence with no instrument
        modules, missing bundled cmt LADSPA sets). After building the 153
        plugin targets + cmt: ALL FIVE PASS (rerun evidence
        709-ctest-rerun5-after-plugins.raw, 709-ctest-golden-after-cmt.raw).
        run 2 (after the fix): 62 executed tests, **62 passed, 0 real
        failures**; the 166 remaining "Not Run" entries are test binaries
        never built by design (targeted lane build; building them = the 44 GB
        full tree this brief forbids) - which is why local-ci's overall exit
        is 1: Not-Run counts as failed for ctest, and a 0-tests-run would be
        an error, never a pass.

## Save/load of the submode (manual transcript, the registered ctest's
## baselines already cover the byte-for-byte claims)

    QT_QPA_PLATFORM=offscreen python3 - (save with loop, assert the persisted
    feedback="1" attribute, reopen, assert mode on + compensation 0, disable,
    re-refuse) > docs/709-logs/709-persistence.raw 2>&1               EXIT=0
        7/7 checks ok (see 709-persistence.raw)

## A16 figures (LANE-LOCAL - the merge tip re-measures both)

    DAWPROJECT_PROOF_BUILD=build bash tools/dawproject-proof.sh        EXIT=0
        MEASURED rows=343 true_inverse=165 snapshot=31 irreversible=13
        not_mutating=134; DECLARED rows=343 entries=343 duplicates=0
        (this build, wasm off). The RELEASE-NOTES A16-HISTOGRAM block is
        updated to the reference configuration's 351/165/34/13/139 =
        the previous 348/163/34/13/138 + this lane's 3 rows
        (2 true_inverse + 1 not_mutating). ReversibilityContractTest PASSED
        against that published block.
    ControlRegistryTest commandCount: 85+7+5+5+5+3+3 = 113 - PASSED.
    Both constants are marked "LANE-LOCAL, the merge tip must re-measure".

    commands_snapshot.json regenerated against a live instance IN THIS TREE
    (tools/mcp-zene-control/snapshot_commands.py --socket <sock>): count
    340 -> 343, feedback.{get_state,enable,disable} present; the fresh diff
    refreshed the two mixer.route_to / mixer.send_to descriptions too (whole-
    surface regeneration, never a hand merge). ControlCommandsSnapshot PASSED.

## Gates (run this lane, unpiped)

    bash tests/fork-sources-gate.sh              PASS (703 fork-NEW, 1108 all)
    bash tests/complexity-gate.sh                PASS (ratchet clean)
    bash tests/file-length-gate.sh --check       PASS (after one reviewed
        reanchor: include/ControlRegistryGroups.h 725 -> 732, reason = the
        feedback.* declaration comment in a comment-dense file at its ceiling)
    bash tests/duplication-gate.sh               PASS (0.50%, budget 5%)
    bash tests/no-tautology-gate.sh              PASS
    bash tests/unregistered-tests-gate.sh        PASS (186 sources, 184 registered)
    bash tests/evidence-gate.sh                  PASS after removing this
        directory's *.log (hashes below) - first run refused 4 .log files
    bash tests/no-upstream-regression-gate.sh    EXIT=1 **PRE-EXISTING**: the
        same 8 violations (docs/train-logs/*.out, committed by the train)
        fail identically at the base tip WITH this lane's changes stashed
        (measured: 709-gate6-basetip.log == 709-gate6-final.log, both 8). No
        path this lane touched is a violation; every inherited path I changed
        carries a `board card #709` reason in tests/upstream-modifications.txt.
    coverage-gate / mutation-gate: need their own instrumented builds
    (train/CI artefacts) - not run in this lane.

## Deleted .log evidence, by hash (REPO-1: hashes, not megabytes)

    022522ada97dc4d23729977c5ab70a91fc3b82537c2e3fa7b65c66c8e60f60b1  709-gate-unreg2.log
    022522ada97dc4d23729977c5ab70a91fc3b82537c2e3fa7b65c66c8e60f60b1  709-gate-unreg.log
    123f5c981ef590a3e532985eeafa2aa3cbb090e78dd6e40c5fddc1c2aeb690c2  709-gate-evidence.log
    27f49e3aa34c4340958a905810d89915f6254c17aac0b5d2f054ca4afbc9be20  709-build-amplifier-prechange.log
    365459141bdb208de59a7a28d4741523066a402cb10432d1ca8edb8e8a76f7b6  709-gate-taut2.log
    365459141bdb208de59a7a28d4741523066a402cb10432d1ca8edb8e8a76f7b6  709-gate-taut.log
    3ca38a6905781ba8aab6260c4557d82a1016e8082499a40bcc250192f96d3834  709-reconfigure.log
    4a1ad0a8fc8ccba1781ad3fc463fedb69398650e185b71a7ed06bc88b5142c32  709-gate9-final.log
    4c6e4849ae0975fd20df4d720f8a93bc2a444db09ce92ef7e28b856fa961d73d  709-gate9-second.log
    5448fef5ada74d0a0c7392b5c290c344889bd8745fa8e2b1e219bb2043cfb017  709-configure.log
    600418a5159bfabee4310634dba4bb7e68f79411c15c5995cc9b4e15298cca30  709-gate-dup.log
    6e65f71963e5bed75253e9182707cd8b17d88d532ebba8294d3d78e934984f70  709-gate-complexity.log
    763367f71b9d589b1bf23a2216e4b4e8c132ad542b8aa251abf46058520f2de0  709-a16-measure.log
    7bd3c8ec842b9ee9a01c9c675a6741459e683d5e7653969de63848eff373e3fa  709-build-postchange.log
    8e283cb57be775983959f70d35c825db7ecf4c19e4cf2a2ca6b7ace185711e91  709-rebuild-feedback.log
    c2c9459a6db1f0c27f14375c1e1c0fcd07ff669bcbde53ad5e399d1b90ba0729  709-build-plugins.log
    cc3e964b39ab37511bee66031358ec290b6daab85a9c626b1b9d6a05f533f310  709-build-zene-prechange.log
    d56a8357cf08013e2f2e1b749968c53c34d538ad2e7af1d13c02755aaff7d7fd  709-gate9-first.log
    dcd8a7848bea21d45e9b56a552c9a47534bf2f47b28097ab18f92c4d062d8cf8  709-gate6-basetip.log
    dcd8a7848bea21d45e9b56a552c9a47534bf2f47b28097ab18f92c4d062d8cf8  709-gate6-pending.log
    de14333e852f71d3562e6a57bcccf42ea3e50425fd57e65ec3b423faff251b33  709-gate6-final.log
    dffc53faafd1ec81c9acbc19fcc7b204492749e1eaeff126e4e36c35d7ea106a  709-build-cmt.log
    f9a495f20541b574a9de42f17251da2d68a17c8b3f0408be806d9f6d0e27d777  709-ctest-rerun5.raw
        (the first, failing re-run of the five tests - deleted for the 1 MiB
        evidence cap; the passing re-runs after the plugin build are kept)
