ZENE STUDIO 0.3.0 - VERIFICATION OF docs/CAPABILITIES-0.3.0.md
================================================================

Date:       2026-09-19
Verified by: kruzzzy (hub agent) for Zach
Subject:    docs/CAPABILITIES-0.3.0.md (capability specification v1)


1. VERDICT
----------

The specification is true for Zene Studio 0.3.0-alpha at commit b89d10a,
with six wrong or stale statements.

Every headline number was re-measured and matches: the binary hash, the
version string, 214 registered tests (all 214 pass), 340 command ids in 53
groups, the A16 histogram 340 / 158 / 32 / 13 / 137, and every gate figure
in Appendix B. All 321 command ids named in section 3 exist in the live
registry. Every source file, test script, tool and sibling document the
spec names exists in the tree.

The six errors are documentation errors, not engine defects. Two are
inherited from sibling docs that are themselves stale
(docs/KNOWN-LIMITATIONS.md and docs/UNDO-BOUNDS.md). None changes what the
release can do.


2. BASE OF MEASUREMENT
----------------------

tree            projects/lmms-fl-research/zene-030, branch release/0.3.0
commit          b89d10a625342757dc7f01be413c4e63e2436cf7 (the spec's base)
spec worktree   zene-030/wcap, branch 030/capabilities, 4 commits ahead of
                b89d10a (the spec, its generator, the fork-sources recipe)
binary          zene-030/build/zene
binary sha256   ab3f9b322bd2ef8ae4569223befb1ebcd1fdebd07d923d52d953cb5a8cb6fb92
binary version  Zene Studio 0.2.1-alpha.626+b89d10a; Qt 6.4.2;
                WANT_WASM=ON, WANT_STEM_SPLIT=OFF, WANT_SESSION_VIEW=ON
tag             no v0.3.0-alpha tag exists yet. The v0.3.0 tag in the repo
                is the 2007 LMMS release and is unrelated.

Three kinds of evidence were used:
  * static   - grep over sources, .github/workflows/build.yml, sibling docs
  * live     - one instance of the binary booted through
               tests/control_socket_harness.py; read control.commands_list,
               control.undo_depth, control.transactions, control.id_contract
               and other read-only ids
  * dynamic  - the full ctest suite, every gate script in Appendix B, the
               A16 histogram probe, the agent-surface gate


3. CONFIRMED CLAIMS
-------------------

claim                                              measured
-------------------------------------------------- -----------------------------
binary sha256 ab3f9b32...cb6fb92                   match
zene --version 0.2.1-alpha.626+b89d10a             match
CMakeLists.txt declares 0.3.0-alpha                match (VERSION_MINOR 3, STAGE alpha)
registered tests: 214                              214; 100% pass, 0 fail, 289 s
340 command ids in 53 id prefixes (live)           340 / 53 (205 mutating)
332 ids in the bridge snapshot                     332
A16 histogram 340/158/32/13/137                    MEASURED rows=340 true_inverse=158
                                                   snapshot=32 irreversible=13
                                                   not_mutating=137; DECLARED
                                                   rows=340 entries=340 duplicates=0
118 ControlCommands*.cpp files                     118
54 declared A16 prefixes, 346 declared rows        54 / 346 (source grep)
session.* 17, wasm.* 8, telemetry.* 2, stem.* 7    match
agent-surface gate 48 reflected, 42 baselined,     PASS: 340 commands, 339 swept,
  0 stale, 0 problems                              1 allowlisted, 48 reflected
fork-sources 663 / 1104 / 40 / 0 stale             match, PASS
upstream-divergence 422 changed paths              match, PASS
all-sources-reproduce                              REPRODUCES
file-length ratchet (fork)                         PASS
complexity ratchet (fork)                          PASS
duplication 0.52 % (budget 5 %)                    match, PASS
unregistered-tests gate                            PASS (171 scanned, 169 registered)
release-version gate                               PASS (tag step skipped: no tag yet)
undo caps 100 steps / 16 MiB / 10 000 / 512 MiB    match (ProjectJournal.h,
  / 400 ms window                                  ProjectJournal.cpp, live)
control.transactions caps                          cap_records 100, cap_bytes 262144
telemetry: closed allowlist of 24 keys, off by     24 keys in Telemetry.cpp;
  default, compiled out by ZENE_TELEMETRY=OFF      enabled=false live; option exists
MIDI retro capture 8192 events                     match (source and live)
OOP typed refusal after 3 deaths                   max_crashes_per_client=3 live
MIDI clock 24 ppqn, MTC present                    match live
mixer.set_volume 0..2, master ch-0 refused         match in source
Lua API 0.2.0, --! zene-api gate                   match (CMakeLists, LUA-COMPATIBILITY-POLICY)
4 scripts under data/scripts                       match (script.list)
stable ids: 6 families, 5 persistent, dev- is a    match (control.id_contract)
  catalogue selector
typed errors {kind, message}                       match (not_found sample)
Windows named pipe: PIPE_REJECT_REMOTE_CLIENTS,    match (ControlServerWin32.cpp,
  everything inside #if defined(Q_OS_WIN), one     lines 58-499)
  thread per connection
CI: 6 job definitions, 7 platform builds;          match (build.yml)
  runners ubuntu-22.04, ubuntu-24.04-arm,
  macos-15-intel, macos-15, ubuntu-latest,
  windows-2022, windows-11-arm
5 platform builds run ctest; mingw64 and           match; mingw step comment present
  windows-arm64 build only
-DWANT_VST3=ON -DWANT_CLAP=ON on every job;         match
  WANT_VST3_TEST_INSTRUMENT on linux-x86_64 only;
  WANT_QT6 on msvc-x64 only
advertised-features.tsv: six rows                  match
.bak written beside project on save                match (DataFile.cpp:391)
ALSA capture path present                          match (AudioAlsa.cpp)
CLAP: four broken test modules                     match (no-symbol, old-version,
                                                   init-fails, no-factory)
KNOWN-LIMITATIONS coverage 87.21 %, 13 770/15 790, match
  165 files, 15 files below the 50 % floor
determinism 7 of 9; Root84 and StrictProduction    match
golden audio 0 LSB over 10 pairs; 13 275 LSB,      match (docs/GOLDEN-AUDIO.md)
  40.5 %, 96.7 %; -0.001 dB sensitivity
DAWproject eleven losses                           match (release notes line 373)
"Everything is operable through --control-socket"  quote present (release notes line 21)
four-part scope contract                           present in FEATURE-LIST and the
                                                   charter NEXT-0.3.0-AGENT-PROMPT.md 3.1
session M1: 4 clips across 2 scenes                match (tests/control-session-m1.py)
CI evidence runs 35126160372, 35212797698          present in docs/reports/
run #6 / run #7 records                            docs/reports/CI-FIX6-REPORT.md
README Download: seven builds, release page only   match
RELEASING: sha256 per asset from the publish step  match

All 321 command ids named in sections 3.1 to 3.12 exist in the live
registry. Five live groups are not named in section 3 at all: arrangement,
audio, chain, roll, settings (12 ids). That is a gap, not an error; the spec
defers the full list to Appendix A.


4. STATEMENTS THAT ARE WRONG
----------------------------

4.1  Section 2.1 and 5.1: "a build job uploads a package for a tag build
     or a manual dispatch, never for an ordinary push".
     WRONG. The six package upload steps in .github/workflows/build.yml
     (lines 274, 492, 749, 1048, 1278, 1443) are
         if: startsWith(github.ref, 'refs/tags/')
     with no workflow_dispatch clause. A manual dispatch uploads nothing.
     Only the release-gate job runs on dispatch.
     Source of the error: docs/KNOWN-LIMITATIONS.md lines 43-45 quote a
     condition with "|| github.event_name == 'workflow_dispatch'" and cite
     line numbers (147, 278, 420, 545, 717, 825) that no longer match.
     FIX: delete "or a manual dispatch" in the spec; correct
     KNOWN-LIMITATIONS to the real condition and line numbers.

4.2  Section 5.4: "Coalescing covers the five declared commands only."
     WRONG. control.undo_depth on the live binary lists six coalescing
     commands: chord.set, clip.move, clip.resize, mixer.set_volume,
     plugin.param_set, rack.macro_set. chord.set was added by the RCO row
     in src/core/ControlReversibilityTableChord.cpp.
     Source of the error: docs/UNDO-BOUNDS.md lists five (no chord.set).
     FIX: "six" in the spec; add the chord.set row to UNDO-BOUNDS.md.

4.3  Section 3.8: proof "ControlRenderCommands".
     WRONG. No test with this name is registered. The render.render
     start_ticks / end_ticks proof lives in tests/control-render-presets.py,
     registered as ControlRenderPresets.
     FIX: replace the name.

4.4  Section 3.8: proofs StemJobManagerTest, StemModelStoreTest,
     StemSplitPipelineTest for render.stems.
     MISLEADING. These three compile only under
     IF(LMMS_HAVE_STEM_SPLIT) (tests/CMakeLists.txt line 427), which is OFF
     in this build and in every release build. They are not among the 214
     tests. They test the stem.* separation engine, not render.stems.
     FIX: cite StemExportTest and ControlStemExportVerb only.

4.5  Section 3.11: "listed one line each in docs/LUA-API-STABILISATION.md §5".
     WRONG POINTER. Section 5 of that file is "Threading statement for the
     new entry points". The withheld-bindings list is in section 8,
     subsection "Withheld, one line each" (line 347).
     FIX: §8.

4.6  Appendix B.2: "python3 tests/agent-surface-gate.py ../build/zene".
     INCOMPLETE. The script requires a fixture argument and exits 2 with
     a usage line when it is missing. The working command, as registered
     in tests/CMakeLists.txt line 2802, is:
         python3 tests/agent-surface-gate.py ../build/zene \
             tests/data/agent-control-fixture.mmp --check
     FIX: add the fixture and --check.


5. SMALL POINTS AND NUANCES
---------------------------

5.1  The one-row residual in Appendix B.2 is settled. The live id the
     source grep misses is chord.set. It is declared with the RCO macro
     (ControlReversibilityTableChord.cpp line 88), which the pattern
     (RC|R)\(" does not match. 339 + 1 = 340 and 157 + 1 = 158. The
     published figure is correct. The spec can record this instead of
     "likely cause".

5.2  ControlNamedPipeSmoke is registered only under
     IF(WIN32 AND PYTHON3_EXECUTABLE) (tests/CMakeLists.txt line 1968). The
     spec's "registered ... CI-only evidence" is right, but the test is not
     one of the 214 this box counts.

5.3  Agent-surface gate: the spec says "340 commands swept". The gate
     reports 340 commands, 339 swept, 1 allowlisted (telemetry.consent).

5.4  Section 3.7: "eleven distinct failure codes". ClapLoader.h has an
     enum of 11 codes, and one of them is None (no failure). There are
     ten failure codes. docs/RELEASE-NOTES-v0.3.0-alpha.md line 2735 has
     the same wording.

5.5  Section 1: "52 declared cmd.group literals". ControlCommands*.cpp
     holds 51 distinct QStringLiteral group names (stem included). The
     52nd, "wasm", is in src/core/ControlWasmSupport.cpp. chord and
     modulator use a kGroup constant, as the spec says. The total of 54
     declared prefixes is right.

5.6  tests/release-honesty-gate.sh FAILS on the local binary on the
     wasm-sandbox row (WANT_WASM=ON here, the row requires OFF). The spec
     predicts exactly this for the tree build. The release binaries are
     the ones the row governs.

5.7  docs/KNOWN-LIMITATIONS.md still carries the H1
     "Zene Studio 0.2.1-alpha: known limitations". The release-version gate
     does not check that heading.

5.8  Appendix A is marked "not generated". The generator
     scripts/capabilities-dump.py runs cleanly from wcap against
     ../build/zene (exit 0, 616 lines, 340 ids in 53 groups, 205 mutating).
     Its output was not pasted into the spec by this verification.

5.9  Section 3.12: control.undo on an instance with no agent command
     recorded unwound the engine's own ProjectJournal step from project
     load (undone_command=""). This matches the documented fallback.

5.10 Files changed by this verification: none in either worktree. The
     complexity and file-length gates refreshed their baseline files; both
     were restored with git checkout. wcap holds one untracked file of its
     own, LANE-STATE.md, which predates this work.


6. METHOD AND RE-RUN COMMANDS
-----------------------------

Run from zene-030/wcap unless stated.

  export LD_LIBRARY_PATH=$PWD/../third_party/wasmtime/lib

  sha256sum ../build/zene
  ../build/zene --version
  (cd ../build/tests && ctest -N | tail -1)
  (cd ../build/tests && ctest -j2 --output-on-failure)       # 214/214 pass

  python3 scripts/capabilities-dump.py ../build/zene         # live catalogue
  DAWPROJECT_PROOF_BUILD=$PWD/../build bash tools/dawproject-proof.sh
                                                             # MEASURED rows=340 ...
  python3 tests/agent-surface-gate.py ../build/zene \
      tests/data/agent-control-fixture.mmp --check

  bash tests/fork-sources-gate.sh
  bash tests/all-sources-reproduce.sh
  bash tests/no-upstream-regression-gate.sh
  bash tests/file-length-gate.sh --scope fork
  bash tests/complexity-gate.sh --scope fork
  bash tests/duplication-gate.sh
  bash tests/unregistered-tests-gate.sh
  bash tests/release-version-gate.sh
  bash tests/release-honesty-gate.sh --header ../build/lmmsversion.h \
      --artifacts ../build                                   # FAILS on wasm row locally
  git checkout -- tests/complexity-baseline.tsv tests/file-length-baseline.tsv

  # the A16 residual
  grep -rhoE '(RC|R)\("[^"]+"' src/core/ControlReversibilityTable*.cpp \
      | sed 's/.*("//; s/"$//' | sort -u > /tmp/a16.txt
  # diff against the live id list from the catalogue -> chord.set

  # the upload condition
  grep -n "if: startsWith" .github/workflows/build.yml       # six lines, tag only

Measurement hygiene: two instances in total were booted by this
verification outside ctest (the catalogue dump and one read-only probe),
each through the tree's harness, closed with control.quit, reaped by PID.
