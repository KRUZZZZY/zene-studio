# Release-prep report — Zene Studio 0.2.0-alpha

**Lane:** release engineer, `post-alpha/release-prep` (worktree
`projects/lmms-fl-research/zene-pa-releaseprep`), based on `post-alpha/integration` @ **`34c1f4f86`**
("Merge post-alpha/render-determinism into post-alpha/integration (merge train 3A, #3 of 6)") — the
merge train was mid-flight when this worktree was cut, and this branch is meant to be merged **last**,
so every "not in this base" below is expected to be resolved by later train merges, not by a defect here.

**Scope executed:** `RELEASE-0.2.0-CHECKLIST.md` blocks A–D (D1/D1b/D2/D4, Block D's capability
contract, the document set, the README/version sweep). Blocks C, E, F stay with the parent.
**Nothing was pushed, tagged, released or merged**; no branch was rebased.

**Commits on this branch** (`git log --oneline 34c1f4f86..HEAD`):

| commit | subject |
|---|---|
| `4c7ebe473` | build(release): 0.2.0-alpha, and the product's own contact address |
| `2840a8479` | docs(release): apply the reviewed 0.2.0-alpha notes, limitations and capability contract |
| `8408894e6` | docs(release): sweep the README, the runbook and the feedback form to 0.2.0-alpha |
| `1a30c4a74` | test(gates): a release-version gate, wired into all six package jobs |
| `b8294e106` | fix(gates): an array declared but not initialised fails the runner's summary under set -u |
| (this file) | docs(release): the release-prep report and its evidence |

The five commits above are **14 files, 1126 insertions, 215 deletions**
(`git diff --stat 34c1f4f86..b8294e106`); this report and its evidence set are the sixth commit.
Evidence committed under `tests/evidence/release-prep-0.2.0/` (see its README).

---

## 0. Verdict, in one paragraph

The version and metadata defect is fixed and **proved on a built binary**: the shipped line declares
`0.2.0-alpha` and a binary built from this tree prints **`Zene Studio 0.2.0-alpha`**. The three reviewed
documents are applied (the manifest byte-identically); every `[VERIFY AT FREEZE]` marker this tree and
its binary can settle is resolved with `file:line` evidence, and every marker that cannot is **left in
place with its blocker named** — nothing deleted, nothing unmarked. The capability contract passes the
**option** half of the honesty gate 6/6 on this build and fails the **module** half on exactly one row
(`vst3-instrument-hosting`), which is a property of a base whose instrument-hosting lane is not merged.
**The one finding that dominates the rest:** the release notes describe work from **13 lanes that are
not ancestors of this base** — including instrument hosting end-to-end, the instrument-window run,
warps, racks, VCA, MPE, telemetry, `zene master`, the exit-abort fix, the recorder fixes, the save
fixes and the mixer TSAN fixes. §4 is that table, and it is the list the freeze must close.

---

## 1. Version and metadata (Block D1, D1b)

### 1.1 What changed

    CMakeLists.txt:62   PROJECT_EMAIL  "lmms-devel@lists.sourceforge.net"
                                     -> "https://github.com/KRUZZZZY/zene-studio/issues"
    CMakeLists.txt:66   VERSION_MINOR  "1" -> "2"

Declared in `tests/upstream-modifications.txt` with a reason read off the diff (Gate 6 classifies
`CMakeLists.txt` as `build/config (allowed)` regardless, but the lane rule is to declare every touched
upstream file). The 0.2.0 values are the ones the other stream already agreed
(`git show post-alpha/agent-control-surface:CMakeLists.txt` → `VERSION_MINOR "2"` inside its
control-surface commit `795c207ea`), applied here on the shipping line — not merged from that branch.

### 1.2 The mechanism nobody had written down: the version comes from the TAG

`CMakeLists.txt`'s `VERSION_*` is **not** what the binary reports when a git tag is reachable.
`cmake/modules/VersionInfo.cmake` (itself a declared upstream divergence, `#615`) runs
`git describe --tags --match 'v[0-9]*.[0-9]*.[0-9]*'` at configure time and **overrides** `VERSION`
with the result. On this base:

    $ git describe --tags --match 'v[0-9]*.[0-9]*.[0-9]*'
    v0.1.0-alpha-123-g34c1f4f86            # exit 0

so an untagged build reports `0.1.0-alpha.123+34c1f4f` — with **MINOR 1, not the 2 that CMakeLists
declares**. On the release commit, with the tag present, describe returns exactly `v0.2.0-alpha`, which
the same code turns into `0.2.0-alpha` (`FORCE_VERSION` = `0.2.0` + pre-release `alpha`). Three
consequences for the freeze:

1. `VERSION_MINOR "2"` matters for the **fallback** path only (no git, or `-DFORCE_VERSION=internal`) —
   a source tarball, a shallow clone without tags. It is still the right value, and it is what makes the
   binary report `0.2.0-alpha` off-tag (proof below).
