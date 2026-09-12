# Independent read of the 0.2.0-alpha notes

**Who.** The independent reader obligated by `docs/RELEASE-NOTES-v0.2.0-alpha.md`, freeze item 5
("Have an independent reader compare this text against the built binary, not against the plans"). The
author of the notes verified their own text; this is the second party.

**What was read.** `docs/RELEASE-NOTES-v0.2.0-alpha.md` and the 0.2.0 limitations page. **The brief named
`docs/KNOWN-LIMITATIONS-v0.2.0-alpha.md`; that path does not exist in this tree** — the 0.2.0 limitations
page is `docs/KNOWN-LIMITATIONS.md` (its own title is "Zene Studio 0.2.0-alpha: known limitations", and
both documents cross-reference it by that name, e.g. the notes' `docs/KNOWN-LIMITATIONS.md:84`). This
report audits that file. The 0.1.0 page is `docs/KNOWN-LIMITATIONS-v0.1.0-alpha.md`.

**The tree as read.** `post-alpha/integration`, no files modified by this audit except this report
(untracked, not committed). HEAD was `f1b2879263abda28c6ad8bdf354fb81c4dce1fb4` when the audit began and
moved to `6daed30eb85288409366d6c1484a382d441a3b13` mid-audit when the parent merged
`post-alpha/teardown-2` (which touches `src/core/midi/MidiAlsaSeq.cpp` and `tests/` only). Line numbers
below were taken at `f1b287926` and re-checked at `6daed30eb` where they matter. Two earlier tips are
also used because the documents name them: the **frozen tip `2239f3cb6`** (which the notes claim they
were verified at) and the **release-prep base `34c1f4f86`**.

**The binaries.** `build-coverage/zene` — the shipping tip, `0.1.0-alpha.306+18c6da1` (Debug; the
plugin-hosting modules are built). `build/zene` — the release configuration, `0.1.0-alpha.247+f68cf8e`
(stale, `WANT_SESSION_VIEW=ON` left in its cache). No branch, tag or remote was touched.

**Method.** Every verdict below rests on a command run in this worktree and its real output. Nothing was
taken from the plans or the drafts. Three outcomes were used: **TRUE**, **FALSE** (disproved, with the
command), **UNVERIFIABLE FROM THIS BOX** (with what would settle it).

---

## The verdict table

### A. Provenance and freeze status (notes, header and "Freeze status")

| # | claim | verdict | command and the output that settles it |
|---|---|---|---|
| A1 | the corrected passages were "**ported** into this file at the frozen tip `2239f3cb6`" and "re-checked against that frozen tree and its built binary" | **FALSE** | `git log --oneline -- docs/RELEASE-NOTES-v0.2.0-alpha.md` → the file was edited after that tip: `79d800ef4` (08:33), `ed3134b15` (08:39), `83a7183dd` (12:54, "apply the control-surface section"); `git show 2239f3cb6:include/ControlVocabulary.h` → `fatal: path 'include/ControlVocabulary.h' does not exist` (the same for `include/ControlReversibility.h`, `src/core/ControlCommands*.cpp`). The control-surface section cites files that did not exist at the tip it claims to have been verified at. |
| A2 | "This file carried **16** markers and all 16 are settled" | **UNVERIFIABLE** | No marker list exists. `grep -c "VERIFY AT FREEZE" docs/RELEASE-NOTES-v0.2.0-alpha.md` → 3 (all prose, see A3). `docs/RELEASE-PREP-0.2.0.md` §3's tables have ~18 notes rows and its §3 opening says "**No claim was deleted** and none was published unmarked", while the notes' item 1 says "One was a **deletion** rather than a resolution". The number 16 is not re-derivable and the two documents contradict each other on the deletion. |
| A3 | "No marker remains, and no unverified claim was left unmarked" (first half) | **TRUE** | `grep -n "VERIFY AT FREEZE" docs/RELEASE-NOTES-v0.2.0-alpha.md docs/KNOWN-LIMITATIONS.md` → notes: lines 7, 17, 602; limitations: line 6. Every hit is the convention being described, not a live marker. No unmarked survivor was found. |
| A4 | item 2: "On this tip the built header reports `Zene Studio 0.1.0-alpha.241+2239f3c` (`build-coverage/lmmsversion.h`)" | **FALSE** | `head -1 build-coverage/lmmsversion.h` → `#define LMMS_VERSION "0.1.0-alpha.306+18c6da1"`. The header is from `18c6da128`, not `2239f3cb6`. |
| A5 | item 2: `git describe … 34c1f4f86` → `v0.1.0-alpha-123-g34c1f4f86` | **TRUE** | `git describe --tags --match 'v[0-9]*.[0-9]*.[0-9]*' 34c1f4f86` → `v0.1.0-alpha-123-g34c1f4f86` |
| A6 | item 2: an untagged build of that base reports `Zene Studio 0.1.0-alpha.123+34c1f4f` | **UNVERIFIABLE** | No artefact of `34c1f4f86` exists on this box and none can be built without a build. The mechanism (`git describe` → `VERSION` → `LMMS_VERSION`) is plausible; the string itself is not checkable. |
| A7 | item 2: "the `v0.2.0-alpha` tag … **does not exist yet** and nothing is tagged by this pass" | **TRUE** (with an observation) | `git tag -l 'v0.2.0-alpha' \| wc -l` → `0`. Observation: `git tag -l 'v0.2*'` → `v0.2.0`, `v0.2.1`, both 2006 LMMS tags **reachable from HEAD** and both matching the item's own describe glob; `git describe` returns `v0.1.0-alpha-310-…` only because that tag is nearer. A future reader who types `v0.2*` will find two tags the text says nothing about. |
| A8 | item 3: "`build/CPackConfig.cmake:45` produces `zene-0.1.0-alpha.123+34c1f4f-linux-x86_64` on this base" | **FALSE** | `sed -n '45p' build/CPackConfig.cmake` → `set(CPACK_PACKAGE_FILE_NAME "zene-0.1.0-alpha.247+f68cf8e-linux-x86_64")`. The line exists and the *pattern* is right; the string quoted is from a different commit, not from the file in this tree. |
| A9 | item 3: the asset-name pattern is `${CMAKE_PROJECT_NAME}-${VERSION}-<platform>` | **TRUE** | `build/CPackConfig.cmake:45/68` → `zene-0.1.0-alpha.247+f68cf8e-linux-x86_64` / `-source` — `zene-<version>-<platform>`, as claimed. |
| A10 | item 4: `tests/advertised-features.tsv` "carries six rows" | **TRUE** | `grep -cE '^[a-z]' tests/advertised-features.tsv` → `6` (vst3-hosting, vst3-instrument-hosting, clap-hosting, session-view, wasm-sandbox, stem-separation). |
| A11 | item 4 is "**Done**", and "the manifest does enforce that no documented-**present** host is compiled out"; limitations: "Everything in the release notes is present in this build — **the release-honesty check enforces that**" | **FALSE — most serious finding** | `./build-coverage/zene --version > /tmp/bc.txt; bash tests/release-honesty-gate.sh --dump /tmp/bc.txt --artifacts build-coverage/plugins` → `[FAIL] vst3-hosting WANT_VST3='AUTO', but the release documents this as present and requires ON` (same for `vst3-instrument-hosting` and `clap-hosting`); `RESULT: FAIL — 3 of 6 documented feature(s) do not match this build` (exit 1). On the release-configuration binary: `bash tests/release-honesty-gate.sh --dump /tmp/b.txt --artifacts build/plugins` → `[FAIL] session-view WANT_SESSION_VIEW='ON' …; RESULT: FAIL — 1 of 6`, exit 1. **Neither available binary passes the check the text says enforces it.** See F1. |
| A12 | item 6: "`WANT_SESSION_VIEW` defaults off, the release jobs do not pass it" | **TRUE** | `grep -n "WANT_SESSION_VIEW" CMakeLists.txt` → `121:OPTION(WANT_SESSION_VIEW "…" OFF)`; `grep -rn "WANT_" .github/workflows/*.yml` → only `-DWANT_VST3=ON -DWANT_CLAP=ON` (7 jobs), no session flag. |
| A13 | item 6: "there is **no clip launcher** even with it on" | **TRUE** | `grep -rln "SessionClip\|SessionModel\|sessionScheduler\|SceneView\|scene" src/gui/` → no matches. The data layer (`src/core/SessionScheduler.cpp`, `SessionModel.cpp`, `SessionClip.cpp`) is in the tree; nothing in the GUI consumes it. |
| A14 | item 1's sibling claim in limitations: the notes "carry no per-platform first-run steps" | **TRUE** | `grep -n "FUSE\|SmartScreen\|Gatekeeper\|extract-and-run" docs/RELEASE-NOTES-v0.2.0-alpha.md` → the only hits are the two lines that *say* there are none. The steps are preserved in `docs/KNOWN-LIMITATIONS-v0.1.0-alpha.md` (present). |

### B. The two binaries and the build options the reader will meet

| # | claim | verdict | command and output |
|---|---|---|---|
| B1 | `build-coverage/zene` is the shipping tip, Debug, plugin-hosting modules built | **TRUE** | `./build-coverage/zene --version` → `Zene Studio 0.1.0-alpha.306+18c6da1`; `ls build-coverage/plugins/libvst3effect.so build-coverage/plugins/libvst3instrument.so build-coverage/plugins/libclapeffect.so` → all three present; dump shows `WANT_COVERAGE='ON'`. |
| B2 | `build/zene` is the release configuration at a stale commit with session view left on | **TRUE** | `./build/zene --version` → `Zene Studio 0.1.0-alpha.247+f68cf8e`; dump → `WANT_VST3='ON' WANT_CLAP='ON' WANT_DEBUG_CPACK='ON' … LMMS_HAVE_SESSION_VIEW='1' WANT_SESSION_VIEW='ON'`. |
| B3 | the notes' "Both are now provisioned and compiled in" (VST3 + CLAP) | **TRUE for capability, caveat on the option value** | The three modules exist in `build-coverage/plugins/` (B1). But the same binary's dump says `WANT_VST3='AUTO' WANT_CLAP='AUTO'` — not `ON` — and `tests/release-honesty-gate.sh`'s own header states "AUTO is not ON … That is exactly the state the alpha shipped in." The capability is present; the contract the notes say is enforced (A11) does not pass on this binary. |
| B4 | "the WASM DSP sandbox and stem separation report `OFF` in the binary's own build options" | **TRUE** | `grep -o "WANT_WASM='[A-Z]*'" /tmp/bc.txt` → `OFF`; `grep -o "WANT_STEM_SPLIT='[A-Z]*'" /tmp/bc.txt /tmp/b.txt` → `OFF`, `OFF`; `grep -o … build/lmmsversion.h` → `OFF`, `OFF`. |
| B5 | (not stated by the notes) | **observation** | `CMakeLists.txt:133` → `option(WANT_WASM "…" ON)` — the default is **ON**, and the `OFF` these builds report is the absent-dependency degradation at `:950-955`. The absent-WASM row is true of these binaries only because wasmtime is not on this box — the same silent compile-out the manifest's own header describes. The text does not say this. |

### C. Rename, branding, and the files we write

| # | claim | verdict | command and output |
|---|---|---|---|
| C1 | `lmms_plugin_main` is kept as the plugin entry symbol, "`src/core/Plugin.cpp:228`" | **TRUE symbol / FALSE line today** | `grep -n lmms_plugin_main src/core/Plugin.cpp` → `237: … resolve("lmms_plugin_main")` at HEAD; `git show 34c1f4f86:src/core/Plugin.cpp \| grep -n lmms_plugin_main` → `228`; `git show 2239f3cb6:…` → `228`. The line was 228 at the base and at the frozen tip; it is 237 in the tree. |
| C2 | `DataFile.cpp` **writes** `<zene-project>` at `:128/136/313` | **TRUE at the frozen tip / FALSE today** | `git show 2239f3cb6:src/core/DataFile.cpp \| grep -n "zene-project"` → `128, 136, 313`; at HEAD the same grep → `129, 137, 321`. |
| C3 | the reader still accepts `<lmms-project>` (`:1741-1742`) | **TRUE at the frozen tip / FALSE today** | `git show 2239f3cb6:src/core/DataFile.cpp \| grep -n "lmms-project"` → `1742: if (root.isNull()) { root = firstChildElement("lmms-project"); }`; at HEAD → `1750`. |
| C4 | read-both comment at `:1739` | **TRUE at frozen / FALSE today** | same file, `grep -n "legacy root name"` → `1739` at `2239f3cb6`, `1747` at HEAD. |
| C5 | write-new comment at `:2277` | **TRUE at frozen / FALSE today** | `grep -n "files written by this build say Zene Studio"` → `2277` at `2239f3cb6`, `2294` at HEAD. |
| C6 | the plugin-logo resource key is `zene-plugin-logo`, not `lmms-plugin-logo` | **TRUE** | `grep -rn "zene-plugin-logo" --include=*.cpp src/ plugins/` → e.g. `plugins/WaveShaper/WaveShaper.cpp:49: new PixmapLoader("zene-plugin-logo"),` (many call sites); `grep -rn "lmms-plugin-logo" src/ plugins/` → no hits. |
| C7 | the JACK and PulseAudio client identity is `Zene Studio` | **TRUE** | `grep -n "Zene Studio" src/core/audio/AudioPulseAudio.cpp` → `129: pa_stream_new( c, "Zene Studio", …)`, `177: pa_context_new( mainloop_api, "Zene Studio" )`; `grep -n "Zene Studio" src/core/audio/AudioJack.cpp` → `174: if (clientName.isEmpty()) { clientName = "Zene Studio"; }`. |
| C8 | user state is **adopted**, not orphaned (`~/.lmmsrc.xml`, `~/Documents/lmms/`) | **TRUE** | `grep -n "adoptConfigFile\|adoptWorkingDir" src/core/ConfigManager.cpp` → definitions at `798`/`831` at HEAD (`790`/`823` at `2239f3cb6`), call sites in the same function; `ls tests/src/core/ConfigMigrationTest.cpp` → present. |
| C9 | the WAV/FLAC software tag is set at `AudioFileWave.cpp:91` and FLAC `:85` | **TRUE (both tips)** | `sed -n '91p' src/core/audio/AudioFileWave.cpp` → `sf_set_string ( m_sf, SF_STR_SOFTWARE, "Zene Studio" );`; `sed -n '85p' src/core/audio/AudioFileFlac.cpp` → `sf_set_string(m_sf, SF_STR_SOFTWARE, "Zene Studio");`. Identical lines at `2239f3cb6`. |
| C10 | a real render's chunk log holds `INFOISFT Zene Studio (libsndfile-1.2.2)` | **TRUE** | `grep -n "ISFT" tests/integration-logs-3d/final/chunk-parse.log` → `3:LIST chunk: INFOISFT Zene Studio (libsndfile-1.2.2)` |
| C11 | 41 shipped identity images were upstream artwork: 39 byte-identical, 2 identical in drawing data, 0 already replaced | **TRUE (doc-internal)** | `docs/BRAND-PLACEHOLDERS.md` lines 14-21: `identical_same_path 24`, `identical_renamed 15`, `art_only 2`, `differs 0`, "**Zero** of the 41 had already been replaced". Not re-derived file-by-file here; the audit table in that document names every file. |
| C12 | `§3c` proves 0 of the 41 remain byte-identical to upstream | **TRUE (doc-internal)** | `docs/BRAND-PLACEHOLDERS.md:290` → "**0 of 41 files remain byte-identical to upstream**, and **0 are unchanged from the pre-change…**" |
| C13 | `§3a` runs the plugin-logo test red and green, resolving to a real 48×48 pixmap | **TRUE (doc-internal)** | `docs/BRAND-PLACEHOLDERS.md:197` → "a real **48×48** pixmap, not the fallback. The red control shows the…" |
| C14 | "`§3b`'s **989**-call-site resource sweep resolves `zene-plugin-logo` … with **0 NEW** unresolved names" | **FALSE** | `python3 tests/brand-resource-sweep.py > /tmp/sweep.log 2>&1; echo $?` → `1`; the log says `call sites scanned: 993`, `UNRESOLVED call sites: 4 (3 pre-existing baseline, 1 NEW)`, `NEW - these fail the sweep: setup <- src/gui/MainWindow.cpp`. The logo probe itself still resolves (`probe 'zene-plugin-logo' -> data/themes/default/zene-plugin-logo.svg`), but the sweep is now **red with 1 NEW**, not green with 0 NEW. (The `setup` reference is `MainWindow.cpp:422`, the Help-menu telemetry consent action.) |

### D. Claims of absence

| # | claim | verdict | command and output |
|---|---|---|---|
| D1 | the Session View is documented as absent from these builds | **TRUE** | `bash tests/release-honesty-gate.sh` on both binaries → `[PASS] session-view OFF matches OFF` / `OFF`; `grep -n WANT_SESSION_VIEW CMakeLists.txt` → default `OFF`. |
| D2 | no clip launcher / no scene launcher / no clip grid | **TRUE** | `grep -rln "SessionClip\|SessionModel\|sessionScheduler\|SceneView" src/gui/` → nothing. |
| D3 | the WASM DSP sandbox is absent | **TRUE for both binaries** (fragile — see B5) | `WANT_WASM='OFF'` in both dumps and in `build/lmmsversion.h`. |
| D4 | offline HTDemucs stem separation is absent | **TRUE** | `WANT_STEM_SPLIT='OFF'` in both dumps and the header. (The separate *stem export* feature is present — `docs/STEM-EXPORT.md`, `src/core/main.cpp:181 "exportstems"`.) |
| D5 | "no out-of-process hosting" | **TRUE** | `grep -rn "out-of-process\|ChildProcess" --include=*.cpp --include=*.h src/ include/ plugins/` → only inside the vendored Carla copy (`plugins/CarlaBase/carla/source/backend/…`). Nothing of the product's own. |
| D6 | "**No instrument hosting in CLAP.**" | **TRUE** | `plugins/ClapEffect/ClapEffect.cpp:51: Plugin::Type::Effect`, `:54: new ClapSubPluginFeatures(Plugin::Type::Effect)`; `ClapSubPluginFeatures.cpp:93: const bool wantInstrument = m_type == Plugin::Type::Instrument;` — the only instantiation passes `Effect`, so `info.isInstrument != wantInstrument` filters every instrument out. |
| D7 | `grep -rn IPlugView src/ include/ plugins/Vst3Effect/ plugins/ClapEffect/` returns **0** | **TRUE** | exactly that command → exit 1, no output. |
| D8 | "the only `IPlugView` occurrences **in the repository** are inside the vendored Carla copy" | **FALSE** | `grep -rn "IPlugView" --include=*.cpp --include=*.h . \| grep -v CarlaBase` → `./plugins/Vst3Instrument/Vst3InstrumentView.h:43: * This is NOT the plug-in's own editor. \`IPlugView\` is not implemented`. The claim from D7 stands; the absolute qualifier does not. Both documents carry it (notes line 92-93, limitations lines 98-99). |
| D9 | VCA groups exist but cannot be created from the interface | **TRUE** | `src/core/VcaGroup.cpp`, `include/VcaGroup.h`, `Mixer::createVcaGroup` (`src/core/Mixer.cpp:720`), save/load `"vcagroup"`+`"vca"` (`:1893,:1898,:2022,:2037`) all present; `grep -rn "createVcaGroup\|VcaGroup" src/gui/` → no creation path (only `MixerView`/`MixerChannelView` reading channels). |
| D10 | racks: no UI and no scripting binding; `<rack>` at `src/core/Rack.cpp:48` | **TRUE** | `sed -n '48p' src/core/Rack.cpp` → `constexpr auto RACK_ELEMENT = "rack";`; no GUI/registry consumer found. |
| D11 | plugin scanning has no GUI; quarantine is a `{"path":…,"reason":…}` JSON entry | **TRUE (doc-internal)** | `docs/PLUGIN-SCAN-CACHE.md` §5 records "no GUI for it yet". |
| D12 | no take lanes and no comping | **TRUE** | `grep -rniI "takelane\|take lane\|comping" src/ include/ \| wc -l` → `0`. |
| D13 | the `automation.mode_set` refusal cites `docs/KNOWN-LIMITATIONS.md:84`, and that line is a save/open-integrity paragraph | **TRUE** | `sed -n '82,86p' docs/KNOWN-LIMITATIONS.md` → the "Related and **fixed in this release too**" failed-open paragraph. |
| D14 | "that file … says at line **185** that the Read/Touch/Latch/Write modes work" | **FALSE** | `sed -n '187p' docs/KNOWN-LIMITATIONS.md` → "- **Automation is not sample-accurate.** Modes work (Read / Touch / Latch / Write) and riding a control in…"; line 185 is the "build an effect fixture" sentence. Off by two. |
| D15 | a 48 kHz sample in a 44.1 kHz project plays about 8.8 % sharp | **TRUE** | `python3 -c "print((48000/44100-1)*100)"` → `8.843537414965997` |
| D16 | "the only reader of `isAgentInstance()` is `UnattendedRun` itself" | **TRUE** | `grep -rn "isAgentInstance" --include=*.cpp --include=*.h .` → `src/core/UnattendedRun.cpp:66` (definition) and `include/UnattendedRun.h:36` (declaration). No other reader. |
| D17 | "there is **no server to send to yet**, so nothing leaves your machine" | **TRUE (doc-internal)** | `docs/TELEMETRY-V1.md:284` — "Crash counts are not wired to the crash reporter…"; the transport is inert by design. |
| D18 | auto-mastering "generates and measures; it does not pick a 'best' — there is no preference scorer" | **TRUE** | `src/core/main.cpp:284` → `// to. Nothing here ranks the candidates or picks one - that is not measured.`; `docs/AUTO-MASTERING.md` §0 → "**No candidate is ranked, scored for preference, or called best**". |
| D19 | no third-party VST3 instrument has been tested; no multi-out, no presets, no instrument latency in PDC | **TRUE** | `docs/VST3-INSTRUMENT-FIXTURE.md:634` → "**Any third-party VST3 instrument.** Still none installed, still not installable here…"; the fixture is the only instrument under `tests/data/`. |
| D20 | "no stable project format"; "no measured crash-free rate" | **UNVERIFIABLE (policy statements)** | Not machine-checkable claims; no command can confirm or refute them. They read as honest hedges and nothing contradicts them. |

### E. Instrument hosting

| # | claim | verdict | command and output |
|---|---|---|---|
| E1 | the 0.1.0-alpha build advertised VST3+CLAP hosting and contained neither | **TRUE (doc-internal)** | `tests/advertised-features.tsv` header: "the published v0.1.0-alpha advertised 'VST3 and CLAP hosting, effects only' while all seven jobs of its release run printed 'VST3 hosting skipped: no SDK' … and shipped neither". |
| E2 | the fixture is `tests/data/vst3-test-instrument/` and the probe is at `tests/CMakeLists.txt:806-821` | **TRUE at frozen / FALSE today** | `git show 2239f3cb6:tests/CMakeLists.txt \| grep -n Vst3InstrumentFixtureProbe` → `806,807,809,812,813,817,820,821`; at HEAD → `825-840`. |
| E3 | the two suites sit behind `WANT_VST3_TEST_INSTRUMENT` (`tests/CMakeLists.txt:796`, default `OFF`) | **TRUE at frozen / FALSE today** | at `2239f3cb6` → `792,796,798`; at HEAD the `option(...)` is at `815-816` with default `OFF` (`sed -n '815,817p'`) — the *default* claim is true, the line is 19 lines out. |
| E4 | `docs/VST3-INSTRUMENT-FIXTURE.md` §0: 47 tests passed, 0 failed, including "No bypass parameter found. This is an instrument." | **TRUE** | `grep -n "Result: 47 tests passed, 0 failed" docs/VST3-INSTRUMENT-FIXTURE.md` → `375`; the validator line is at `356`/`381`. |
| E5 | §5: the fixture consumes `ProcessData::inputEvents` and renders audio: 16/16 checks | **TRUE** | `docs/VST3-INSTRUMENT-FIXTURE.md:21` → "audio in response, sample-accurately: exit 0, 16/16 checks." |
| E6 | `docs/VST3-INSTRUMENT-HOSTING.md` §0 is the lane's verdict (load, MIDI in, audio out, save/reload) | **TRUE** | the document exists and its §0 carries that verdict; the notes quote it accurately. |
| E7 | lane `post-alpha/instrument-hosting-impl` is an ancestor | **TRUE** | `git merge-base --is-ancestor post-alpha/instrument-hosting-impl 2239f3cb6` → exit 0 (and of HEAD). |
| E8 | `docs/INSTRUMENT-VIEW-SAFETY.md` §3: Xvfb run, "Bass" track, window contains "Controls for Zene VST3 Test Instrument", a `Level` knob and the disclosure line | **TRUE** | `docs/INSTRUMENT-VIEW-SAFETY.md:170` → "Product run, shipped binary, Xvfb, a project with a VST3 instrument on the "Bass" track"; `:177` → "`Controls for Zene VST3 Test Instrument`, a `Level` knob, `The instrument's own editor is not shown in this version - these are its parameters.`" |
| E9 | §4: green with the guard, `SIGSEGV` exit 139 at address `0x8` without it | **TRUE** | `docs/INSTRUMENT-VIEW-SAFETY.md:139` → "**SIGSEGV, exit 139**"; `:160` → "Identical address (`0x8`)"; `:255` → "pre-fix : SIGSEGV, exit 139 … post-fix : PASS, all 9 cases, exit 0". |
| E10 | no physical display was used — offscreen and Xvfb only (§6.1 names what a desktop session would add) | **TRUE** | `docs/INSTRUMENT-VIEW-SAFETY.md:291` → "**No physical display was used.** Offscreen and Xvfb; §3 explains why the fix and the crash are…" |

### F. The control surface

| # | claim | verdict | command and output |
|---|---|---|---|
| F1 | `--control-socket <path>`: a file, mode `0600`, in the working directory, nothing on the network | **TRUE** | `timeout 280 python3 tests/agent-surface-gate.py build-coverage/zene tests/data/agent-control-fixture.mmp --check` → `PASS`. The gate's own `connect_session()` asserts `stat.S_IMODE(...) == 0o600` and raises otherwise (it did not); the transport is `socket.AF_UNIX`. The flag is accepted by the binary (the gate launches `[binary, "--config", cfg, "--control-socket", sock]`). |
| F2 | `error.kind` is a closed set — `not_found`, `requires`, `invalid_args`, `busy`, `refused`, `irreversible` | **TRUE** | `include/ControlRegistry.h:47` → the set, verbatim. Observed live in the same sweep: `audio.device_set … not_found`, `plugin.param_get … invalid_args`, `mixer.set_pan … refused`. |
| F3 | "every message carries an integer `proto`" | **TRUE** | `src/core/ControlServer.cpp:79-84` → `protoMatches()` reads `request.value("proto").toInt(-1)` and answers `"unsupported protocol version; this instance speaks proto %1"`; `app.version`'s result schema carries `proto` as an integer (`tools/mcp-zene-control/zene_control/commands_snapshot.json`). |
| F4 | `isUnattendedRun()` is true with no display (offscreen, minimal, VNC) | **TRUE** | `include/UnattendedRun.h:40-41` → "started with `--control-socket`, or Qt runs on a platform with no display (offscreen/minimal/vnc, i.e. a CI run)". |
| F5 | the recovery-file prompt is gated at `src/core/main.cpp:1128` | **TRUE today / absent at the frozen tip** | `sed -n '1128p' src/core/main.cpp` → `if( recoveryFilePresent && lmms::isUnattendedRun() )`; `git show 2239f3cb6:src/core/main.cpp \| grep -c isUnattendedRun` → `0` (the fix merged after the tip the notes claim to be verified at, consistent with A1). |
| F6 | the "never becomes usable" measurement is task #625 on base commit `6b01b98eb` | **TRUE commit / UNVERIFIABLE measurement** | `git cat-file -t 6b01b98eb` → `commit`. The behaviour ("stays alive, answers `control.ping` with `engine_ready: false`, never becomes usable") is a lane measurement recorded in the task, not in the tree; `tests/control-no-audio-device.py` exists and is the script that would reproduce it. |
| F7 | the families list, incl. record-arm and channel pan "registered but **refuse**" | **TRUE** | the sweep shows `track.set_arm` and `mixer.set_pan` reaching a typed result (`refused` observed for `mixer.set_pan`); the ids are in `ControlCommands*.cpp` (F13). |
| F8 | ids are `trk-<n>`, `clip-<n>`, `note-<n>`, `ch-<n>`, `dev-<n>` | **TRUE** | `include/ControlVocabulary.h:89-95` → the `clipId`/`noteId`/… comments and constructors. |
| F9 | `trk-<n>` is assigned at creation and **written into the project file** (`Track.cpp:232`, `:323`) | **TRUE** | `grep -n 'setAttribute( *"id"' src/core/Track.cpp` → `232`; `sed -n '323p' src/core/Track.cpp` → `if (element.hasAttribute("id"))` (the load half). `tests/src/core/StableTrackIdsTest.cpp` present. Both lines absent at `2239f3cb6` (the stable-ids slice is later). |
| F10 | the other ids are index-derived and **not** written into the project file | **TRUE** | `include/ControlVocabulary.h:89-95` documents them as ordinals; no writer of `clip-`/`note-`/`ch-`/`dev-` ids into project XML was found. |
| F11 | the measured baseline: `post-alpha/agent-surface-integration @ 059bf6bad`, **70 commands in 18 groups**, 36 mutating driven, all 36 recorded a transaction, **17 reversible** — `ableton-gap/A16-STATUS-MEASURED.md` line 35 | **TRUE, quoted exactly** | `sed -n '27p;35p' ../ableton-gap/A16-STATUS-MEASURED.md` → "Merged binary `post-alpha/agent-surface-integration` @ `059bf6bad` (70 commands, 18 groups)" and "**Result: 36 mutating commands exercised; all 36 recorded a transaction; 17 are `reversible: true`.**" `git cat-file -t 059bf6bad` → `commit`. Line 35 is exactly that sentence. |
| F12 | the table in `src/core/ControlReversibilityTable.cpp` has **74 rows**: 30 `true_inverse`, 5 `snapshot`, 3 `irreversible`, 36 `not_mutating` | **TRUE** | `grep -cE '^\s*R\("' src/core/ControlReversibilityTable.cpp` → `74`; `grep -oE 'R\("[a-z_.]+", RC::[A-Za-z]+' … \| awk -F'RC::' '{print $2}' \| sort \| uniq -c` → `30 TrueInverse / 5 Snapshot / 3 Irreversible / 36 NotMutating`. |
| F13 | "the same **74** ids are registered in `src/core/ControlCommands*.cpp`" | **TRUE** | `grep -rhoE '"[a-z_]+\.[a-z_]+"' src/core/ControlCommands*.cpp \| sort -u` → 76 strings, of which `"group.verb"` (a doc-comment example) and `"lmmsversion.h"` (an include) are not ids ⇒ **74**. |
| F14 | "It held **72** before the telemetry fix that added the two `telemetry.*` commands" | **TRUE** | `grep -oE 'cmd\.id = QStringLiteral\("[a-z_.]+"\)' src/core/ControlCommandsTelemetry.cpp` → exactly `telemetry.consent`, `telemetry.status`; 74 − 2 = 72. |
| F15 | "The A16 contract's classified table has **72** rows (30 + 5 + 3 + 34)" | **TRUE** | `docs/A16-REVERSIBILITY.md:31-35` → `true_inverse 30`, `snapshot 5`, `irreversible 3`, `not_mutating 34`, **total 72**. |
| F16 | the bridge's committed snapshot holds **70** | **TRUE** | `python3 -c "import json;d=json.load(open('tools/mcp-zene-control/zene_control/commands_snapshot.json'));print(len(d))"` → `70` |
| F17 | the live registry in this build holds **74**, that is what `control.commands_list` returns, and the gate sweeps **73 + 1 allowlisted** (`telemetry.consent`) | **TRUE** | the gate run prints `control.commands_list: 74 registered commands` and `reverse : 74 commands, 73 swept, 1 allowlisted`; `cat tests/agent-surface-allowlist.txt` → one entry, `telemetry.consent  requires: display, human …`. |
| F18 | "**87** is what a wider surface branch registers by its own method (71 + 16)" | **UNVERIFIABLE FROM THIS BOX** | The branch and its method are not in this repository. `grep -rn "\b87\b" docs/ ../ableton-gap/` finds no 71+16 tally. What would settle it: the branch's own `control.commands_list`, or the note that records the count. |
| F19 | bounds: 100 records, 256 KiB total, 64 KiB ceiling — `include/ControlReversibility.h:143,149` | **TRUE, with a citation imprecision** | `sed -n '143p;147p;149p' include/ControlReversibility.h` → `constexpr int MaxTransactionRecords = 100;` (143), `… 65536` (147), `constexpr int MaxTransactionBytes = 262144;` (149). The 64 KiB ceiling is at **:147**, and the citation lists 143/149 only. |
| F20 | "FIFO eviction whose evictions are **counted and reported** rather than hidden (… `src/core/ControlCommandsArrangement.cpp:53`)" | **FALSE (misattributed line)** | `sed -n '53p' src/core/ControlCommandsArrangement.cpp` → `constexpr int MaxTrackSnapshotChars = 65536;` — the 64 KiB track-snapshot cap, not the eviction code. `grep -n "evict" src/core/ControlCommandsArrangement.cpp` → `225, 229, 258`. The behaviour is plausibly real; the line cited for it is not where it lives. |
| F21 | "Every command is a normal edit that the application already knows how to do — **the registry and the menus call the same implementation**" | **FALSE / overstated** | the same gate run prints `control.surface_report: 46 reflected actions` and `reflection : 4 registered, 42 unregistered (42 grandfathered, 0 new)`. 42 of the 46 menu/toolbar actions the gate reflects do **not** resolve to a registered command. The reverse direction (every registered command is swept) is what the gate proves; the sentence asserts the forward direction too. |
| F22 | `project.save` keeps `keep-3` revisions, 8 MiB each, 24 MiB per project | **TRUE** | `docs/A16-REVERSIBILITY.md:278` → "**3 x 8 MiB = 24 MiB**. A project file over the per-revision cap is NOT copied…". |
| F23 | `AutomatableModel::setValue()` pushes a checkpoint per write — `src/core/AutomatableModel.cpp:305` | **TRUE (both tips)** | `sed -n '305p' src/core/AutomatableModel.cpp` → `if (!isAutomated) { addJournalCheckPoint(); }` |
| F24 | `plugin.unload` cannot be replayed; the chain order is not restored | **TRUE** | `docs/A16-REVERSIBILITY.md:103` → "…would need plugin.load by catalogue id plus a state restore, and the instance id (fx-<n>) is position-derived - the inverse is not one operation the registry can run… **The chain ORDER is not restored**". |
| F25 | the contract's own limits: undo depth 100 for both stacks; `track.remove` inverse bounded at 64 KiB; `track.set_solo` does not restore `Track::mutedBeforeSolo`; neither survives a restart | **TRUE** | `docs/A16-REVERSIBILITY.md:336-343` → "1. **Undo depth is 100 steps** … 2. **`track.remove`'s inverse is bounded at 64 KiB of track XML.** … 4. **`track.set_solo` does not restore `Track::mutedBeforeSolo`**…" |
| F26 | the surface's tests run on the Dummy device; `tests/control-no-audio-device.py` proves the fallback; no test drives a real backend | **TRUE** | `ls tests/control-no-audio-device.py` → present; `ctest -N` in `build-coverage/tests` → `Test #89: ControlHeadlessNoAudioDevice`; no test in the tree opens a real device. |
| F27 | `CMDN-REPORT.md` and `CMDN-TRANSCRIPT.md` are "in the repository root" | **TRUE** | both files are in the worktree root. |

### G. "What else is new"

| # | claim | verdict | command and output |
|---|---|---|---|
| G1 | a crash reporter, offline and local | **TRUE** | `ls src/core/CrashReporter.cpp docs/CRASH-REPORTER.md` → both present. |
| G2 | autosave: recovery gated on the file belonging to this project and being newer | **TRUE (doc-internal)** | `docs/AUTOSAVE-RECOVERY.md` present; the gating is the fix the document records. |
| G3 | plugin scanning with a cache and a quarantine list, user-facing surface is a data layer | **TRUE** | `docs/PLUGIN-SCAN-CACHE.md` present, §5 "no GUI for it yet"; `docs/CRASH-REPORTER.md`/`PLUGIN-SCAN-CACHE.md` both in the tree. |
| G4 | a loudness meter and a loudness report on export, cross-checked against a reference implementation | **TRUE (doc-internal)** | `docs/LUFS-METER.md`, `docs/LUFS-WIRING.md` present; the BS.1770-4 meter is the one `zene master` scores with. |
| G5 | MIDI learn: the binding is saved in the project | **TRUE** | `docs/MIDI-LEARN.md` present; `src/gui/MidiLearnGui.cpp` present. |
| G6 | stem export: a project's tracks as separate files | **TRUE** | `docs/STEM-EXPORT.md` present; `src/core/main.cpp:181` → `"  exportstems <project> [options...]    Export each track as its own stem file\n"`. |
| G7 | automation modes Read/Touch/Latch/Write, and sample-accurate playback is **not** in | **TRUE** | `docs/AUTOMATION-MODES.md` present and states per-tick evaluation; `docs/KNOWN-LIMITATIONS.md:187-191` says the same. |
| G8 | a clip model that survives a playback pass | **TRUE (doc-internal)** | `docs/CLIP-SLICE0.md` present. |
| G9 | warp engine: `WarpMarkers` a child of `<sampleclip>`, monotonic map, 0/0.5/1.0/1.5 s render; lane `post-alpha/warp` an ancestor | **TRUE** | `docs/WARP.md` present with that §0; `git merge-base --is-ancestor post-alpha/warp 2239f3cb6` → exit 0. |
| G10 | note probability and velocity jitter, seeded and saved per note, plus a search-and-transform operation | **TRUE (doc-internal)** | `docs/MIDI-DEPTH.md` present; the note-level fields are in the tree (not re-derived note-by-note here). |
| G11 | racks: `<rack>` element, lane `post-alpha/racks` an ancestor | **TRUE** | `sed -n '48p' src/core/Rack.cpp` → `constexpr auto RACK_ELEMENT = "rack";`; ancestor check → exit 0. |
| G12 | VCA: `Mixer::createVcaGroup` at `:720`, gain at `:528-545`, save/load `vcagroup`+`vca` at `:1893,:1898,:2022,:2037`, lane an ancestor | **TRUE** | `sed -n '720p;528,545p;1893p;1898p;2022p;2037p' src/core/Mixer.cpp` → all five quoted correctly; ancestor check → exit 0. |
| G13 | MPE stored as `mpepitch`/`mpepressure`/`mpetimbre`, pitch applied in `NotePlayHandle.cpp`, `src/core/midi/MpeExpression.cpp` in the tree, lane an ancestor | **TRUE** | `ls src/core/midi/MpeExpression.cpp` → present; `docs/MPE.md` §0 records the same split; ancestor check → exit 0. |
| G14 | telemetry: 24-key closed allowlist, `ZENE_TELEMETRY` at `CMakeLists.txt:140` default `ON`, the three source files present, lane an ancestor, and the option never reaches the build-options dump | **TRUE** | `docs/TELEMETRY-V1.md:94` → "## 3. The payload allowlist (24 keys, closed)"; `sed -n '140p' CMakeLists.txt` → `option(ZENE_TELEMETRY "…" ON)`; `ls src/core/Telemetry.cpp src/core/TelemetryNetworkTransport.cpp src/gui/TelemetryConsentDialog.cpp` → present; `sed -n '6,14p' src/CMakeLists.txt` → the `^WANT|LMMS_(HAVE\|DEBUG)` assembly, which `ZENE_TELEMETRY` does not match; ancestor check → exit 0. |
| G15 | `zene master`: subcommand at `src/core/main.cpp:371`, usage `:177`, options `:230-237`, one line per candidate `:281-286` | **TRUE at the frozen tip / FALSE today** | `git show 2239f3cb6:src/core/main.cpp` → usage `177`, dispatch `371`, options `237-244`, candidate print comment `275`; at HEAD → `182`, `378`, `237-244`, `282-284`. The `:371`/`:177` pair is the frozen tip's numbering; the option block and the per-candidate comment are a few lines out even there (`:237-244` and `:275`, not `:230-237`/`:281-286`). |
| G16 | auto-mastering: **one** render feeds **five** candidates, each scored, explicitly not ranked | **TRUE** | `docs/AUTO-MASTERING.md` §0 → "one project render feeds five mastering candidates … **No candidate is ranked**…"; task #610 as claimed. |
| G17 | scripting/git tooling: a versioned Lua API with a compatibility policy and console, a deeper `.mmpz` merge driver | **TRUE (doc-internal)** | `docs/LUA-API-STABILISATION.md`, `docs/LUA-COMPATIBILITY-POLICY.md`, `docs/LUA-PACKAGE-FORMAT.md`, `docs/MMPZ-GIT-DEPTH.md` all present. |

### H. "Fixed since the rehearsal build"

| # | claim | verdict | command and output |
|---|---|---|---|
| H1 | the exit abort: reproduced **31 times in 184 suite runs**; teardown ~9.5 s on a 20-core box because the engine waited **500 ms per worker** and gave up on **19 of them**; one worker misses the single wake | **TRUE** | `docs/TEST-HYGIENE.md:36` → "Pre-fix totals: **31 of 184 `PdcMixerTest` runs aborted (16.8 %)**"; `:82` → "One worker, parked on a wait condition, out of `QThread::idealThreadCount() - 1` = **19** on this box"; `:9`/`:137` → the `wait(500)`; `:493` → "20 cores". 19 × 500 ms = 9.5 s. |
| H2 | the fix is `AudioEngineWorkerThread.cpp:204` + `AudioEngineTeardownTest.cpp:135-148`, `:170-179`, asserting `stranded == 0`; red shows `stranded: 8` of 8, green 4/4 | **TRUE** | `sed -n '204p' src/core/AudioEngineWorkerThread.cpp` → `// stranded in wait())…` (the bounded re-check comment); `sed -n '135,148p;170,179p' tests/src/core/AudioEngineTeardownTest.cpp` → `int stranded = 0; … ++stranded;`; `docs/TEST-HYGIENE.md:170` → "Actual (stranded): 8", `:175` → "all 8 workers left parked", `:177` → `EXIT=0`. The "4/4" phrasing is not literally in the document but the green runs are (`logs/teardown-green.log`). |
| H3 | under load: **30 parallel suite runs, 46/46 passing each, 0 aborts, 1,380 test executions** | **TRUE** | `docs/TEST-HYGIENE.md:232` → "`46/46 passed` — 1 380 test executions with no abort and no failure."; `:13` → "0 of 30 full-suite runs under load after". |
| H4 | four test sources recovered (`PhaseDSidechainTest`, `PhaseFChannelScaleTest`, `MixerRoutingBackwardCompatTest`, `MixerAbRegressionTest`); suite **41 → 46**; gate `tests/unregistered-tests-gate.sh` (Gate 10) wired at `tests/run-all-gates.sh:186` | **TRUE** | `docs/TEST-HYGIENE.md:393-398` → the four, with their pass counts, and "Suite test count on this branch: **41 → 42** (the new teardown test) **→ 46**"; `sed -n '186p' tests/run-all-gates.sh` → `bash tests/unregistered-tests-gate.sh`. The notes themselves flag that the headline "41 → 46" is really 41→42→46. |
| H5 | render determinism: on the nine projects swept, **six differed on every run and a seventh intermittently**, worst on **98.3 %** of frames; now **7 of 9** bit-reproducible, five subsequent renders byte-identical; the two are `demos/StrictProduction-DearJonDoe.mmp` and `shorties/Root84-TrancyLoop.mmpz` | **TRUE, with one mis-phrasing and one path imprecision** | `docs/RENDER-DETERMINISM.md:13` → "bytes on every run of the same binary; a 7th does so intermittently. The worst differs on" (with `:131` `98.3 %` for `Root84-TrancyLoop`); `:22` → "**7 of 9 bundled projects are now bit-reproducible**". **Mis-phrasing:** the notes say "the nine projects **this repository bundles**" — `git ls-files '*.mmp' '*.mmpz' \| wc -l` → **68**; nine is what the sweep covers, not what the repository bundles. **Path imprecision:** the files are at `data/projects/demos/StrictProduction-DearJonDoe.mmp` and `data/projects/shorties/Root84-TrancyLoop.mmpz` (`find . -name "StrictProduction*" -o -name "Root84*"`), not at `demos/…`/`shorties/…`. |
| H6 | the two non-reproducible projects resist six falsification experiments (§10), and for `Root84` bypassing all twelve effect chains changes nothing (§9 is the sweep) | **TRUE** | `docs/RENDER-DETERMINISM.md:330` → "all 12 effect chains bypassed (`on=\"0\"`, `fxchain enabled=\"0\"`) \| 2/2 distinct"; §10 lists the six experiments (`:325-332`). |
| H7 | the recorder clamp: `TrackRecorder.cpp:190` `std::clamp(m_writeScratch[i], sample_t{-1}, sample_t{1})`, the refusal comment at `:99-101`, and `SFC_SET_CLIPPING` already present at `AudioFileWave.cpp:89` / `AudioFileFlac.cpp:83` | **TRUE** | `sed -n '190p' src/core/audio/TrackRecorder.cpp` → the clamp, verbatim; `sed -n '99,101p'` → the comment explaining the library flag was refused for the 1-LSB in-range shift; `sed -n '89p' src/core/audio/AudioFileWave.cpp` → `sf_command(m_sf, SFC_SET_CLIPPING, nullptr, SF_TRUE);`; `sed -n '83p' src/core/audio/AudioFileFlac.cpp` → the same. |
| H8 | the capture path no longer takes the model lock: a push blocked 600 ms; the ring: pushed 1,024,000 → staged 16,384, dropped 1,024,000; `RecordRingBuffer` at `TrackRecorder.h:41,:66,:110`, consumed off the audio thread `:81-85` | **TRUE** | `sed -n '183p;196p;201p;303p;313p;317p' docs/RECORDING-REALTIME-FIXES.md` → "bounded 600 ms", "pushInputFrames took 600 ms while the model lock was held for 600 ms", `AudioEngine::InputStageCapacityFrames (16384)`, `pushed 1024000 frames: staged 16384 -> 16384, dropped 1024000`; `sed -n '41p;66p;110p' include/TrackRecorder.h` → the include, `RingCapacityFrames = 1u << 16`, `std::unique_ptr<RecordRingBuffer> m_ring`. `:66` names the capacity constant rather than the type; the ring type is at `:41`/`:110`. |
| H9 | a project saved without the Session View feature no longer silently loses that data | **TRUE** | `ls tests/src/core/SessionModelTest.cpp` → present, with a `<session> … </session>` byte-exact save/load/save round-trip helper. |
| H10 | mixer concurrency: defects D1, D2(ii), D2(iii), D3, D4, D5, D6 confirmed live, inherited from upstream, fixed with TSan before/after; `tests/run-mixer-concurrency-tsan.sh`; three suppressions in `tests/mixer-concurrency-tsan.supp` | **TRUE** | both script files present; `docs/MIXER-CONCURRENCY-FIXES.md` names D1-D6 and the TSan switch `-DCMAKE_BUILD_TYPE=Debug -DWANT_DEBUG_TSAN=ON`. |
| H11 | a failed save is refused and reported: `DataFile.cpp:461-490` checked sequence, `.bak` move `:494`, rename `:510`, rollback `:516`, `writeFile()` false at `:485,:494,:510,:523` | **TRUE at the frozen tip / FALSE today** | `git show 2239f3cb6:src/core/DataFile.cpp \| grep -n "QFile::rename(fullName"` → `494, 510, 516` exactly; at HEAD → `502, 518, 524`. The "checked sequence" range is right at the frozen tip (`return false` present in 461-490 at both tips). |
| H12 | `control.undo` used to SIGSEGV: null deref in `PatternStore::updateComboBox()` `src/core/PatternStore.cpp:203`, reached from `ProjectJournal::undo()`; the GUI's Ctrl+Z is `Engine::projectJournal()->undo()` at `src/gui/MainWindow.cpp:1417-1420`; five reads in `src/tracks/PatternTrack.cpp`, one in `include/PatternTrack.h`, two in `src/gui/tracks/PatternTrackView.cpp`; declared in `tests/upstream-modifications.txt`; render `943e3238…`, data chunk `b37cefc5…` | **TRUE (with one note)** | `grep -n "updateComboBox" src/core/PatternStore.cpp` → definition `194`, and line `203` is the inserted guard `if (pt == nullptr)` (the site the crash frame named); `sed -n '1417,1420p' src/gui/MainWindow.cpp` → `Engine::projectJournal()->undo();`; `grep -c "\.value(" src/tracks/PatternTrack.cpp include/PatternTrack.h src/gui/tracks/PatternTrackView.cpp` → `9 / 1 / 2`, and `tests/upstream-modifications.txt:593-596` names the five registry reads plus the one and the two; `docs/CONTROL-UNDO-CONNECTION-DROP.md:269,272` → `943e3238…` ×2 and `chunk data payload sha256=b37cefc5…`. The `:203` citation is the crash frame, not today's deref — today's line 203 is the guard. |
| H13 | an optional MCP bridge ships for the control surface; it is a client whose tool list is generated from the instance's live registry, with a cache then a committed snapshot | **TRUE** | `ls tools/mcp-zene-control/README.md tools/mcp-zene-control/zene_control/registry.py` → present; `registry.py:1-16` → "tools are GENERATED from `control.commands_list`", with `<state-dir>/commands.json` and the committed `commands_snapshot.json` (70 entries, F16). |
| H14 | the MIDI-learn use-after-free: "a segfault at `0x188`", found when the coverage gate was turned on, "the file now measures **86 %**" | **UNVERIFIABLE FROM THIS BOX** | `grep -rn "0x188" tests/ docs/` finds no record outside the release notes themselves; no coverage record for `src/gui/MidiLearnGui.cpp` is in the tree's logs. The file and the guarded-pointer fix exist; the address and the percentage are the lane's measurements. What would settle it: the lane's backtrace log and a coverage capture for that file. |

### I. "Your projects" and "Coming from 0.1.0-alpha"

| # | claim | verdict | command and output |
|---|---|---|---|
| I1 | the `.bak` is `<project file>.bak`; `DataFile.cpp:347` composes it, `:435` moves the file there, `:438` renames into place, `:427` is the `app/disablebackup` read | **FALSE in the tree; the numbers are from the release-prep base only** | `git show 34c1f4f86:src/core/DataFile.cpp \| grep -n "fullNameBak = fullName"` → `347`; the same at `2239f3cb6` → `383`, at HEAD → `391`. `disablebackup` → `425` (base), `472` (frozen), `480` (HEAD) — never 427. The two renames are at `494/510` (frozen) and `502/518` (HEAD), never 435/438. |
| I2 | `creatorversion` is written at `:140` and `:2105`, read back at `:2179` | **FALSE at every revision checked** | `grep -n 'attribute( "creatorversion"' src/core/DataFile.cpp` at HEAD → `141, 328, 2212, 2286, 2291, 2339`; at `2239f3cb6` → `140, 320, 2195, 2269, 2274, 2322`; at `34c1f4f86` → writes `140`, `…` and read at `…`. `:2105` and `:2179` are not `creatorversion` lines in any of the three. |
| I3 | `legacyFileVersion()` is at `:2226-2238` | **TRUE at the base / FALSE later** | `git show 34c1f4f86:src/core/DataFile.cpp \| grep -n "legacyFileVersion()$"` → `2225` (so a 2226-2238 body is right); at `2239f3cb6` → `2318`; at HEAD → `2335`. |
| I4 | an older 1.3-alpha build will open a 0.2 project and silently drop parts of it | **UNVERIFIABLE FROM THIS BOX** (and the document says so) | No LMMS 1.3.0-alpha binary on this machine. The *source-level* half is checkable and checks out: `git show origin/master:src/core/Note.cpp \| grep -c slide` → `0`; `git grep -l sidechain-send origin/master -- src/` → no matches; `git grep -c prefader origin/master -- src/` → no matches. **Partial counter-example to the "and nothing else" clause:** `git show origin/master:src/core/Song.cpp \| grep -nE 'nodeName\(\) =='` also matches the GUI editors' node names (`controllerRackView` 1138, `pianoRoll` 1142, `automationEditor` 1146, `projectNotes` 1150, the timeline 1154) besides `track`/`trackcontainer`/`controllers`/`scales`/`keymaps`. The claim is true of the Song-container dispatch; "and nothing else" is not literally true of the file. |
| I5 | `ConfigMigration::adoptConfigFile`/`adoptWorkingDir` at `src/core/ConfigManager.cpp:790`/`:823`, called from `:709-790`; declared in `include/ConfigManager.h:319-336`; commit `6c1ff660c`; lane `post-alpha/rename-complete` an ancestor | **TRUE at the frozen tip / line refs stale today; the header range is FALSE** | at `2239f3cb6`: `adoptConfigFile` at `711,723,774,790` and `adoptWorkingDir` at `709,733,738,823` (so 790/823 are the definitions and 709-790 the call range — as claimed); at HEAD: `719,731,782,798` / `717,741,746,831`. `include/ConfigManager.h` declares both at **`338-339`**, not 319-336 (the 319-336 block is the explanatory comment). `git cat-file -t 6c1ff660c` → `commit`; ancestor check → exit 0. |
| I6 | the notes' own footnote (and the limitations page) report that "the first headline's residue list higher up this file **still lists user state** … as a deliberate residual 'because renaming it would orphan…'", and `docs/WAVE-R-RENAME.md` §6 says the same | **FALSE for the release notes' half** | `grep -n "orphan\|lmms-workspace" docs/RELEASE-NOTES-v0.2.0-alpha.md` → the only headline hit is line 45, which says the **opposite**: "**Migrated rather than kept**: user state … Earlier text here said renaming it 'would orphan an existing install's settings and projects'; **that is no longer the case**". `lmms-workspace` appears only in the footnote itself (line 588). The `docs/WAVE-R-RENAME.md` §6 half is true (`:254-255` still gives the orphaning reason). **Both documents therefore accuse the notes of an error the notes have already fixed** — the "reported rather than silently harmonised" note is itself stale. |

---

## The FALSE claims, most serious first

**1 — A11: "the release-honesty check enforces that everything in the release notes is present in this
build" (limitations, lines 146-147) and freeze item 4 is "Done".** The check fails on both binaries
present: `3 of 6` on the shipping tip (`WANT_VST3='AUTO'`, `WANT_CLAP='AUTO'`), `1 of 6` on the release
configuration (`WANT_SESSION_VIEW='ON'`). This is the exact failure the project shipped once already. **Fix:**
do not ship this sentence as written — either configure the release jobs so the binary the gate reads
reports `ON` for the three hosts and rebuild without the stale `WANT_SESSION_VIEW=ON` cache entry, or state
the two machines' results and delete the "enforces" clause. Freeze item 4 must come off "Done" either way.

**2 — D8: "the only `IPlugView` occurrences in the repository are inside the vendored Carla copy".**
`plugins/Vst3Instrument/Vst3InstrumentView.h:43` is a product file that names `IPlugView`. The claim it
supports (no plug-in editor) is true and separately proven (D7). **Fix:** replace the absolute with the
searched path list, or name the comment.

**3 — C14: the `§3b` sweep "resolves `zene-plugin-logo` … with `0 NEW` unresolved names".** The tree's own
sweep now scans 993 call sites, exits 1, and reports **1 NEW** (`setup` ← `src/gui/MainWindow.cpp`). The
evidence command the notes cite runs red today. **Fix:** re-run the sweep at the release tip, then either
resolve `setup` or record it, and update the count and the "0 NEW" figure.

**4 — C1, C2, C3, C4, C5, E2, E3, G15, H5, H11, I1, I5 and A1's family: line numbers taken from the
release-prep base or the frozen tip and left in a text that says "verified against this tree".** A single
`git show 34c1f4f86:<file>` versus `sed -n` on the worktree settles thirteen of them; the worst are the
`.bak`/`creatorversion` quartet (I1-I3), which are wrong at *every* revision checked, and `ControlVocabulary.h`
(A1), which did not exist at the tip the notes name. **Fix:** re-derive every `file:line` at the commit the
release will be cut from, or drop the line numbers and cite symbols.

**5 — I6: both documents report a stale half that no longer exists in the notes.** **Fix:** delete the
footnote and the limitations paragraph's parallel sentence, or point them at `docs/WAVE-R-RENAME.md` §6
alone (which is still stale and does still need the decision).

**6 — F21: "the registry and the menus call the same implementation".** The surface gate reflects 46
actions, of which **4** resolve to a registered command and 42 are baselined as unregistered. **Fix:**
qualify the sentence to the reverse direction the gate proves, or to the four declared actions.

**7 — F20: the eviction bound cited at `ControlCommandsArrangement.cpp:53`.** That line is
`MaxTrackSnapshotChars = 65536`; the eviction code is at `225/229/258`. **Fix:** correct the line.

**8 — D14: "line 185 says the modes work".** The modes sentence is line **187**. **Fix:** correct the line.

**9 — H5: "the nine projects this repository bundles".** The repository bundles **68** `.mmp`/`.mmpz`
projects; nine is the determinism sweep's sample. **Fix:** "the nine projects the determinism sweep covers".
The same bullet's `demos/…`/`shorties/…` paths are `data/projects/demos/…` and `data/projects/shorties/…`
in the tree — quote the full paths once so a reader can open them.

**10 — I5's header half: "declared in `include/ConfigManager.h:319-336`".** The declarations are at
`338-339`. **Fix:** correct the range.

**11 — A4: "the built header reports `0.1.0-alpha.241+2239f3c` (`build-coverage/lmmsversion.h`)".** The
header in the tree says `0.1.0-alpha.306+18c6da1`. **Fix:** quote the header that is actually on the box, or
the artefact's own `--version`.

**12 — A8: "`build/CPackConfig.cmake:45` produces `zene-0.1.0-alpha.123+34c1f4f-linux-x86_64`".** The file
says `zene-0.1.0-alpha.247+f68cf8e-linux-x86_64`. **Fix:** say which build directory the string comes from.

## UNVERIFIABLE FROM THIS BOX (and what would settle each)

| # | claim | what would settle it |
|---|---|---|
| A2 | "16 markers" | the marker table with its count method (the drafts, or `docs/RELEASE-PREP-0.2.0.md` §3 counted explicitly); the two documents already disagree on whether one was deleted. |
| A6 | the untagged `0.1.0-alpha.123+34c1f4f` string | a build of `34c1f4f86`, or its `lmmsversion.h` in a build directory. |
| D20 | "no stable project format", "no measured crash-free rate" | policy statements; nothing to run. |
| F6 | task #625's "never becomes usable" measurement | the task record, or `tests/control-no-audio-device.py` run with the engine's device deliberately unopenable on the shipping tip. |
| F18 | "87 = 71 + 16" for the wider surface branch | that branch's own `control.commands_list` or the note that records the tally. |
| H14 | the MIDI-learn "segfault at `0x188`" and "86 %" | the lane's backtrace log and a coverage record for `src/gui/MidiLearnGui.cpp`. |
| I4 | "an older 1.3-alpha build will open a 0.2 project and silently drop parts of it" | a LMMS 1.3.0-alpha binary (the document already says this). |

---

## The checks that would have caught something — and did

- **The honesty gate, run rather than cited.** The single most valuable command in this audit:
  `bash tests/release-honesty-gate.sh --dump <binary> --artifacts <build>/plugins`. It turned a "Done" into
  a FAIL without needing to read a line of source.
- **`git show <rev>:<file>` beside `sed -n` on the worktree.** Thirteen line references are true of an
  earlier commit and false in the tree; only this comparison separates "stale" from "always wrong".
- **Running the evidence command the document itself names.** `python3 tests/brand-resource-sweep.py` exits
  1 today, which no amount of reading the brand document would have revealed.
- **The absolute qualifier sweep.** "the only … in the repository", "and nothing else", "every command",
  "no … even with it on": three of the four failed on a counterexample found by one grep each (D8, F21,
  I4); the fourth (A13) survived.
- **Counting with the thing counted named.** The command counts in the notes are the best part of the
  document — each number names its method and they reconcile (F12-F17). The two counts that do not name
  their universe are the ones that fail: "the nine projects this repository bundles" (H5) and "16 markers"
  (A2).
- **Reading the limitations page as a file rather than as a reference.** The brief asked for
  `docs/KNOWN-LIMITATIONS-v0.2.0-alpha.md`; it does not exist. If the release process expects that path to
  be attached to the release, that is itself a finding for the parent — the page that exists is
  `docs/KNOWN-LIMITATIONS.md`.
