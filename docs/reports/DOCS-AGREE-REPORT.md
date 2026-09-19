# DOCS-AGREE-REPORT — board card 691

**Pass every number in the four release documents against the release binary, each with the command that
produced it, each naming its universe, and the release's promise stated in as many words.**

Lane `030/docs-agree`, worktree `zene-030/wdocs`, base `08d96d612` (the 0.3.0 release tip; run #8 green on it).
**Nothing here is pushed**; the parent lands the branch after 690.

Corrections are on this branch, one commit per document or claim class:

| commit | file(s) | what |
| --- | --- | --- |
| `864f5ad9d` | `README.md` | every number and absence claim re-derived from the binary |
| `d9bcfd245` | `docs/KNOWN-LIMITATIONS.md` | the version it ships with, the CLAP-on-Windows contradiction, three stale counts |
| `002ec87e1` | `docs/STATUS.md` | the 0.2.1 picture marked as such + the five lines that read as current |
| `1c6fbfc62` | `docs/RELEASE-NOTES-v0.3.0-alpha.md` | three claims true on their day, re-taken at this tip |
| `22e1069df` | all four | the promise, stated in as many words |
| `6ca3b018d` | `docs/KNOWN-LIMITATIONS.md` | the telemetry kill switch's registry delta, re-based |
| `ee5fdb19e` | `README.md`, `docs/KNOWN-LIMITATIONS.md`, `docs/STATUS.md` | the CI counts: six jobs, seven platform builds, four dispatch-only |
| (this commit) | `docs/RELEASE-NOTES-v0.3.0-alpha.md`, `docs/STATUS.md`, this report | the A16 configuration sentence and the STATUS scope notes |

## 1. The instrument — what "the artifact" is, and the universes

| | |
| --- | --- |
| **Binary** | `zene-030/build/zene` — `sha256 2eed203147678d3eec7e774d67b555eed95f01bcc96124b423c7c65aa53a7cf6`, built 2026-09-17 |
| **Version it reports** | `Zene Studio 0.2.1-alpha.612+a039d26` (Linux x86_64, Qt 6.4.2, GCC 13.3.0) — the CMake version strings at `08d96d612`; the bump to 0.3.0 is the live `030/version-bump` lane's |
| **Configuration (from `--version`)** | `RelWithDebInfo`, `WANT_SESSION_VIEW='ON'`, `WANT_WASM='ON'` (+ wasmtime via `LD_LIBRARY_PATH=zene-030/third_party/wasmtime/lib`), `WANT_VST3='ON'`, `WANT_VST3_TEST_INSTRUMENT='ON'`, `WANT_CLAP='ON'`, `WANT_STEM_SPLIT='OFF'`, `ZENE_TELEMETRY=ON` (cache) |
| **"Reference configuration"** (= what the docs' A16 figure is measured in, and what this box builds) | telemetry in, session view in, wasmtime in, stem separation out |
| **"Release configuration"** (= what the six build jobs of `.github/workflows/build.yml` produce) | telemetry in (default), session view in (default since 0.3.0), wasmtime **out** (no job provisions the C API — `grep -rn wasmtime .github/workflows/` is empty; `CMakeLists.txt:962-971` degrades `WANT_WASM` to OFF), stem separation out, VST3 + CLAP in |
| **The instruments** | the live registry (`control.commands_list` over `--control-socket`), the tree's own proof scripts, `ctest -N`, `git ls-files`, the committed snapshot |

Harness: the tree's own (`tests/control_socket_harness.py`), booting the real binary under
`QT_QPA_PLATFORM=offscreen` and driving it from an external client — no browser, no GUI, no plan.

## 2. What "every number" was taken to mean (the census)

`README.md`, `docs/STATUS.md`, `docs/KNOWN-LIMITATIONS.md` and `docs/RELEASE-NOTES-v0.3.0-alpha.md` hold
**1146 lines that contain a digit** (202 / 371 / 1617 / 2878 lines total, respectively). Every one was
extracted (recipe in §6) and triaged. The ones that are **not** rows below are:

* code paths, file paths, line numbers, commit SHAs, dates and issue numbers (`src/core/Song.cpp:404`);
* the dated per-lane measurements inside the release notes' "added 2026-09-xx" sections, which say in their
  own words that they are that lane's tip's figures — the ones a reader would take as *current* were
  re-taken at this tip and are rows below;
* the historical registers (`docs/STATUS.md`'s "claims that were false", "the numbering problem",
  `ITEM-NUMBERING-CROSSWALK`, the Bar-2 integers), which are explicitly about other trees and are not
  re-measurable from this one.

The countable claims that remain are the rows below: **67 rows — 25 agree, 33 disagree with this tip (every
one either fixed, or superseded in place with the measured figure stated beside it), 7 cannot-verify here, and
2 scoped as another tree's history and deliberately left.** Judgement-based rows are marked. **All line
numbers are the pre-fix (`08d96d612`) numbering** — grep for the quoted text, not the number, since the fixes
shifted lines in every file.

## 3. The claim table

Verdicts are mine, measured on the artifact above; every row's "universe" names what the number was counted
over. Lines are from `08d96d612` (the numbers in brackets are post-fix line numbers where the fix moved them).

### 3.1 `README.md`

| # | claim | command | result | universe | verdict |
| --- | --- | --- | --- | --- | --- |
| R1 | `:7` "This repository is the **0.2.1-alpha** tree" | `grep -n VERSION_ CMakeLists.txt`; `build/zene --version` | `0.`/`2`/`1`/`alpha`; binary reports `0.2.1-alpha.612+a039d26` | `CMakeLists.txt` @ `08d96d612` | **agrees with the artifact; not edited — owned by `030/version-bump`** (see §8) |
| R2 | `:47` "19 command groups holding 74 commands" | boot over `--control-socket`, `control.commands_list` | **340 ids in 53 groups** (reference config); **332 in 52** (release config = the committed snapshot) | every id the binary registers | **disagrees → fixed** |
| R3 | `:51` "Three commands ... refuse every call" | live calls, in order | `track.set_arm` → **ok** (`armed:true`, take file opened); `automation.mode_set` → **ok** (`read`→`touch`); `mixer.set_pan` → **`kind:"refused"`** | one live instance, three commands | **disagrees → fixed** (one, not three) |
| R4 | `:50` "each command carrying a JSON schema and a declared reversibility class (the A16 contract)" | `bash tools/dawproject-proof.sh` §2 | `MEASURED rows=340 true_inverse=158 snapshot=32 irreversible=13 not_mutating=137`, `DECLARED rows=340 entries=340 duplicates=0` | the four table blocks as the registry assembles them | **agrees** (and the figure is added to README by the fix) |
| R5 | `:39` "15+ built-in synthesizers" | `plugin.list` over the socket | catalogue **409 entries** (334 loadable) = 56 builtin / 280 ladspa / 73 lv2; **62 instruments**, of which **26 builtin** | this box's catalogue (built-in + installed LADSPA/LV2) | **agrees as a floor** (judgement: not all 26 are synthesisers) |
| R6 | `:60` "Its own in-tree regression suites ... (default OFF), so CI does not build or run them" | `grep -n WANT_VST3_TEST_INSTRUMENT .github/workflows/build.yml` | `:79  -DWANT_VST3_TEST_INSTRUMENT=ON` in the `linux-x86_64` job's `CMAKE_OPTS`; no other job passes it | the six build jobs' configure arguments | **disagrees → fixed** |
| R7 | `:64` "CLAP ... on Linux and macOS only. The three Windows build jobs compile it out (`-DWANT_CLAP=OFF`)" | same grep for `WANT_CLAP` | all of `CMAKE_OPTS` (`:78`, `:306`, `:535`, `:786`), the msvc call (`:1144`) and msys2 pass `-DWANT_CLAP=ON`; `tests/advertised-features.tsv`'s `clap-hosting` row is `*` | the six build jobs + the honesty manifest | **disagrees → fixed** |
| R8 | `:64` "there is no CLAP instrument hosting" | registry + catalogue | `plugins/ClapInstrument` builds; `plugin.host_notes` is registered; the catalogue carries `clapinstrument` (`dev-34`) | the built binary | **disagrees → fixed** |
| R9 | `:82` "an offline ... loudness meter ... **not a live meter**" | registry | `meter.arm`, `meter.get_state`, `meter.measure_file` registered (3 ids) | the built binary | **disagrees → fixed** |
| R10 | `:87` "the stretch is resampling, so it **changes pitch**" | registry | `warp.stretch` registered beside the resampling verbs (6 `warp.*` ids) | the built binary | **disagrees→ fixed** (resampling still changes pitch; a pitch-preserving verb exists) |
| R11 | `:89` note probability "**no UI or command** reaches it yet" | registry | `note.probability_set` registered | the built binary | **disagrees → fixed** (no UI; a command exists) |
| R12 | `:91` MPE "**pressure and timbre are captured and stored but not applied**" | `docs/STATUS.md:82-88` (the MPE fix, #649) + `device.mpe_set` registered | all three axes applied on playback (pitch as a ratio, pressure/CC74 on the note's channel) | the tree + the registry | **disagrees → fixed** |
| R13 | `:108` "Session View ... compiled out (`WANT_SESSION_VIEW` defaults OFF and no release job passes it)" | `grep -n WANT_SESSION_VIEW CMakeLists.txt` | `:133 OPTION(WANT_SESSION_VIEW ... ON)` ("Default ON since 0.3.0-alpha"); `tests/advertised-features.tsv` row `session-view` requires ON | CMakeLists + the manifest | **disagrees → fixed** (the *interface* limit stands: no grid, no scene launcher, a launched slot does not render audio) |
| R14 | `:114` "Automation modes are not usable ... `automation.mode_set` refuses every call" | live call | `automation.mode_set` → **ok, `mode:"touch"`** | one live instance | **disagrees → fixed** |
| R15 | `:121` "**No fades, crossfades or clip gain; no trim or slip ...; no take lanes, comping or punch in/out; no freeze or bounce-in-place; no sample-accurate automation; no controller surfaces; no groove pool**" | registry, prefix counts | `clip` 16, `comp` 7, `transport.punch_*`, `freeze` 3 + `bounce.in_place`, `automation.ramp_set/get`, `controller` 7, `groove` 7 — all registered | the built binary | **disagrees → fixed** (they are socket-only; the "no interface" half is kept and honoured) |
| R16 | `:121` "no patcher GUI, no live LUFS meter" | registry + `grep -rn Patcher src/gui` | `patcher.set_wiring`/`patcher.get_state` registered (2 ids), no GUI; `meter.*` registered, no GUI | the built binary + the tree | **agrees for the GUI half; the "no live meter" half fixed** |
| R17 | `:98` "7 of the 9 projects the determinism sweep covers are bit-reproducible, and the two that are not are named" | — (a re-render of 9 projects, 2× each) | **cannot-verify in this pass**; the doc of record is `docs/RENDER-DETERMINISM.md` §7 and the two are named there and in `docs/KNOWN-LIMITATIONS.md` | 9 bundled projects | **cannot-verify** (recipe in §6.7) — left as written, recipe and doc added |
| R18 | `:129` "`build.yml` compiles the tree on **seven jobs**" | job ids in `.github/workflows/build.yml` | **six build jobs** (`linux-x86_64`, `linux-arm64`, `macos`, `mingw`, `msvc`, `msys2`) + `release-gate`; the `macos` job is a matrix over two arches → **six jobs, seven platform builds** | the workflow's job ids | **disagrees → fixed** |
| R19 | `:130` "its **two** build-backed jobs (unit tests, and coverage) stay `workflow_dispatch`-only" | the `if:` guards in `.github/workflows/quality-gates.yml` | **four** jobs carry `if: github.event_name == 'workflow_dispatch'` (`unit-tests`, `coverage`, `mcp-bridge-e2e-tests`, `vst3-instrument-fixture`); two do not (`static-gates`, `mcp-bridge-python-tests`) | the workflow's six jobs | **disagrees → fixed** |
| R20 | `:130` static gates "3, 4, 6, 7, 8 and 9 ... on every push and pull request" | triggers + guards | `on: push / pull_request / workflow_dispatch`; `static-gates` has no `if:` guard | the workflow file | **agrees** |
| R21 | `:156` "The top-level build directory has no `CTestTestfile.cmake`, so `ctest` run there reports **0 tests**" | `ls build/CTestTestfile.cmake`; `cd build && ctest -N` | file absent; `Total Tests: 0` | `zene-030/build` (the artifact's own build tree) | **agrees** |
| R22 | `:159` optional flags "both **off in the published alpha builds**" | `grep -rn WANT_STEM_SPLIT\|wasmtime .github/workflows/` | no job passes `-DWANT_STEM_SPLIT=ON`; no job provisions wasmtime, so `WANT_WASM` degrades OFF (`CMakeLists.txt:962-971`) | the workflows | **agrees** |
| R23 | `:189` naming ("the built executable is `zene`, ... and `--version` all say Zene Studio") | `build/zene --version` | `Zene Studio 0.2.1-alpha...` | the artifact | **agrees** |
| R24 | `:36` the promise sentence | `python3 promise-check.py` | PRESENT ×1 (added by this pass; see §4) | the four documents | **agrees after the fix** |

### 3.2 `docs/STATUS.md`

The page's own header scopes every number in it to `post-alpha/integration` @ `5565b4b1b` (2026-09-13); it is
a dated snapshot, not this tip's status, and README claimed otherwise. Fix: the README pointer corrected, and
a measured banner at the top of the page (R25) — no number below it was quietly rewritten.

| # | claim | command | result | universe | verdict |
| --- | --- | --- | --- | --- | --- |
| R25 | `:1` "describes the tree at `5565b4b1b` — version 0.2.1-alpha" (as **this commit's** status, per README) | banner + README fix | banner now names the tip's measured figures (340/53, 332/52, A16 340 rows, 214 ctests, 662 ledger entries, session view ON, CLAP everywhere, snapshot 332) | this tip | **disagrees as-presented → fixed** |
| R26 | `:31` "19 groups / 74 commands" | as R2 | 340/53 | this tip | **disagrees with this tip → superseded in the banner** |
| R27 | `:52`,`:247` "drops the registry **74 → 72**" (telemetry kill switch) | registry prefix count | `telemetry.*` = **2** ids (`telemetry.consent`, `telemetry.status`) → **340→338** reference, **332→330** release | the built binary | **disagrees as a base → fixed in `KNOWN-LIMITATIONS` (K9), banner-scoped in STATUS** |
| R28 | `:200` tooling ladder "74 commands in 19 groups" | as R2 | 340/53; the "missing" groups it lists (`session.*`, `warp.*`, `rack.*`, `comp.*`, `link.*`, `browser.*`) are registered | this tip | **disagrees with this tip → fixed** |
| R29 | `:207` coverage "**87.21 %** — 13770/15790 over 165 files" and "`tests/fork-sources.txt` holds **244** non-comment entries" | `find . -name coverage-fork.info`; `grep -v '^\s*#' tests/fork-sources.txt \| grep -v '^\s*$' \| wc -l` | no capture in the tree; ledger holds **662** entries in 1188 lines | the tree's fork scope | **cannot-verify (no capture); ledger count fixed as a note (K6)** |
| R30 | `:214` "`WANT_SESSION_VIEW` defaults **OFF** (`CMakeLists.txt:121`)" | `grep -n WANT_SESSION_VIEW CMakeLists.txt` | now `:133` = `ON` | CMakeLists @ this tip | **disagrees → fixed** (superseded in place) |
| R31 | `:219` "`WANT_STEM_SPLIT` defaults **OFF** (`CMakeLists.txt:120`)" | same | `:125` = `OFF` (citation moved) | CMakeLists | **agrees in substance; citation fixed in the banner** |
| R32 | `:221` WASM "degrades to OFF when the C API is absent ... CI provisions none" | grep + CMakeLists | true; and the six-job `wasm.*` group it mentions is now **eight** ids | workflows + CMakeLists | **agrees** |
| R33 | `:240` `WANT_VST3_TEST_INSTRUMENT` "defaults OFF", `linux-x86_64` passes ON, "the other six jobs keep the default" | build.yml | correct as written ("six jobs" vs "other six" is the jobs/platforms wording of §2/§8) | build.yml | **agrees** |
| R34 | `:243` CLAP "**OFF on the three Windows jobs**" | as R7 | all six jobs (seven platform builds) pass ON | build.yml + manifest | **disagrees → fixed** |
| R35 | `:245` "Qt6 is built only by the msvc job (`-DWANT_QT6=ON`)" | `grep -n WANT_QT6 .github/workflows/*.yml` | only `build.yml:1140` (msvc); `quality-gates.yml` (dispatch-only jobs) also passes it | build.yml | **agrees** (in `build.yml`) |
| R36 | `:246` telemetry default ON (`CMakeLists.txt:140`) | grep | `:152` (citation moved); default ON | CMakeLists | **agrees in substance; citation fixed in the banner** |
| R37 | `:250` the honesty gate's `session-view`/`wasm-sandbox`/`stem-separation` required-OFF rows | `tests/advertised-features.tsv` (6 rows) + the gate run | rows exist; the gate reports **6 of 6 only on a build without the wasmtime C API** — on this box's build it reports **5 of 6** (`wasm-sandbox` FAIL, this build has it ON) | the manifest + the build under test | **agrees for the release configuration; the this-box reading is recorded (K7)** |
| R38 | `:306` "`commands_snapshot.json` holds **70** commands against the registry's **74**, with its README still saying '23 generated tools'" | `json.load(...snapshot)`; the bridge README | snapshot now holds **332** (`captured_at 2026-09-16T14:54:53Z`) against the registry's 340 (this config) / 332 (its own) | the file | **disagrees → fixed** |
| R39 | `:347` "`gh run list` showed the build workflow **FAILED** on the `v0.2.1-alpha` tag push (`34725347297`)" | — (needs a GitHub API read) | not re-read here; the lane context records run #8 green on this tip | GitHub Actions | **cannot-verify here** (recipe in §6.8) |

### 3.3 `docs/KNOWN-LIMITATIONS.md`

| # | claim | command | result | universe | verdict |
| --- | --- | --- | --- | --- | --- |
| K1 | `:1` title "**Zene Studio 0.2.1-alpha**: known limitations" | `head -1` | the file carries 0.3.0 sections (verb wave, CLAP, stem, safestart, golden audio...) | the file itself | **disagrees → fixed** (title + banner; 0.2.1/0.2.0 history kept) |
| K2 | `:131` "**No CLAP hosting on Windows** ... the Windows builds are configured with `-DWANT_CLAP=OFF`" | as R7, plus the same file's §`CLAP hosting on Windows` (line 1510+) | all six jobs pass ON; the loader has a `LoadLibraryW` half; the tsv row is `*` | build.yml + tsv + the file | **disagrees → fixed** (the file contradicted itself) |
| K3 | `:417` "for **7 of the nine** projects the determinism sweep covers two renders are byte-identical ... (the tree ships **68** `.mmp`/`.mmpz` files)" | `git ls-files \| grep -cE '\.(mmp\|mmpz)$'` | **68** tracked project files (41 under `data/projects/`) | tracked files | **68 agrees**; 7-of-9 **cannot-verify** (same as R17) |
| K4 | `:440` coverage "**87.21 %** ... (13,770/15,790) over 165 of the 242 fork-scope entries", "baseline 67 files at **85.95 %** (4,614/5,368)" | no coverage capture in the tree | cannot be re-derived here; the ledger at this tip holds **662** entries | that capture's own 165 files | **cannot-verify → note added, limitation untouched** |
| K5 | `:450` "`tests/coverage-gate.sh --check` reports **15 new files** below its 50 % entry floor; **ten** dialogs, **two** telemetry, **three** genuinely untested" | — (needs a coverage build) | not re-run here | a coverage build | **cannot-verify** (recipe in §6.6) |
| K6 | `:437` "measured at `3ef822eaf`; every commit after it is documentation only" | already corrected in-file (2026-09-13) | — | — | agrees with the in-file correction; superseded by K4's note |
| K7 | `:440` (the honesty-guard paragraph) "**6 of 6 rows match** ... 3 of 6 on `build-coverage/zene`, 1 of 6 on ..." | `bash tests/release-honesty-gate.sh --dump <zene --version output> --artifacts build` | **5 of 6** on this box's build (`wasm-sandbox` FAIL); the release jobs provision no wasmtime, so there the row holds | the build under test | **agrees for the release configuration; the 5-of-6 reading recorded** |
| K8 | `:403` "the *bridge in this tree* covers all **173** ids" | `tests/control-commands-snapshot.py` | **340 live ids → 340 generated tools** (342 with the bridge's two); **332** offline → 334 tools; 0 missing / 0 extra in all three modes | the bridge in this tree + the binary | **disagrees → fixed** |
| K9 | `:403` "the scratch copy" whose "offline list is the 0.1.0-alpha **70**" | read the scratch copy's snapshot; `~/.hermes/config.yaml` | **70 ids, captured 2026-09-12, version `0.1.0-alpha.15+a244564`**; the Hermes config's `zene-control` entry points at it | the environment, not the repo | **agrees** (and cannot be fixed by a commit — still true) |
| K10 | `:405` "**265 ids / 43 groups live against this tree's 144-id snapshot**" | as K8 | **340 / 53 live vs 332** | the binary + the committed snapshot | **disagrees → fixed** |
| K11 | `:405` "**no build on this machine compiles the sandbox in** (`Wasmtime_LIBRARY-NOTFOUND` in every configured build)" | `--version` + the registry | `zene-030/build` has `WANT_WASM='ON'` and registers **all eight** `wasm.*` ids | this box's builds | **disagrees → fixed** |
| K12 | `:405` the ten groups "driven end to end by the registered ctest `ControlMcpGroupCoverage`" | `ctest -R ControlMcpGroupCoverage` | **Passed, 5.57 s** — and on this build there is no `wasm.` excuse to take, so the group is driven for real | the registered test, this build | **agrees** |
| K13 | `:899` the telemetry kill switch: registry "**74 → 72 commands**, the diff being exactly `telemetry.consent` and `telemetry.status`" | registry prefix count | the diff **is** exactly those two ids; the base is 340 (→338) / 332 (→330) | the built binary | **disagrees in base → fixed** |
| K14 | `:28` (via STATUS) "**41** shipped identity images ... replaced" | `python3 tests/brand-resource-sweep.py --quiet` | exits 0: 3 pre-existing unresolved call sites, **0 NEW**; the count and its table are `docs/BRAND-PLACEHOLDERS.md`'s own audit (41 files, 39 byte-identical to upstream) | the tree's data/ resources | **agrees with its source document; not independent** (judgement) |
| K15 | `:1105` "No live mode is claimed, and none exists" (HTDemucs) | `WANT_STEM_SPLIT='OFF'` in `--version` | stem separation is out of every release build | the artifact | **agrees** |
| K16 | per-feature "drivable through the socket, not from the interface" statements (~40 of them) | registry prefixes + `grep -rn <Feature> src/gui` spot checks | the ids exist in the registry; the GUI claims were not exhaustively re-grepped (see §7) | the binary + the tree | **agrees where spot-checked; the rest are judgement rows** |

### 3.4 `docs/RELEASE-NOTES-v0.3.0-alpha.md`

| # | claim | command | result | universe | verdict |
| --- | --- | --- | --- | --- | --- |
| N1 | `:5`,`:21` the promise, verbatim | `python3 promise-check.py` | PRESENT ×2 | the four documents | **agrees** |
| N2 | `:852` "The table holds **340 rows** — 158 / 32 / 13 / 137 ... `DECLARED rows=340 entries=340 duplicates=0`" | `bash tools/dawproject-proof.sh` §2; `ctest -R ReversibilityContractTest` | printed identically; the ctest that READS the block **Passed** | the four table blocks vs the registry | **agrees** |
| N3 | `:852` "for the configuration **the release ships** (telemetry client in, wasmtime sandbox in, session data layer in, offline stem engine out)" | build.yml + CMakeLists + defaults | session in ✓, telemetry in ✓, stem out ✓, **wasmtime NOT in any release job** (nothing provisions the C API) | the six build jobs' configuration | **disagrees → fixed** (re-worded to the *reference* configuration, with the delta named) |
| N4 | `:877-881` the option rows `telemetry.status rows=2`, `wasm.load rows=8 snapshot=3 not_mutating=5`, `session.get_state rows=17`, `stem.get_state rows=7` | registry prefix counts + the probe | telemetry **2**, wasm **8**, session **17** — the first three match exactly; `stems` measures **0** rows in this build (`WANT_STEM_SPLIT=OFF`), consistent with the row being a delta for the option | the built binary + the probe | **agrees for three; the fourth is a configuration delta (cannot-verify here)** |
| N5 | `:1131` "a 70-id 0.1.0-alpha cache leave **74 ids** of this tree's surface unreachable" | as K9 | the 70-id copy exists (the scratch copy's); the 74 was that tip's surface | that lane's tree | **historical; the section's current figure re-taken (N6)** |
| N6 | `:1134` "**185 ids registered, 185 exposed live, 185 exposed offline** (187 tools ...)" | `tests/control-commands-snapshot.py` | **340 registered, 340 live, 332 offline** (342 / 334 tools) | the bridge + the binary | **disagrees as current → fixed** (re-take added; the 185 is kept as that train's figure) |
| N7 | `:1150` "this one is the tip it ships in" | the snapshot file | snapshot regenerated 2026-09-16 from a no-wasmtime build (`332`, `2026-09-16T14:54:53Z`); the tip registers 340 | the file | **disagrees → fixed** |
| N8 | `:2330` "**265 ids across 43 groups reachable live** (267 tools ...) against the snapshot's **144 ids across 27 groups**" | as N6 | **340 / 53 live vs 332 / 52** | the binary + the snapshot | **disagrees → fixed** |
| N9 | `:2335` "because no configuration on this machine compiles the wasmtime sandbox in" | as K11 | false now | this box's builds | **disagrees → fixed** |
| N10 | `:2768` "What this does NOT have: ... **no CLAP instrument hosting**" | registry + catalogue | `clapinstrument` (`dev-34`) + `plugin.host_notes` registered (feature row 79, the next day) | the built binary | **disagrees → fixed** (scoped correction) |
| N11 | the ~55 dated per-lane sections' own figures | — | not re-taken one by one; the ones a reader takes as current are N6/N8/N10 | each lane's tip | **out of scope by design** (§2), except the four rows above |
| N12 | `:76` "the base of record is `post-alpha/integration` @ `70f2d087c`" | `git cat-file -t 70f2d087c` | the commit exists in this repository | git | **agrees** (existence checked) |

## 4. The promise, in as many words

```
everything is operable through --control-socket and the MCP bridge; almost nothing is operable from the interface
```

| file | before this pass | after |
| --- | --- | --- |
| `docs/RELEASE-NOTES-v0.3.0-alpha.md` | PRESENT ×2 (`:5`, `:21`) | unchanged |
| `README.md` | absent | **PRESENT ×1** (`:36`) |
| `docs/STATUS.md` | absent | **PRESENT ×1** (banner, `:4`) |
| `docs/KNOWN-LIMITATIONS.md` | absent | **PRESENT ×1** (banner, `:11`) |

Check (recipe §6.1): whitespace-collapsed, markdown emphasis stripped, the sentence is byte-for-byte in all
four documents. It is *not* a raw substring because the documents wrap it and mark `--control-socket`
as code — the check is word-for-word, which is what the card asks for.

## 5. The independent reader (nominated, and its findings recorded)

The author of a claim is never its verifier, and that includes this lane's own fixes. **Nominated reader:
a Hermes subagent run on 2026-09-19, fresh context, which authored none of the four documents and none of
this lane's edits** (identity in its own words: *"Hermes subagent (deepseek/deepseek-v4.1-flash via
commandcode), read-only reader of worktree zene-030 (branch release/0.3.0 @ 08d96d612, rel. docs branch
030/docs-agree @ wdocs), measuring against build/zene ... over its own control socket"*). It was given the
commands, not the answers, and told to be adversarial.

Its verdicts, verbatim from its own report:

* **live registry — agrees**: `n_ids= 340`, `n_groups= 53`, every one of the 53 per-group counts matches, 0
  extra groups, sums 340 = 340.
* **A16 — agrees**: `MEASURED rows=340 true_inverse=158 snapshot=32 irreversible=13 not_mutating=137`,
  `DECLARED rows=340 entries=340 duplicates=0`.
* **ctests — agrees**: `Total Tests: 214`.
* **snapshot — agrees**: `count= 332`, `captured_at= 2026-09-16T14:54:53Z`, 52 groups.
* **fork ledger — agrees**: `662` entries in 1188 lines.
* **the three commands — agrees**: `track.set_arm` `armed: true` with a take file; `mixer.set_pan`
  `ok=false, kind 'refused'`; `automation.mode_set` `mode 'touch'` from `read`.
* **the fixes it spot-checked — agrees**: README's 332/52 and its "one refusal" line, STATUS's banner
  (340/53, 332/52, A16, 214, 662, 332, and the quoted `--version` string), KNOWN-LIMITATIONS' title and its
  "340 live / 332 offline" line.
* **promise — partly agrees, and this is the one gap it found**: verbatim in README and the release notes,
  **absent from STATUS.md and KNOWN-LIMITATIONS.md**. Closed by commit `22e1069df`; re-checked with §6.1.
* **its disagreements: none.** *"No number in the four documents disagrees with my measurements."*
* **what it could not verify** (its words): it did not boot a live instance of the *release* configuration
  itself (it measured the build under test live and the committed snapshot for the release figure); it did
  not enumerate refusal outcomes across all 340 ids to prove `mixer.set_pan` is the *only* refusal (it
  corroborated via `tests/agent-surface-negative-control.md`); and it searched only the four documents for
  the promise.

## 6. One-command recipes (what the parent should re-run, in this order)

Run from `zene-030` (the artifact's tree) unless noted; `export
LD_LIBRARY_PATH=/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-030/third_party/wasmtime/lib`

1. **The promise, all four documents** —
   `python3 /tmp/docsagree/promise-check.py $PWD` (script reproduced here: `docs/reports/DOCS-AGREE-REPORT.md`
   §6.1 is the source of truth; the check is whitespace-collapse + strip `\`*>` + case-insensitive find of the
   sentence). **Expect: PRESENT ×1, ×1, ×1, ×2 — "ALL FOUR CARRY THE PROMISE".**
2. **Identify the artifact** — `sha256sum build/zene && ./build/zene --version | head -2`.
   **Expect `2eed2031…` and `Zene Studio 0.2.1-alpha.612+a039d26`.**
3. **The command surface** — boot the binary over `--control-socket` with the tree's own harness and call
   `control.commands_list`: write the probe from §10 to `/tmp/audit-probe.py`, then
   `cd wdocs/tests && QT_QPA_PLATFORM=offscreen python3 /tmp/audit-probe.py <zene-030>/build/zene`.
   **Expect 340 ids / 53 groups**, and `wasm.*` = 8, `session.*` = 17, `telemetry.*` = 2.
4. **The A16 figure** —
   `DAWPROJECT_PROOF_BUILD=$PWD/build DAWPROJECT_PROOF_WORK=/tmp/a16parent bash tools/dawproject-proof.sh`
   **Expect `MEASURED rows=340 true_inverse=158 snapshot=32 irreversible=13 not_mutating=137` and
   `DECLARED rows=340 entries=340 duplicates=0`** — and `cd build/tests && ctest -R ReversibilityContractTest`.
5. **The MCP spine** — `cd wdocs/tests && QT_QPA_PLATFORM=offscreen python3 control-commands-snapshot.py
   $PWD/../../zene-030/build/zene --compiled-in wasm.` **Expect `PASS … SAME 332 command id(s)` and
   `PASS: the bridge exposes a tool for EVERY one of them`**; `ctest -R ControlMcpGroupCoverage` **Passed**.
6. **The three commands** — the same §10 probe, second half: `track.set_arm` ok, `mixer.set_pan` refused,
   `automation.mode_set` ok.
7. **Determinism (9 projects ×2 renders, the expensive one)** —
   `bash tools/render-determinism-probe.sh --all` (needs an audio-free headless render; ~minutes).
   This is the one row R17/K3 that this pass could not settle.
8. **The live CI state** — `gh run list --limit 10` (the docs' CI claims are about runs, which no local
   artifact can settle).
9. **The counts a reader will grep** — `ctest -N | tail -1` from `build/tests` (**214**);
   `python3 -c "import json;d=json.load(open('wdocs/tools/mcp-zene-control/zene_control/commands_snapshot.json'));print(d['count'],d['captured_at'])"`
   (**332 / 2026-09-16T14:54:53Z**); `grep -v '^\s*#' wdocs/tests/fork-sources.txt | grep -v '^\s*$' | wc -l`
   (**662**); `git ls-files | grep -cE '\.(mmp|mmpz)$'` (**68**).
10. **Coverage (the other expensive one, and the reason R29/K4 cannot be settled here)** —
   `bash tests/run-coverage.sh build-coverage` with the pinned VST3 SDK and CLAP headers provisioned and
   `-DWANT_VST3_TEST_INSTRUMENT=ON`, then `tests/coverage-gate.sh --check`. The docs' 87.21 % figure is that
   capture's, not this tree's; the tree holds no `coverage-fork.info` to check it against.

### 6.1 the promise check (self-contained)

```python
NEEDLE = ("everything is operable through --control-socket and the MCP bridge; "
          "almost nothing is operable from the interface")
def norm(t):
    for ch in ("`", "*", ">"): t = t.replace(ch, " ")
    return " ".join(t.split()).lower()
for f in ["README.md","docs/STATUS.md","docs/KNOWN-LIMITATIONS.md","docs/RELEASE-NOTES-v0.3.0-alpha.md"]:
    print(f, norm(open(f).read()).count(NEEDLE.lower()))
```

## 7. What I could NOT verify, and why

| row | why | recipe |
| --- | --- | --- |
| R17 / K3 — "7 of 9 projects are bit-reproducible" | needs 18 renders of 9 bundled projects; out of this pass's budget | recipe 7 |
| R29 / K4 — coverage 87.21 %, 13,770/15,790, 165 of 242 | **no coverage capture exists in this tree** (`find . -name coverage-fork.info` → nothing) and the capture commit is named in the doc, not present | recipe 10 |
| K5 — the coverage gate's 15 files | needs a coverage build | as above |
| R39 — the CI matrix's live state / run ids | needs GitHub; not read here | recipe 8 |
| STATUS's `5565b4b1b` figures (87.21 %, 244, 67-file baseline) | that commit is a different tree; the page is now explicitly scoped to it | `git show 5565b4b1b:docs/...` if the commit is present |
| the release notes' ~55 dated lane sections | each is that lane's own tip's measurement; re-taking all of them is a different card | §2 |
| the ~40 per-feature "no `src/gui/` path reaches it" statements | spot-checked, not exhaustively re-grepped (each needs its own `grep`) | the grep is quoted inline in each bullet |
| `mixer.set_pan`'s uniqueness as *the* refusal | not enumerated across all 340 ids; corroborated by `tests/agent-surface-negative-control.md` ("`mixer.set_pan` is the one typed refusal") | call every mutating id and count `kind:"refused"` |

## 8. Judgement calls, and the live-lane overlap

1. **`README`'s version strings were not touched.** Lines 7 and 16–20 state the tree's version
   (`0.2.1-alpha`) and are being rewritten right now by the live `030/version-bump` lane — verified:
   `git -C lmms diff 08d96d612..030/version-bump -- README.md` touches exactly those three hunks
   (0.2.1-alpha → 0.3.0-alpha, v0.1.0-alpha → v0.2.1-alpha) plus `CMakeLists.txt` and
   `tests/test-release-version-gate.sh`. The README's *pointers* to `RELEASE-NOTES-v0.2.1-alpha.md`
   (lines 10 and 138, outside that diff) were updated to `v0.3.0-alpha` here — **the one place the two
   changes are adjacent**; the version-bump lane's hunk and this one do not overlap textually.
2. **`docs/STATUS.md` was scoped, not rewritten.** Its numbers are a dated audit of another commit; every
   one of them is now either explicitly under the banner or marked superseded in place. Rewriting 370 lines
   would have replaced measured history with this pass's assumptions.
3. **"seven jobs" is the repo's own habit** for seven platform builds from six job definitions. Where I
   rewrote a sentence I made it precise (six jobs / seven platform builds); the untouched occurrences in
   STATUS and in the release notes are left as their lanes wrote them.
4. **`docs/RELEASE-NOTES-v0.3.0-alpha.md` is titled "(in progress)"** and its own rule is to re-check each
   claim before the tag. This pass re-took the four figures a reader would take as current; the remaining
   ~55 section figures are that lane's measurements by construction.
5. **Not edited, and should be a follow-up**: `tools/mcp-zene-control/README.md`'s snapshot note still
   describes the **scratch** copy's 70-id, `0.1.0-alpha.15`, 2026-09-12 file as "the copy in this tree"
   while the committed snapshot is 332 ids (2026-09-16, from `wwasm/build-nowasm/zene`); and the MCP tool
   text for `mixer.remove_channel` says "master `ch-0`" while `mixer.get_state` on this build returns the
   master as **`ch-1`** (`index 0`). Both are outside the four documents, so they are recorded rather than
   changed.

## 9. Rows the parent should spot-check before the tag

1. §6.3 and §6.4 — the two figures the whole docs set rests on: **340 ids / 53 groups** (reference) and the
   **A16 histogram 340/158/32/13/137**.
2. §6.1 — the promise, all four documents (the one thing this pass added to two of them).
3. §6.5 — the MCP spine (`ControlCommandsSnapshot` + `ControlMcpGroupCoverage`, both Passed here).
4. recipe 7 — the determinism row, which nobody has re-rendered (R17/K3).
5. R34/K2 — the CLAP-on-Windows correction: check it against `.github/workflows/build.yml` yourself; it is
   the largest single reversal of a shipped limitation in this pass.


## 10. Appendix — one self-contained probe (recipes 3 and 6)

Drop this at `/tmp/audit-probe.py` and run it from `<wdocs>/tests` (so `control_socket_harness` imports) as
`QT_QPA_PLATFORM=offscreen python3 /tmp/audit-probe.py <zene-030>/build/zene`. It prints the registry size,
the per-group histogram, and the three refusal replies — the four instrument readings the table rests on.

```python
#!/usr/bin/env python3
"""Task 691 instrument: control.commands_list + the three refusal claims, from the ARTIFACT."""
import json, os, sys
sys.path.insert(0, os.environ.get("ZENE_TESTS_DIR", "."))
import control_socket_harness as H

def main(binary):
    inst = H.start_instance(binary)
    try:
        client = H.connect(inst)
        H.wait_ready(inst, client, None)
        version = H.ok_result(client.call(1, "control.version"), 1)
        result = H.ok_result(client.call(2, "control.commands_list"), 2)
        entries = result["commands"]
        groups = {}
        for e in entries:
            g = str(e.get("id")).split(".")[0]
            groups[g] = groups.get(g, 0) + 1
        print("version", json.dumps(version, sort_keys=True))
        print("count", len(entries), "groups", len(groups))
        print("histogram", json.dumps(groups, sort_keys=True))
        added = H.ok_result(client.call(3, "track.add", {"type": "instrument"}), 3)
        track = added.get("track") or added.get("id")
        for n, cid, args in ((4, "track.set_arm", {"track": track, "armed": True}),
                             (6, "mixer.set_pan", {"channel": "ch-1", "pan": 0.5}),
                             (7, "automation.mode_set",
                              {"track": "trk-2", "parameter": "inst/0", "mode": "touch"})):
            if cid == "mixer.set_pan":
                chan = H.ok_result(client.call(5, "mixer.get_state"), 5)["channels"][0]["id"]
                args["channel"] = chan
            reply = client.call(n, cid, args)
            print(cid, json.dumps({"error": reply.get("error"),
                                   "result": reply.get("result")}, sort_keys=True)[:400])
        client.call(9, "control.quit")
        client.close()
        inst.wait_for_exit(H.QUIT_TIMEOUT)
    finally:
        inst.close()

if __name__ == "__main__":
    main(sys.argv[1])
```

`track.set_arm` is expected to answer `armed: true` with a take file beside this instance's recovery file
(`mixer.set_pan` is expected to answer `kind: "refused"`; `automation.mode_set` `mode: "touch"`).

*Written by the `030/docs-agree` lane, 2026-09-19. The four documents were edited only where the artifact
disagreed with them; no honest limitation was weakened, and every figure this pass introduced carries the
command that produced it and the universe it was counted over.*