2. The tag **is** the version: `v0.2.0-alpha` on the release commit, nothing else. A tag named
   `v0.2-alpha` or `v0.2.0a` would silently produce `0.2.0` or fail to derive a pre-release stage.
3. Any pre-tag build of this branch — including the tag job's own earlier runs, and CI on this branch —
   legitimately reports `0.1.0-alpha.123+34c1f4f`. That is the same provenance string the published
   alpha's `0.1.0-alpha.28+ccd07f4` came from; it is not a second product version.

### 1.3 Proof: the built binary reports `0.2.0-alpha`

Build first, with the CI's own flag set (`tools/local-ci.sh`), then reconfiguring the same build
directory with the repository's **documented** override (`VersionInfo.cmake` prints
`Ignore Git information: -DFORCE_VERSION=internal`), which is the only way to exercise the fallback
path off-tag without creating a tag (this lane must not tag):

    $ cmake -S . -B build -DUSE_WERROR=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTARGET_UARCH=official \
            -DUSE_COMPILE_CACHE=ON -DWANT_DEBUG_CPACK=ON -DWANT_VST3=ON -DWANT_CLAP=ON -DWANT_QT6=ON \
            -DFORCE_VERSION=internal
    RECONFIGURE_EXIT=0
    $ grep '^set(VERSION' -m1 ... ; # configure banner:
    * Project version             : 0.2.0-alpha
    *   Major version             : 0
    *   Minor version             : 2
    *   Release version           : 0
    *   Stage version             : alpha
    $ cmake --build build -j4
    BUILD_FORCE_VERSION_EXIT=0
    $ QT_QPA_PLATFORM=offscreen ./build/zene --version
    ZENE_VERSION_EXIT=0
    Zene Studio 0.2.0-alpha
    (Linux x86_64, Qt 6.4.2, GCC 13.3.0)

Full output: `tests/evidence/release-prep-0.2.0/02-version-0.2.0-alpha.txt`. The build directory was
then **restored to the CI flag set** (`-U FORCE_VERSION`), where the same binary reports
`Zene Studio 0.1.0-alpha.123+34c1f4f` (`…/01-version-untagged-ci-flags.txt`) — that is the tag mechanism
above working as designed, not a regression, and it is why the release is cut from a tag.

**Limit, stated plainly:** the tag path itself (a real `v0.2.0-alpha` tag) was **not** executed here —
creating a tag is outside this lane by hard rule. What was executed is the fallback path, which
produces byte-identical text by construction. The tag path is exercised by the parent's tag build, and
the new version gate (§6) fails the build if the tag and the declared version disagree.

**The brief's `./build/lmms --version` is a wrong premise:** the wave-R rename made the target
`zene` and there is no `build/lmms` in this tree (`src/CMakeLists.txt`: `ADD_EXECUTABLE(zene …)`; the
binary is `build/zene`, and `README.md`'s build section says so). Reported, not guessed.

### 1.4 `PROJECT_EMAIL`: what "correct" means, and what can actually be proved

The value is replaced with the product repo's issue URL — an address this project owns and can answer
on. `PROJECT_COPYRIGHT` is untouched: `2008-2026 LMMS Developers, 2026 Zene Studio contributors` is
attribution, which is a licence condition (checklist D1b), and the rename lane enforces that.

Proof, and its honest boundary (`PROJECT_EMAIL` is **not** in the binary — `grep -rn PROJECT_EMAIL
src/ include/ plugins/` returns nothing):

* the only consumer is `CPACK_NSIS_CONTACT` (`cmake/nsis/CMakeLists.txt:17`), inside
  `ADD_SUBDIRECTORY(nsis)` which is guarded by `IF(LMMS_BUILD_WIN32)` (`cmake/CMakeLists.txt:26-27`) —
  so on Linux the value never reaches any generated file. A Windows installer cannot be produced on this
  box (no mingw/MSVC), so **"the Windows installer now shows our issue URL" is UNVERIFIED here** and
  belongs to the MSVC/mingw jobs;
* what is proved: the value is defined correctly in the source of truth, and the upstream address is
  gone from the tree — `grep -rl "lmms-devel@lists.sourceforge.net" . --exclude-dir=build --exclude-dir=.git
  | wc -l` → **0** (before this change it was `CMakeLists.txt`).

### 1.5 The other identity surfaces (checklist D1's "desktop entry, man page")

* `cmake/linux/zene.desktop` carries **no version field at all** (`Name=Zene Studio`, `Exec=zene`) and
  `doc/zene.1` embeds no version string (it documents `-v, --version`). There is nothing for D1's
  "the desktop entry and the man page report 0.2.0-alpha" to be true *of*; noted so the freeze does not
  go looking for a change it cannot make.
* The package name follows the version: `build/CPackConfig.cmake` generates
  `set(CPACK_PACKAGE_FILE_NAME "zene-0.1.0-alpha.123+34c1f4f-linux-x86_64")` on this untagged base, so the
  release's assets are `zene-0.2.0-alpha-<platform>.<ext>` — pattern verified, the version slice of it
  is the tag's.

---

## 2. The three reviewed documents, applied

| reviewed draft | applied to | fidelity |
|---|---|---|
| `drafts/RELEASE-NOTES-v0.2.0-alpha-DRAFT.md` | `docs/RELEASE-NOTES-v0.2.0-alpha.md` | verbatim except marker resolution (§3) and the replacement of the "DRAFT, not yet applied" status block with a verification-status block |
| `drafts/KNOWN-LIMITATIONS-v0.2.0-alpha-DRAFT.md` | `docs/KNOWN-LIMITATIONS.md` | verbatim except marker resolution (§3) |
| `drafts/advertised-features-v0.2.0.tsv` | `tests/advertised-features.tsv` | **byte-identical** (`diff -q` → 0, 4377 bytes) |

Nothing was rewritten. The 0.1.0 limitations page is **not** lost: it is preserved as
`docs/KNOWN-LIMITATIONS-v0.1.0-alpha.md`, the same convention the 0.1.0 release notes already use — and it
is needed, because the 0.2.0 page points at "the one-time steps are in the release notes" for the
unsigned-build warnings and neither 0.2.0 document currently carries them (§5, dangling pointer).

---

## 3. The marker table

Convention used in the two documents: a marker is **resolved in place** when this tree and its built
binary settle it, and otherwise **left as a marker** with its blocker named
(`post-alpha/<lane> is not in the release-prep base 34c1f4f86`, or `needs the published artefacts`).
No claim was deleted and none was published unmarked.

### 3.1 Resolved against the tree and the binary

| document | marker | resolution, with the evidence |
|---|---|---|
| notes | rename residue list | `docs/WAVE-R-RENAME.md` §6 (lines 239-275): namespace/`LMMS_*` macros, `<lmms-project>`/`creator="LMMS"`/175 presets, licence headers + attribution, `lmms_plugin_main` + the `lmms-plugin-logo` key (54 call sites), `~/.lmmsrc.xml` + `~/Documents/lmms/` + the `lmms-workspace` marker, the JACK/PulseAudio client name, MIME types, upstream URLs, historical docs |
| notes | plugin-scanning user-facing surface | `docs/PLUGIN-SCAN-CACHE.md` §2.1/§5 — "no GUI for it yet"; quarantine is a `{"path":…,"reason":…}` JSON entry |
| notes | sample-accurate automation is NOT in | `docs/AUTOMATION-MODES.md` — evaluated once per tick, not per frame (the doc names the blocking line) |
| notes | the two non-reproducible projects | `demos/StrictProduction-DearJonDoe.mmp`, `shorties/Root84-TrancyLoop.mmpz` — `docs/RENDER-DETERMINISM.md` §9 ("2 of 9 bundled projects are still not reproducible"), §10 the six falsification experiments, §7 the sweep |
| notes | `.bak` path + `creatorversion` | `src/core/DataFile.cpp:347` (`fullName + ".bak"`), `:435`/`:438` (rename into place), `:427` (`app/disablebackup`); `creatorversion` written `:140`, `:2105`; read `:2179`; drives "Version difference" and `legacyFileVersion()` `:2226-2238` |
| notes | instrument fixture as the witness | `tests/data/vst3-test-instrument/` (MIT fixture), probe `Vst3InstrumentFixtureProbe` (`tests/CMakeLists.txt:737-752`), validator result and the MIDI→audio probe in `docs/VST3-INSTRUMENT-FIXTURE.md` §0/§5 |
| notes | "the plugin's own editor does not exist" | `grep -rn IPlugView src/ include/ plugins/Vst3Effect/ plugins/ClapEffect/` → **0** (the tree's `IPlugView` hits are all inside the vendored Carla copy of the VST3 SDK headers) |
| limitations | `.bak` naming/path | as above |
| limitations | `creatorversion` attribute | as above |
| limitations | no take lanes / comping | verified absence: `grep -rniI "takelane\|take lane\|comping" src/ include/` → **0** |
| limitations | plugin-scanning interface | as above |
| limitations | re-state WASM/stem from the binary | `WANT_WASM='OFF'`, `WANT_STEM_SPLIT='OFF'` in `build/lmmsversion.h`, i.e. exactly the text the binary prints on its `Build options:` line |
| limitations | automation not sample-accurate | as above |
| limitations | the two non-reproducible projects | as above |
| limitations | half of "no instrument editor" | the "does not exist in the host" half (`IPlugView` → 0) |

### 3.2 Left as markers — because the owning lane is not in this base

| document | claim | owning branch (verified with `git merge-base --is-ancestor`) |
|---|---|---|
| notes | instrument hosting end-to-end: load → MIDI in → audio out → save → reload | `post-alpha/instrument-hosting-impl` (`7566607f2`) |
| notes + limitations | the instrument window opening and listing controls | `post-alpha/instrument-view-safety` |
| notes | warp engine | `post-alpha/warp` |
| notes + limitations | racks | `post-alpha/racks` |
| notes + limitations | VCA / mix-and-edit groups | `post-alpha/vca` (`<vcagroup>` appears nowhere under `src/`/`include/` here) |
| notes + limitations | MPE expression | `post-alpha/mpe` |
| notes + limitations | opt-in telemetry, 24-field allowlist | `post-alpha/telemetry` |
| notes | auto-mastering, `zene master` subcommand + candidate list | `post-alpha/auto-mastering` (no `master` action in `src/core/main.cpp`) |
| notes + limitations | the exit-abort/teardown fix, and the test-source registration gate | `post-alpha/test-hygiene` (`git log --all -S"stranded"`) |
| notes | recorder wraparound clamp | `post-alpha/recording-realtime` (the writer thread at `TrackRecorder.cpp:149-180` clamps nothing; its only `std::clamp` is the channel index at `:57`) |
| notes | capture path no longer takes the model lock, 1,024,000-frame conservation | `post-alpha/recording-realtime` (the lock-free SPSC ring itself is older — `include/TrackRecorder.h:3,50,66`, `TrackRecorder.cpp:137-141`); the measurement is nowhere in this tree |
| notes + limitations | failed save reported, `.bak` kept, degraded-state fix | `post-alpha/saveload-integrity` (the defect is still visible here: `DataFile.cpp:435`/`:438` discard the rename results and `save` returns `true`) |
| notes | mixer concurrency fixes + TSAN evidence | `post-alpha/mixer-concurrency` |
| limitations | coverage 84.34 % | `post-alpha/coverage-green` — see §3.3 |
| limitations | the CC0 logo / replacement mark | `post-alpha/brand-placeholders` |

Fifteen markers were **also added** to bullets that had none, because they assert something this base
does not contain: the convention is "no unverified claim ships unmarked", and adding the marker is
faithful to it where deleting the claim would not be. They are the warp/racks/VCA/MPE/telemetry bullets,
the exit-abort and test-source bullets, the mixer/TSAN bullet, the duplicated save bullet, and the two
migration sentences (§3.4).

### 3.3 Left as markers — because they need an artefact that does not exist yet

| document | marker | blocker |
|---|---|---|
| notes | the artefact list and the SHA-256 digest block | the published release. The publish step appends the digests (`docs/RELEASING.md` steps 4–5); the asset-name pattern is verified (`zene-<version>-<platform>.<ext>` from `CPack_PACKAGE_FILE_NAME`) but a digest cannot be written before a byte exists |
| limitations | the coverage number "from the frozen tree" | no coverage run exists for this base. `84.34 %` is the unmerged `coverage-green` lane's figure; the coverage run that **is** merged measured **81.46 %** fork-scope (`docs/COVERAGE-RUN.md`: 3747/4600 over 61 files with a record). Gate 2 must be re-run on the frozen tree and whatever it says is the number that ships |
| limitations | "an older 1.3-alpha build opens a 0.2 project and silently drops parts of it" | needs an LMMS 1.3.0-alpha binary; it is a claim about *another* program and cannot be executed here |
| notes | TODO 5, "have an independent reader compare this text against the built binary" | a second party. This lane is the author of these resolutions; an independent reader is a nomination for the parent |

### 3.4 Two places where the reviewed text **contradicts** the tree (reported, not silently fixed)

Both are marked in place, with the finding written into the marker:

1. **"Coming from 0.1.0-alpha — your settings and your projects are migrated rather than orphaned"**
   (notes, and the equivalent sentence in limitations' "The name, honestly"). There is **no migration
   code**: `grep -rniI "migrat" src/ include/` matches only comments about the *plugin* migration, and
   `docs/WAVE-R-RENAME.md` §6 records that `~/.lmmsrc.xml`, the config root, `~/Documents/lmms/` and the
   `lmms-workspace` marker were **deliberately kept** because renaming them would orphan an existing
   install. There is no "new configuration location", so there is nothing to migrate. The honest
   phrasing is that 0.2.0-alpha reads the same files 0.1.0-alpha did. **The sentence must be rewritten
   or removed before the tag.**
2. **The release notes carry the same save-failure claim twice** — "Saving no longer reports success
   when it failed" and "Save failures are no longer reported as successes" (the second longer). Both are
   unverifiable here (same unmerged lane). Which one survives is a freeze decision; this lane did not
   delete either.

---

## 4. The capability contract (Block D2/D3) — cross-checked against the build

### 4.1 The gate, run both ways

    $ bash tests/release-honesty-gate.sh --header build/lmmsversion.h
    HONESTY_HEADER_EXIT=0
      [PASS] vst3-hosting     ON matches ON
      [PASS] vst3-instrument-hosting ON matches ON
      [PASS] clap-hosting     ON matches ON
      [PASS] session-view     OFF matches OFF
      [PASS] wasm-sandbox     OFF,OFF matches OFF
      [PASS] stem-separation  OFF matches OFF
    RESULT: PASS — all 6 documented feature(s) are what this build contains

    $ bash tests/release-honesty-gate.sh --header build/lmmsversion.h --artifacts build
    HONESTY_ARTIFACTS_EXIT=1
      [PASS] vst3-hosting     ON matches ON; module build/plugins/libvst3effect.so
      [FAIL] vst3-instrument-hosting WANT_VST3 says ON but no '*vst3instrument*.so' exists under build
      [PASS] clap-hosting     ON matches ON; module build/plugins/libclapeffect.so
      [PASS] session-view / wasm-sandbox / stem-separation: OFF matches OFF
    RESULT: FAIL — 1 of 6 documented feature(s) do not match this build

**What the header-only run does and does not prove** (stated because the brief asked): without
`--artifacts` the gate checks the *option* rows only — the requirement "a documented-present feature must
be explicitly ON, and `AUTO` is not `ON`" — and it proves nothing about whether the module those options
imply was built. That is why the second run exists, and why the header-only green must not be quoted as
"the release is honest".

**The FAIL is the correct verdict and the manifest is not touched.** `plugins/Vst3Instrument/` does not
exist on this base (`ls plugins/Vst3*` → `plugins/Vst3Effect` only), so `vst3instrument` cannot be built
here. The manifest's own comment already states the host target is
`BUILD_PLUGIN(vst3instrument …)` in `plugins/Vst3Instrument/CMakeLists.txt`, gated by `WANT_VST3` — the
same option the effect host uses. So **D2's new claim is unbacked until `post-alpha/instrument-hosting-impl`
lands**; the fix is a merge, never an edit to `tests/advertised-features.tsv`.

Not the local-box trap: this box **does** have the SDK provisioned (`provision-plugin-hosting-deps.sh`
verified the pins in place, `VST3 SDK v3.8.1_build_84 (3cdf9ca5d, MIT)`, `CLAP 1.2.10 (195b42a00, MIT)`),
and the effect modules **were** built — `build/plugins/libvst3effect.so`, `build/plugins/libclapeffect.so`.
The one missing module is missing because its source lane is not merged, not because of the SDK.

### 4.2 What the base actually contains, versus what the notes claim

Verified merged into `34c1f4f86` (so their bullets are supportable here): crash-report, autosave,
plugin-scan, lufs-meter + lufs-wire, midi-learn + midi-race, stem-export, automation-modes, clip-slice0,
midi-depth, lua-api, mmpz-git-depth, render-determinism, wave-r-rename, oop-hosting, pipeline-hardening,
docs-security, gate-debt, readme-truth, pr594, vst3-instrument-fixture.

**Not merged** (bullets marked, §3.2): auto-mastering, brand-placeholders, instrument-hosting-impl,
instrument-view-safety, vca, racks, mpe, telemetry, warp, saveload-integrity, recording-realtime,
mixer-concurrency, session-scheduler, cmd-auto, cmd-notes, cmd-plugins, coverage-green, test-hygiene,
headless-load, control-hardening, stable-ids, router-live, agent-control-surface, agent-surface-gate.

Also: **the README's roadmap section directly contradicts the notes** — it lists "Warp engine", "Racks +
Chain Selector" and "MPE" as *not available in the current build*, while the notes advertise all three as
new. On this base the README is right and the notes are wrong; at the freeze (if those lanes merge) the
notes become right and the README's roadmap block becomes false. **One of the two must change at the
freeze** — deliberately left alone here because choosing requires the freeze tree, and there is no
honest way to write either version from this base.

---

## 5. README and version-facing sweep

| file | change | why |
|---|---|---|
| `README.md` Download | names **0.2.0-alpha**, links `releases/tag/v0.2.0-alpha`, names v0.1.0-alpha as the previous release | it named 0.1.0-alpha as current and linked the rehearsal build's release |
| `README.md` Download, capability sentence | points at the limitations page, says instrument hosting is new/narrow, **CLAP is effects only** | it said the alpha "cannot host instrument plugins … hosts third-party plugins as effects only" — false once the release notes ship |
| `README.md` "What is Zene Studio" | the VST3 bullet updated to match the notes | it said instrument hosting is **not implemented** |
| `docs/RELEASING.md` | new "0.2.0-alpha — what changes in this runbook" section; the publish sequence moved from `lmms-0.1.0-*` to `zene-0.2.0-alpha-*`; smoke test expects `Zene Studio 0.2.0-alpha`; artefacts/logs written under `build/` instead of `/tmp` | the runbook is the parent's Block C/F instrument and it still published the *previous* release's asset names (`lmms-*`, which the rename retired) and called v0.1.0 "the release happening now" |
| `.github/ISSUE_TEMPLATE/alpha-feedback.yml` | asks for 0.2.0-alpha; explains the untagged string (`0.1.0-alpha.123+34c1f4f`); names `<working dir>/crash-reports/zene-crash-report.txt` | it asked for 0.1.0-alpha, and claimed the build "writes no log file" while the crash reporter (merged, `docs/CRASH-REPORTER.md:207-212`) writes exactly that file |

Deliberately **left alone**, with the reason:

* `README.md` "Build status" (run IDs on `0c23587d2` and the v0.1.0-alpha tag) — historical CI record,
  correctly dated, and the paragraph already tells the reader to read `gh run list` instead.
* `README.md` roadmap block — see §4.2; the contradiction needs the freeze tree to resolve.
* `docs/STATUS.md`, `docs/RELEASE-NOTES-v0.1.0-alpha.md`, `docs/WAVE-R-RENAME.md`, `docs/phase-f/**`,
  `docs/INTEGRATION-VERIFY.md`, `docs/COVERAGE-RUN.md` — release/program records of what happened, not
  claims about what 0.2.0 will contain. `docs/WAVE-R-RENAME.md` §6 lists the historical documents as
  deliberately kept.
* `docs/PLUGIN-SCAN-CACHE.md:4` ("on the `v0.1.0-alpha` tag") — a dated statement about when that work
  landed; still true.
* `docs/RELEASING.md`'s 0.1.0 body and its rollback section — kept as the worked example, with the
  delta section above it.
* `tests/QA-GATES.md`, `docs/CONVENTIONS.md`, `docs/INTEGRATION-MERGES*.md` — the stale-count class is
  checklist item B2.8 and belongs to the docs/gate-hygiene work, not to release prep.

---

## 6. Guarding the class: `tests/release-version-gate.sh` (with a red control)

The honesty gate reads build **options**; it cannot see what the release is **called**. The new gate
fails when the version and its documents or its tag disagree:

* the tree's `CMakeLists.txt` version must have `docs/RELEASE-NOTES-v<version>.md` with a matching H1;
* the README's Download section must name that version and link `releases/tag/v<version>`;
* on a tag build (`GITHUB_REF=refs/tags/…`) the ref must be exactly `v<version>`;
* a `v*` tag pointing at HEAD must be `v<version>`, and a `v<version>` tag must be in HEAD's history.

It is wired into **all six package jobs** in `.github/workflows/build.yml` (right after the honesty
guard, with an explicit `shell: bash` for the Windows job), which is the mechanism that stops the *next*
release. Verified as YAML: six steps, correct shells.

**Red control** — `tests/test-release-version-gate.sh`, exit 0, 8/8 controls:

    8 controls, 8 passed, 0 failed
    G0   real tree                                   exit 0
    G0b  real tree, GITHUB_REF=refs/tags/v0.2.0-alpha exit 0
    R1   release build from refs/tags/v0.1.0-alpha    exit 1  ← the tag/version drift
    R2   tree says 0.3.0-alpha, documents say 0.2.0   exit 1
    R3   README reverted to the previous release      exit 1
    R4   no release notes under the declared version  exit 1
    R5   HEAD tagged v0.1.0-alpha (real git repo)     exit 1
    R6   v0.2.0-alpha tag on a commit HEAD does not descend from   exit 1

Full log: `tests/evidence/release-prep-0.2.0/07-test-release-version-gate.txt`.

---

## 7. The gate runner's `set -u` empty-array defect (rode along, as instructed)

`tests/run-all-gates.sh` declared `declare -a RESULTS` / `declare -a SKIPPED`. A **declared but
uninitialised** array is unset, so `${#SKIPPED[@]}` under `set -uo pipefail` is an unbound variable.
`SKIPPED` is empty on exactly the best run — the one where every gate executed — so the summary block
died precisely when it had nothing to skip, and was masked on every run where a gate skipped (which is
why every run tonight printed a summary).

**Before** — the file's own lines (61-62), executed verbatim, then the expansion it feeds:

    $ sed -n '61,62p' tests/run-all-gates.sh
    declare -a RESULTS
    declare -a SKIPPED
    $ bash repro           # same two lines + a seeded RESULTS + `if [[ ${#SKIPPED[@]} -gt 0 ]]`
    FILE_LINES_BEFORE_EXIT=1
    repro: line 5: SKIPPED: unbound variable

**After** — the same extraction, now `=()` (the idiom `tests/fork-sources-gate.sh:131-133` already uses):

    $ grep "^declare -a RESULTS\|^declare -a SKIPPED" tests/run-all-gates.sh
    66:declare -a RESULTS=()
    67:declare -a SKIPPED=()
    $ bash repro
    FILE_LINES_AFTER_EXIT=0
    no skips
    row=1|ctest|PASS
    # and with one skip seeded:  "skipped: 1 of 1 gates did not run"

`bash -n` clean on both touched files (`SYNTAX_RUN_ALL_EXIT=0`, `SYNTAX_MUTATION_EXIT=0`). A normal run
still prints its summary — `run-all-gates.sh --no-mutation` reports gates 2 and 5 as SKIP with their
hint lines, and its summary diff against the earlier full run shows **only** the expected
`mutation PASS → SKIP` change. No `|| true` anywhere: the fix removes the crash, it does not hide it.

**Other unguarded expansions of possibly-empty arrays, listed as asked:**

| file:line | expansion | status |
|---|---|---|
| `tests/run-all-gates.sh:61-62` | `RESULTS`, `SKIPPED` (11 expansion sites: 175, 191, 192×2, 193, 203, 204×2, 208×2) | **fixed** — both arrays are now always *set*, which closes every site |
| `tests/mutation-gate.sh:465` | `ROWS`, expanded at `:683` and `:695` under `set -uo pipefail` | **fixed** (`declare -a ROWS=()`) — same class, append-only array, would have died on a zero-mutant run. Flagged here because it is a second file: revert that one line if you consider it out of scope |
| `tests/fork-sources-gate.sh:131-133` | `UNREGISTERED`, `INHERITED`, `TOOLING` | already correct (`=()`) |
| rest of `tests/*.sh`, `tools/*.sh` | `grep -rn "declare -a"` finds no other declaration | none |

A static guard for the class is a one-liner if you want it in the gate suite
(`grep -nE '^[[:space:]]*declare -a [A-Za-z_]+[[:space:]]*$' tests/*.sh tools/*.sh` should return
nothing) — not added here, to keep this lane's surface to what was asked.

**Limit, stated plainly:** the **zero-skip path is proven at the shell-semantics level** (the file's own
declaration lines executed against the file's own expansion), **not by a full zero-skip ten-gate run** —
that needs Gate 2 (`--with-coverage`, an instrumented build) and Gate 5 (the mutation sweep), which are
not worth spending tonight, and Gate 6 is red here anyway (§8). No claim is made beyond what ran.

---

## 8. Gates and tests actually run (exact commands, unpiped exit codes)

Build first — `tools/local-ci.sh` with the brief's job cap, logging into `build/`, `df -h` before and after:

    $ df -h /home   →  Before: 286G size, 241G used, 31G avail (89%)
    $ JOBS=2 bash tools/local-ci.sh --build-dir build --jobs 4 > build/local-ci.out 2>&1
    LOCAL_CI_EXIT=0
      provision EXIT=0   (both pins already in place, verified: VST3 3.8.1_build_84 / CLAP 1.2.10)
      configure EXIT=0
      build     EXIT=0        # 4 × "Terminated signal" lines: 0;  "error:" lines: 0
      ctest     EXIT=0
      ctest totals: 100% tests passed, 0 tests failed out of 47      # from build/tests
    $ du -sh build → 7.3G       $ df -h /home   →  After: 233G used, 40G avail (86%)
    linux-x86_64: REPRODUCED;  the other six jobs: NOT-REPRODUCIBLE-HERE (toolchain absent, as the
    script prints per job);  one deviation: no Qt5 dev files → -DWANT_QT6=ON (CI installs qtbase5-dev)

`ctest` ran from `build/tests` (47 tests, not 0 — the top-level directory has no `CTestTestfile.cmake`
and reports 0, which is an error). Log: `tests/evidence/release-prep-0.2.0/11-ctest.txt`.

| gate | command | exit | verdict |
|---|---|---|---|
| release honesty (option rows) | `bash tests/release-honesty-gate.sh --header build/lmmsversion.h` | **0** | 6/6 PASS |
| release honesty (+ modules) | `… --header build/lmmsversion.h --artifacts build` | **1** | 1 of 6 FAIL: `vst3-instrument-hosting` (unmerged lane, §4.1) |
| release version (new) | `bash tests/release-version-gate.sh` | **0** | tree/notes/README agree on 0.2.0-alpha |
| that gate's red control (new) | `bash tests/test-release-version-gate.sh` | **0** | 8 controls, all as declared |
| Gate 9 fork-sources | `bash tests/fork-sources-gate.sh` | **0** | 138 fork, 1014 inherited, 14 tooling, 0 unregistered |
| Gate 6 upstream-regression | `bash tests/no-upstream-regression-gate.sh` | **1** | **one** violation, `tools/ncpu-shim.c` — pre-existing, not mine (§8.1) |
| full runner | `bash tests/run-all-gates.sh` | **1** | 1 ctest PASS · 2 coverage SKIP · 3 PASS · 4 PASS · 5 PASS · 6 FAIL · 7 PASS · 8 PASS · 9 PASS |
| full runner, after the array fix | `bash tests/run-all-gates.sh --no-mutation` | **1** | same, with 5 SKIP as passed; summary intact |
| Gate 2 coverage | — | **skipped** | needs an instrumented build; explicitly a freeze item (B7 / coverage-green). The merged run's number is 81.46 % (`docs/COVERAGE-RUN.md`); the notes' 84.34 % is the unmerged lane's |

**The expected `run-all-gates.sh → 3` is not reachable on this base**, because `3` (`PASS-WITH-SKIPS`)
requires every executed gate to pass and Gate 6 fails first. The failure is pre-existing and provably not
introduced here:

### 8.1 Why Gate 6's red is not this lane's

The first Gate 6 run was made **before this lane committed anything**, so its input set
(`git diff --name-only <gate base>..HEAD`) was the pristine committed base `34c1f4f86` — my uncommitted
edits were not even in scope. It reported exactly one violation, `tools/ncpu-shim.c` "undeclared change to
upstream-inherited code", introduced by `433d20738` (render-determinism, an ancestor of the base):
`tools-sources.txt` *mentions* the file in two comments but has no entry line for it, and the gate's
fork-tooling classifier requires an exact line match. Re-run after this lane's commits, the verdict is
**unchanged: one violation**, and every file this lane touched is classified allowed
(`CMakeLists.txt` → build/config; `docs/**`, `README.md` → docs; `tests/**`, `.github/**` → allowed).
The one-line fix (register `tools/ncpu-shim.c` in `tests/tools-sources.txt`) belongs to the gate-hygiene
lane per checklist B2.4, so it is **reported, not done**.

---

## 9. What the freeze still has to do (exact list)

1. **Merge the lanes §3.2 lists**, then re-check every marker left in the two documents; if any of those
   lanes does **not** land, the corresponding bullets must be deleted from the notes, not published.
2. **Decide the two contradictions:** the migration sentence (§3.4.1) and the duplicated save bullet
   (§3.4.2). Both must be edited before the tag — this lane deliberately did not guess.
3. **Resolve the README/notes contradiction** on warps, racks and MPE (§4.2).
4. **Merge this branch last** (it carries the manifest and both documents; anything merged after it can
   reintroduce a disagreeing claim).
5. **Re-run Gate 2 (coverage)** on the frozen tree and paste the number into
   `docs/KNOWN-LIMITATIONS.md` — `84.34 %` is currently an unmerged lane's figure.
6. **Run the honesty gate against the release artefacts, not a local build** —
   `bash tests/release-honesty-gate.sh --header <artifact>/lmmsversion.h --artifacts <artifact-dir>` —
   and require **exit 0**. On this base it cannot pass: `vst3instrument` has no source. If
   `scripts/release-verify.sh` is the wrapper for that (checklist F2 cites it), note that **it does not
   exist on this base** (`ls scripts/` → `fetch-wasmtime.sh` only).
7. **Tag `v0.2.0-alpha` on the release commit** and confirm the tag job's `--version` says
   `Zene Studio 0.2.0-alpha`; the new version gate enforces the tag/version agreement in all six jobs.
8. **Restore the unsigned-build first-run steps** (FUSE 2 / `--appimage-extract-and-run`, SmartScreen
   *More info → Run anyway*, macOS *Open Anyway*) into the 0.2.0 documents — the limitations page points at
   the release notes for them and neither carries them (§2). Preserved page:
   `docs/KNOWN-LIMITATIONS-v0.1.0-alpha.md`.
9. **Fill the artefact list and digests** at publish, from the published bytes (§3.3).
10. **Nominate an independent reader** for the notes (TODO 5) — this lane is the author of the
    resolutions and cannot be its own verifier.
11. **Clear Gate 6** by registering `tools/ncpu-shim.c`, and check whether the whole-tree scope is to be
    re-anchored (checklist B2.1/B2.4) — neither is this lane's.
12. **Do not touch `docs/STATUS.md` from here** — it is a live status document the docs lanes rewrite,
    and this lane's base is mid-merge-train.

---

## 10. What could NOT be done, and the limits hit

* **The tag path was not executed** (no tagging from this lane) — the fallback path proved identical text
  (§1.3).
* **`PROJECT_EMAIL` cannot be proved in the binary or on Linux** — its only consumer is Windows-only
  installer metadata (§1.4). Given the brief's "prove the built binary reports it" is unsatisfiable for
  this variable by construction, this is the closest honest evidence: definition correct, upstream
  address gone tree-wide, single consumer identified.
* **Gate 2 (coverage) was skipped**, deliberately: an instrumented build costs a second ~7 GB build dir
  and the number is a freeze item (§8).
* **Windows/macOS/arm64 packaging is unbuilt here** (no toolchain); `zene-0.2.0-alpha-*` is a verified
  *pattern*, not a verified artefact list.
* **The instrument-hosting end-to-end run and the editor-window run were not reproduced** — their lanes
  are not merged, so nothing about them is claimed.
* Build cost: configure 46 s, full build ~15 min at `-j4` (peak memory observed under the box's 30 GB;
  **zero** `Terminated signal` lines, so no OOM this time), 7.3 GB of build output, disk 31 GB → 40 GB
  available (siblings freed space mid-run).
* Evidence is committed, not in `/tmp` (checklist B2.9): `tests/evidence/release-prep-0.2.0/`.
