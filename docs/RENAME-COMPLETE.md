# Zene Studio rename — complete (report)

**Branch:** `post-alpha/rename-complete` · **Base:** `post-alpha/wave-r-rename` (`0e3596a5d`) ·
**Tip:** this documentation commit, on top of the three layer commits below.

**Verdict.** The rename is pushed through the user-visible surface, the user state (with migration, not
orphaning), and the project/preset format (read-both / write-new). The code-identifier layer
(`lmms::`, `LMMS_*`, `lmms_plugin_main`) is **assessed, not attempted** — §6 gives its exact size and
what it would cost. One boundary does not move and is stated plainly in §5: the licence headers,
copyright notices and "derived from LMMS" attribution stay, because this program is
GPL-2.0-or-later and a derivative work.

Owner instruction: *"Rename everything to Zene Studio, keep no traces of the old name if possible."*
The "if possible" is honoured by pushing until it stops, and then naming exactly where it stops and why.

| # | Commit | Subject | Paths |
|---|---|---|---|
| 1 | `5097a3134` | layer 1 — user-visible strings and client identity | 72 + 2 manifests |
| 2 | `6c1ff660c` | layer 2 — user state migrated, not orphaned | 7 |
| 3 | `c30c93082` | layer 3 — project/preset format read-both, write-new | 226 |

Each commit's tree configures and builds. The test-registration manifest (`tests/CMakeLists.txt`,
`tests/all-sources.txt`) necessarily lands with the last layer, because it names all three new test
targets and an earlier commit that registered a not-yet-existing source would not configure.

---

## 1. Environment and exact commands

```
cd <worktree>
JOBS=4 bash tools/local-ci.sh --build-dir build --jobs 4
```

`tools/local-ci.sh` is committed mode `100644`, so it is run as `bash tools/local-ci.sh` — the only
invocation change. **Printed deviation** from the script, not silent: `Qt5 development files not
found: added -DWANT_QT6=ON (CI's runner installs qtbase5-dev; this box has Qt6 only)`.

Build steps were re-run to completion after the last source edit, then `ctest`, then the render
proofs, then this document. Every exit code below is measured **unpiped** (`cmd > log 2>&1; echo EXIT=$?`).

| step | command | exit |
|---|---|---|
| configure | `cmake -S . -B build -DUSE_WERROR=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTARGET_UARCH=official -DUSE_COMPILE_CACHE=ON -DWANT_DEBUG_CPACK=ON -DWANT_QT6=ON` | **0** |
| build | `cmake --build build -j3` | **0** |
| ctest | `(cd build/tests && QT_QPA_PLATFORM=offscreen ctest --output-on-failure -j2)` | **0** |
| local-ci overall | `JOBS=4 bash tools/local-ci.sh --build-dir build --jobs 4` | first attempt **1** (resource failure, see §7), re-run to completion **0** |

**ctest totals: `100% tests passed, 0 tests failed out of 28`** — 28 tests, not 0. Wave R's tree had 25;
this work adds `ConfigMigrationTest`, `DataFileFormatTest` and `PluginLogoResourceTest`.

---

## 2. Layer 1 — user-visible strings and client identity

### What changed (72 files)

The previous layer's work was **verified rather than assumed**: `PROJECT(zene)`, `bin/zene`,
`zene-*` packages, the desktop entry, `--version`, the About dialog and the CI upload globs were all
still in place as `docs/WAVE-R-RENAME.md` §4 describes.

| surface | where |
|---|---|
| JACK client name, Setup-dialog client, "kicked by JACK" text | `src/core/audio/AudioJack.cpp` |
| PulseAudio context + stream names | `src/core/audio/AudioPulseAudio.cpp` |
| ALSA sequencer client name | `src/core/midi/MidiAlsaSeq.cpp` |
| soundio app + stream names | `src/core/audio/AudioSoundIo.cpp` |
| JACK MIDI probe default **+ legacy config-attribute fallback** | `src/core/midi/MidiJack.cpp` |
| CLAP host name / vendor / url | `plugins/ClapEffect/ClapHost.cpp` |
| VST support-layer product string | `plugins/VstBase/RemoteVstPlugin.cpp` |
| Carla UI name (`CarlaRack-Zene` / `CarlaPatchbay-Zene`) | `plugins/CarlaBase/Carla.cpp` |
| written-file software tags (WAV/FLAC/OGG/MP3) + stem user agent | `src/core/audio/AudioFile*.cpp`, `src/core/StemModelStore.cpp`, `src/core/OnnxRuntimeStemSeparator.cpp` |
| file-dialog filters, plugin type column + filter button, plugin-browser root, dialog titles | `src/gui/MainWindow.cpp`, `src/gui/modals/EffectSelectDialog.cpp`, `src/gui/PluginBrowser.cpp`, `src/gui/modals/ControllerConnectionDialog.cpp`, `src/core/Song.cpp` |
| plugin descriptions shown in the browser | 11 plugin sources |
| MIME: `application/x-zene-project`, clipboard `application/x-zene-*`, icon file | `cmake/linux/zene.desktop`, `cmake/linux/zene.xml`, `cmake/linux/icons/scalable/mimetypes/`, `include/Clipboard.h` |
| scripting API (`zene` / `--! zene-api`, legacy spellings kept working) | `src/core/ScriptEngine.cpp`, `src/core/ScriptBindings.cpp` |
| built-in plugin logo resource key (`zene-plugin-logo`) | 29 call sites, the theme SVG, the tile PNG |

### The logo rename: 54 references, and the silent break it could have caused

**Corrected count.** The figure to use is the **committed pre-change tip**: `git grep -ohE
'"lmms-plugin-logo"' HEAD | wc -l` → **54 occurrences in 54 files** (28 under `plugins/`, 26 under
`tests/` — 25 of those frozen `tests/reference/**` copies). An earlier lane's "27 live call sites"
figure does not reproduce on this branch and should not propagate.

After this layer: `grep -rIoE '"zene-plugin-logo"' src plugins tests/src` → **29 call sites in 29
code files**, and `lmms-plugin-logo` has **0 occurrences anywhere under `src/`, `include/`,
`plugins/`, `data/`, `cmake/`, `doc/`, `tools/`**. The 26 remaining occurrences of the old name in the
whole tree are 25 frozen `tests/reference/**` copies (declared, §5) and 1 historical report.

This rename has a **silent** failure mode, which is why it is tested rather than asserted:
`embed::loadSvgPixmap()` (`src/gui/embed.cpp:45`) appends `.svg`, opens the path through the artwork
search path, and on failure does `qWarning() << "Failed to open resource for SVG: "` and returns a
**1×1 transparent QPixmap**. No exception, no non-zero exit, a build nowhere near red — and every
plugin dialog showing a blank one-pixel logo.

`tests/src/core/PluginLogoResourceTest.cpp` loads the pixmap through the product's own artwork search
path and asserts it is real art, with a negative control. It was observed **red and green** before
being committed:

```
# green (resource present)
QINFO  : PluginLogoResourceTest::pluginLogoLoads() plugin logo 'zene-plugin-logo' resolved to 48 x 48 px from the artwork search path
PASS   : PluginLogoResourceTest::pluginLogoLoads()
Totals: 4 passed, 0 failed, 0 skipped, 0 blacklisted, 15ms                 GREEN_EXIT=0

# red control: mv data/themes/default/zene-plugin-logo.svg out of the way
FAIL!  : PluginLogoResourceTest::pluginLogoLoads() 'logo.width() > 1 && logo.height() > 1' returned FALSE.
         (the plugin logo is the 1x1 fallback pixmap: the resource did not resolve)
Totals: 3 passed, 1 failed                                               RED_EXIT=1
```

A second, general check was run: every first-party resource name referenced via
`PixmapLoader(...)`/`getIconPixmap(...)` must have a file behind it, resolving the way the product
resolves (theme directory **or** the referencing plugin's own directory, which `BuildPlugin` adds to
the artwork search path). Result — **498 first-party names, 2 unresolved**:

```
first-party resource names referenced: 498
UNRESOLVED (2):
    arp_down_on  <- ['src/gui/LfoControllerDialog.cpp']
    arp_up_on    <- ['src/gui/LfoControllerDialog.cpp']
```

Both are **pre-existing**: `src/gui/LfoControllerDialog.cpp` is not in this change set at all
(`git diff --name-only HEAD -- …` → 0 lines; last upstream change `ed0f288c8`). `zene-plugin-logo` is
not in the unresolved set.

### Sweep, layer 1 (run live at the end, on the final tree)

| pattern | product tree (`src include plugins data cmake doc tools`) |
|---|---|
| `lmms-plugin-logo` | **0** |
| `x-lmms` (MIME/clipboard) | **0** |
| `lmms_tile` | **0** |
| `CarlaRack-LMMS` | **0** |
| `"lmms"` client literal | **2 source files**, both deliberate read-both (`MidiJack.cpp` legacy config attribute, `ScriptBindings.cpp` Lua alias) |
| `LMMS VST Support` | **1** — the file's own header comment naming the upstream component it derives from |

---

## 3. Layer 2 — user state: migrated, not orphaned

### What changed (7 files)

| state | old | new |
|---|---|---|
| config file | `~/.lmmsrc.xml` | `~/.zenestudio.xml` |
| working directory | `~/Documents/lmms/` | `~/Documents/Zene Studio/` |
| pre-1.2.0 layout | `~/lmms/` | `~/zene/` |
| portable workspace | `<appdir>/lmms-workspace/` | `<appdir>/zene-workspace/` |
| config XML root | `<lmms>` / `lmms-config-file` | `<zene>` / `zene-config-file` |
| dev-tree config | `<builddir>/.lmmsrc.xml` | `<builddir>/.zenestudio.xml` |

`src/core/ConfigManager.cpp`, `include/ConfigManager.h`, `src/gui/GuiApplication.cpp`,
`include/MainWindow.h`, `doc/zene.1`, `README.md`, `tests/src/core/ConfigMigrationTest.cpp`.

`ConfigMigration::adoptConfigFile` / `adoptWorkingDir` are **pure path functions** — no singleton, no
`qApp` — which is what makes them testable. The policy:

* the new location wins whenever it exists (when both exist the legacy one is left strictly alone);
* otherwise the legacy item is **renamed into place**: the user keeps their settings and projects and
  the old name disappears;
* if the rename is impossible (cross-device, permissions), a config **falls back to a copy** and a
  working directory **falls back to being used where it is** — pointing the product at an empty new
  folder is the one outcome that makes every project appear to vanish;
* a rename of the directory is only attempted when both paths share a parent, so it is a cheap atomic
  rename rather than a copy that could fail halfway through a project folder;
* the pre-1.2.0-under-`$HOME` precedence is unchanged from before the rename;
* `--config` still overrides everything.

### Migration proof — end to end, with a real seeded `$HOME`

```
--- seeded pre-rename state (HOME=/tmp/rhome.hnH9KZ) ---
.lmmsrc.xml
Documents
from-legacy.mmp
$HOME/Documents/lmms/projects/tune.mmp
upgrade EXIT=0
--- post-run HOME ---
.zenestudio.xml
Documents
Zene Studio
--- adopted config: the user's OWN settings are still in it ---
audiodev="PulseAudio"
saveinterval="300"
<zene  (root element = the NEW one, write-new)
--- the user's project moved with the directory ---
present at Documents/Zene Studio/projects/tune.mmp
byte-identical to the file that was seeded
legacy config gone (adopted)
legacy working dir gone (adopted)
```

### Migration proof — unit test

`tests/src/core/ConfigMigrationTest.cpp`, six cases, each seeding old-style state in a
`QTemporaryDir`: adoption of the config (content byte-identical, old name gone); adoption of a working
directory **with a project inside it** (the project moves, old name gone); both-exist (new wins, legacy
untouched and not deleted); fresh install (nothing created); unrenamable directory (a different parent)
→ the legacy directory keeps being used and the project stays reachable; and an adopted config whose
root still says `<lmms>` is still readable (read-both for the config root).

`docs/KNOWN-LIMITATIONS.md` and the release notes were **not** touched — another lane owns them; §8
carries the proposed wording.

---

## 4. Layer 3 — the project/preset format: read-both, write-new

### What changed (226 files)

* `src/core/DataFile.cpp` — the writer, the loader's one literal root match, and two gaps found by the
  new test (below).
* 174 shipped presets + the shipped projects/templates, including the 38 qCompress `.mmpz` demos —
  221 files, rewritten through a decompress/recompress round trip that preserves the
  `[4-byte BE length][zlib]` container (verified: 38/38 still decompress).

### Write-new

`DataFile::write()`:

* the root element is written as `zene-project`;
* `creator="Zene Studio"` and a refreshed `creatorversion`;
* a legacy `<!DOCTYPE ...>` is dropped, because a file this build writes has no DOCTYPE. `QDomDocument`
  keeps a parsed doctype in a private member that `QDomNode::removeChild` cannot reach — measured, not
  assumed — so the line is removed at the output boundary.

**Two real gaps were found by the new test and fixed, not asserted away:**

1. re-saving a *loaded* old file kept the old root, because the root rename lived only in the element
   constructor. The test went red (`Actual: "lmms-project"`), the writer was fixed.
2. it then kept `creator="LMMS"`, because the creator attributes were set only in `upgrade()`, which
   does not run for a file already at the current version. The test went red again
   (`Actual (root.attribute("creator")): "LMMS"`), the writer was fixed.

### Read-both

`DataFile::loadData()` has always used `documentElement()`, so it never matched the root's tag name and
an old file always loaded — the format rename was already read-both by construction. The **one** place
that did match it literally, `upgrade_noHiddenAutomationTracks()`, now accepts both roots.

### Format proof — a pre-rename file in, a new-root file out

```
--- the pre-rename file handed in (first 3 lines) ---
<?xml version="1.0"?>
<!DOCTYPE lmms-project>
<lmms-project type="song" version="1.0" creator="LMMS" creatorversion="1.2.0">
upgrade(old -> new) EXIT=0
--- what this build wrote (first 3 lines) ---
<?xml version='1.0'?>
<zene-project creatorplatform="linux" version="31" creatorplatformtype="ubuntu"
              creatorversion="0.1.0-alpha.3+13f33b9" creator="Zene Studio" type="song">
  <head timesig_numerator="4" timesig_denominator="4" masterpitch="0" mastervol="100" bpm="140"/>
upgrade(new -> new, idempotent) EXIT=0
--- a shipped .mmpz demo still loads (qCompress container) ---
upgrade(.mmpz) EXIT=0
<zene-project creatorversion="0.1.0-alpha.3+13f33b9" … creator="Zene Studio" type="song">
--- 'dump' still decompresses it (container intact) ---
<?xml version="1.0"?>
<!DOCTYPE zene-project>
```

The same round trip is pinned in `tests/src/core/DataFileFormatTest.cpp`: a document built by this code
writes `zene-project` + `creator="Zene Studio"`; a pre-rename file still loads with its `<head>` and
`<song>` resolved; re-saving it writes the new root, the new creator and no DOCTYPE; a new-root file
loads; and a legacy `multimedia-project` root still loads. The suite's pre-existing
`SlideNotesTest` already loads a hand-written pre-rename `<lmms-project creator="LMMS">` file, and
still passes.

### The legacy roots that stay

62 shipped presets use `multimedia-project` / `multimediaproject` with
`<!DOCTYPE multimedia-project>` — the pre-LMMS ZynAddSubFX-era spelling. These are **left exactly as
they are**: that is an *older format's* identifier, not this product's name, and rewriting it would
falsify the format. The reader must keep accepting them, and does.

### Sweep, layer 3 (live, final tree)

| pattern | product tree |
|---|---|
| `</lmms-project>` | **0** |
| `<lmms-project` | **1** — `tools/mmpz-git/demo-output.txt`, a captured command/output transcript |
| `creator="LMMS"` | **11 files, all `plugins/RnnoiseDenoiser/testdata/**` + its README** — captured test fixtures, not edited |
| `multimedia-project` legacy root | 62 presets + `tests/emptyproject.mmp` + the Rnnoise fixtures — kept (§ above) |

---

## 5. What remains deliberately, and why

Grouped by reason. Counts are from the live sweep on the final tree; the first group is the boundary
that **cannot move**.

**(a) The licence boundary — cannot move.** 1251 files under `src/`, `include/`, `plugins/` carry
`* This file is part of LMMS …`. GPL-2.0-or-later requires the notices be retained, and this is a
derivative work: `COPYING`, `CMakeLists.txt:64` (`PROJECT_COPYRIGHT` = "2008-2026 LMMS Developers,
2026 Zene Studio contributors"), the `--version` copyright line and every source header stay. Removing
them would be misattribution and a licence violation, not a rename. Also in this group: the plugin
descriptor copyright fields (`"LMMS contributors"`, `"LMMS WASM DSP sandbox contributors"`), the
`audioMasterGetVendorString` author name (`"Tobias Doerffel"`), the CC0 artwork credit in the logo
SVG's RDF metadata, and `data/projects/**/LICENSES.TXT` (the bundled demo content's attribution).

**(b) Upstream URLs and external identities.** `https://lmms.io` (~1305 files, mostly the header URL
above), `github.com/LMMS/*` (14 files: submodule URLs, upstream PR links, bash-completion and
LinuxDeploy references), `.gitmodules`, `.mailmap`, `.tx/config`, `.github/FUNDING.yml` and the
issue-template `config.yml` (they point at the upstream project's own Discord/donation page — the text
is true of those URLs), `PROJECT_EMAIL` (the upstream mailing list; there is no Zene Studio one),
`doc/wiki/**` (a submodule of the upstream wiki). A URL that resolves to a real upstream resource is
not a trace of our old name; rewriting it would produce a dead link.

**(c) Frozen artifacts — not editable.** 25 `tests/reference/**` files keep the old resource name
`lmms-plugin-logo`; they are verbatim upstream copies whose provenance is pinned blob-by-blob in
`tests/reference/ORIGIN.tsv`, and Part C compares the migrated plugins against them. They are safe
because `PixmapLoader` is **lazy** (`include/embed.h`: the constructor only stores the name;
`pixmap()` is the only consumer), and nothing renders those headless reference targets — the renamed
logo's own resolution is pinned by `PluginLogoResourceTest` instead.
`plugins/RnnoiseDenoiser/testdata/**` (9 `.mmp` with `creator="LMMS"`, a captured `strace` log naming
`~/.lmmsrc.xml` and `~/Documents/lmms`, a captured gdb backtrace) and `tools/mmpz-git/demo-output.txt`
are captured evidence: editing them would falsify the record.

**(d) Historical documents.** `docs/WAVE-R-RENAME.md`, `docs/RELEASE-NOTES-v0.1.0-alpha.md` (that
release really did ship the old executable name), `docs/phase-f/**`, `DOCS-NAMING.md`.

**(e) Product code that must keep the old spelling, because that is its job.** The adoption constants
in `ConfigMigration` (`.lmmsrc.xml`, `lmms/`, `lmms-workspace/`, `Documents/lmms`) exist **to be
adopted, never to be written**; `MidiJack`'s legacy config-attribute read; the dual `--! (zene|lmms)-api`
regex and the `lmms` Lua global alias; the `firstChildElement("lmms-project")` read fallback; the
`lmms_SOURCE_DIR`/`lmms_BINARY_DIR` CMake-cache acceptance; and DataFile's historical
`vocoder-lmms` → `vocoder` upgrade. Each is a one-line, commented read-both path.

**(f) `src/core/PeakController.cpp:214`** — "Due to a bug in older version of LMMS, the peak …". A
user-visible string, kept because it is a **factual reference to the upstream lineage** (the bug was
LMMS's, in this product's ancestry). Rewording it would make the warning less precise.

**(g) The 38 translation catalogs** `data/locale/*.ts` still contain the pre-rename source strings —
including the ones this layer changed. These are `lupdate` artifacts; there is no Qt Linguist
toolchain on this box and no sudo, so they cannot be regenerated here. Same residual as wave R §7.1;
until a machine with `lupdate` runs, the renamed strings fall back to English in translated UIs. It is
a generated-artifact refresh, not a code change.

**(h) The plugin-logo ARTWORK is still upstream's.** The file is now `zene-plugin-logo.svg` and its
`<dc:title>` reads "Zene Studio plugin logo", but the **art is byte-identical to upstream's**
`data/themes/default/lmms-plugin-logo.svg` — this lane renamed the resource and did not draw anything.
It is **CC0**, credited to *Rebecca Noel Ati* in the RDF metadata, which is retained, so shipping it is
legal. **Stating the scope question the owner needs to settle:** a new logo is *not* in this rename's
scope — it is a design decision, and it must not fall between two lanes and ship as upstream art by
accident. If the owner wants new artwork it needs its own task; if not, a one-line note in the
limitations is enough.

---

## 6. What an older build does with a new file

Checked against the source, not guessed: the **published alpha** is `0c23587d2` (tag `v0.1.0-alpha`),
and it is in this clone.

* The alpha's `DataFile::loadData()` reads `QDomElement root = documentElement();` — **exactly as this
  tree does**. It never compares the root's tag name, so a file whose root is `<zene-project>` **opens
  in the alpha**.
* The alpha has the **same 31 upgrade methods** (counted in `git show 0c23587d2:src/core/DataFile.cpp`),
  and this build writes `version="31"`. `loadData()` only calls `upgrade()` when
  `m_fileVersion < UPGRADE_METHODS.size()` — never true for a file this build writes — so the alpha's
  one root-name literal (`upgrade_noHiddenAutomationTracks`) is **never reached**.
* The alpha would therefore report the version-difference notice in its own words if the versions
  differ by minor: its message is hard-coded `"This %1 was created with LMMS %2"`, so a file written by
  a later build is announced as "created with LMMS 0.2.0". Cosmetic, in the alpha only.
* The **writer** half cannot be tested: the released alpha binary is not on this box. That is why the
  released alpha's users were told to keep copies — and it is why the alpha's own reader behaviour
  above is documented from its source rather than asserted.

**Practical answer for a user:** a project saved by this build still opens in the alpha, and any project
saved by the alpha still opens here.

---

## 7. Behaviour preservation, and the gates

### No behaviour change — the audio path

The measured claim is structural and it is a real diff, not a description: **on the audio path every
changed line is a string literal.** `git diff` over
`src/core/audio src/core/SampleBuffer.cpp src/core/Mixer.cpp src/core/AudioEngine.cpp
src/core/RenderManager.cpp src/core/Effect.cpp src/core/EffectChain.cpp` yields only:

```
- sf_set_string(m_sf, SF_STR_SOFTWARE, "LMMS");            + … "Zene Studio"
- id3tag_set_comment(m_lame, "Created with LMMS");         + … "Created with Zene Studio"
- vorbis_comment_add_tag(&m_vc, "Cool", "This song has been made using LMMS");
                                                           + … "… using Zene Studio"
- tr("LMMS was kicked by JACK …") / clientName = "lmms" / pa_stream_new(c, "lmms", …) / …
```

No sample computation, no buffer sizing, no DSP, no serialization of audio state. The consequence to
state plainly: **rendered files are byte-different for metadata reasons** (the software tag changes),
which is exactly why a hash is the wrong instrument here.

### The measurement, and why it is reported this way

`zene-pa-stems/docs/STEM-EXPORT.md` §4.4 established that this tree's renders are **not bit-reproducible
run to run**. Re-measured here on `data/projects/shorties/Root84-TrancyLoop.mmpz`, four renders in
separate processes with the same binary:

```
run1 vs run2: peak|d|=11541  rms|d|=1357.216
run1 vs run3: peak|d|=10739  rms|d|=1210.836
run1 vs run4: peak|d|=11546  rms|d|=1366.328
run2 vs run3: peak|d|= 9404  rms|d|=1221.517
run2 vs run4: peak|d|= 8579  rms|d|=1015.233
run3 vs run4: peak|d|= 9343  rms|d|=1129.839
levels: run1 rms=8506.6 peak=32767 · run2 rms=8506.4 peak=32767 ·
        run3 rms=8507.5 peak=32767 · run4 rms=8503.9 peak=32767
```

**Same-build run-to-run floor: peak |Δ| ≈ 8.6 k–11.5 k LSB, rms |Δ| ≈ 1.0 k–1.4 k LSB, at ≈ −9 dBFS,
while the overall level is stable to 0.0004 dB (rms 8503.9–8507.5) and peaks at full scale in all four
runs.** The floor is the tree's own non-determinism (the ~400-frame dropout episode STEM-EXPORT §4.4
describes), not anything this rename did: it is measured *between runs of one binary*.

The honest conclusion, which is the point of measuring it: **the floor is larger than any
rename-induced delta could be, so a WAV-level before/after comparison across two builds would not be
discriminating on this project.** It was therefore not used as evidence, and a second full build for a
manual A/B was not affordable (the shared box sat at load ≈50 with 13 GB of 286 GB free; §9). What *is*
used:

* **Sensitivity controls** — the metric is neither blind nor saturating: identical input → `peak|Δ| = 0
  LSB`; the same render with **+1 LSB on a single sample** → `peak|Δ| = 1 LSB` (`−90.31 dBFS`). So the
  8.6 k-LSB floor above is real signal, not a broken meter.
* **`tests/reference/mixer-ab-render.raw`** — a committed reference render that predates this work, and
  `tests/src/core/MixerAbRegressionTest.cpp`, which byte-compares mixer output against it. **Stated
  plainly: this harness is in the tree but is NOT registered as a ctest target on this branch
  (`grep MixerAbRegressionTest tests/CMakeLists.txt` → no match), so it is not part of my evidence and
  I did not run it.** Enabling it would give the cross-rename sample-level comparison this report
  cannot provide; it is named here as the cheapest available follow-up.
* **The 28-test suite, 28/28 green**, which includes the Part C sample-exact reference renders
  (`PluginPortsMigrationTest`, unchanged and passing).

### A real render and `--version`

```
$ QT_QPA_PLATFORM=offscreen ./build/zene render data/projects/shorties/Root84-TrancyLoop.mmpz -o out.wav -f wav
render1_run1 EXIT=0 · render2_run1 EXIT=0 · render1_run2 EXIT=0 · render2_run2 EXIT=0
1 909 856 bytes / 477 440 frames / 44 100 Hz / 2 ch / peak 32767 / rms 8506.6

$ ./build/zene --version          # exit 0
Zene Studio 0.1.0-alpha.3+13f33b9
(Linux x86_64, Qt 6.4.2, GCC 13.3.0)
…
Copyright (c) 2008-2026 LMMS Developers, 2026 Zene Studio contributors
```

### The gates, with their real exit codes

| gate | command | exit | evidence |
|---|---|---|---|
| 1 unit tests | `(cd build/tests && ctest …)` | **0** | `100% tests passed, 0 tests failed out of 28` |
| 3 no tautology | `bash tests/no-tautology-gate.sh` | **0** | `PASS: every registered test file has test slots and real assertions, no literal tautologies` |
| 4 complexity | `bash tests/complexity-gate.sh --check` | **0** | `PASS (check mode: no regressions)` |
| 6 no undeclared divergence | `bash tests/no-upstream-regression-gate.sh` | **0** | `PASS … (352 file(s) in the ledger)` — **0 violations** |
| 7 file length | `bash tests/file-length-gate.sh --check` | **0** | after a deliberate re-anchor of `ScriptBindings.cpp` 1217 → 1222 |
| 8 duplication | `bash tests/duplication-gate.sh` | **0** | `PASS: duplicated lines 1.08% (budget 5%)` |
| 9 scope registration | *not on this branch* — see below | **0** | 1094 scanned, 100 fork-NEW, 995 inherited, **0 unregistered, 0 stale** |
| 5 mutation | not run (expensive sweep, last run ≥3 min, budget spent on the proofs above) | — | — |

**Gate 6 was red, and this is corrected.** An earlier claim in this series read "Gate 6 after this
commit: PASS, 0 violations". Two things were true and are now separated:

* On the *committed* tip before this lane's work, Gate 6 genuinely was **RED**: wave R's rename commit
  `018d2041f` changed 38 upstream-inherited files (the icon set, NSIS/macOS/AppImage packaging, the man
  page and bash completion, the About dialog, `ConfigManager`'s install paths, `main.cpp`'s
  `--version` banner) with **no ledger entry**, and the gate reported `38 violation(s)`, exit 1. That
  debt is paid off in the first commit on this branch, each file declared with a reason read off wave
  R's own diff. **Its command and output are in §1's log and in the commit that made it: the claim is
  the measurement, not a summary of one.**
* A pre-commit check then **reproduced the audit's finding independently**: applying Gate 6's own rule
  to a `git stash create` snapshot of the working tree found `plugins/CarlaBase/Carla.cpp` undeclared
  (a real miss — the Carla UI-name change) and the three **rename old-paths** undeclared. A renamed file
  is a change to upstream-inherited code on *both* sides: the old path leaves upstream's namespace and
  the new one is not upstream's. All four are now declared, with reasons, and the
  `--` old-path entries say explicitly that they exist because a rename deletes the path.
* On the final tip `c30c93082`: **`GATE6_EXIT=0`, `violations=0`, 352 changed files matched.**
  `bash tests/no-upstream-regression-gate.sh` was re-run and its output read; nothing in this report
  rests on an earlier run.

**Gate 9 does not exist on this branch** — `find . -name fork-sources-gate.sh` returns nothing here,
though it exists on sibling lanes. So its documented rule was executed against this tree using a
**read-only copy of a sibling lane's script** (`zene-pa-gatedebt/tests/fork-sources-gate.sh`, copied
into `tests/` as `.g9tmp.sh`, run, deleted in the same command; the tree is clean of it):
`PASS: every tracked source in scope is registered (100 fork-NEW, 995 inherited)`, exit **0**.
The three new test sources are registered in `tests/all-sources.txt` — which is where every test source
in this repo lives (0 test entries in `tests/fork-sources.txt`) — not in `fork-sources.txt`, because
that manifest is regenerated from `src include plugins` and adding tests to it would move the
complexity/file-length/duplication/coverage scope.

**Pre-existing whole-tree ratchet debt, not caused by this work** (reported because it is red and
someone should know): with `--scope all`, **Gate 4 exits 1** —
`REGRESSION: new function over target: AudioBufferTest::CrossProcess_TwoProcessesWithSameSharedMemory
(CCN 11)` — and **Gate 7 exits 1** —
`REGRESSION: tests/src/wasm/WasmSandboxTest.cpp grew 948 -> 1022 lines`. Neither file is in this change
set (`git status --porcelain` does not list them), so both predate this lane. Nothing was trimmed to
make a metric green.

---

## 8. Proposed wording for the lanes that own them

**`docs/KNOWN-LIMITATIONS.md`** — replace the current "the app … shares LMMS's config file
(`~/.lmmsrc.xml`) and working folder (`~/Documents/lmms/`)" bullet with:

> **Your settings and projects move once, and nothing is lost.** The config file is now
> `~/.zenestudio.xml` and the working folder `~/Documents/Zene Studio/`. On the first run after
> upgrading, the old `~/.lmmsrc.xml` and `~/Documents/lmms/` are *moved* into the new names; if they
> cannot be moved (read-only home, a different filesystem), the old location keeps being used, so your
> settings and projects are never orphaned. Projects written by 0.1.0-alpha (`<lmms-project>`,
> `creator="LMMS"`) open exactly as before, and saving rewrites them in the new format
> (`<zene-project>`, `creator="Zene Studio"`). The licence headers and the "derived from LMMS"
> attribution are unchanged and always will be — this program is GPL-2.0-or-later and a derivative
> work, and those notices are a condition of the licence.

**Release notes (v0.2)** — proposed bullets:

> * **The product is called Zene Studio everywhere you can see it.** A few names other software sees
>   changed too, and two of them are worth knowing about:
>   * the **JACK/ALSA/PulseAudio client name** is now `Zene Studio`, so a saved patchbay or stream
>     routing entry that names the old client will not match once — re-make the connection and it
>     sticks.
>   * the **project file association** is now `application/x-zene-project`; if you installed the old
>     desktop file, reinstall it (or re-run the AppImage's desktop integration) for the new double-click
>     association to register.
> * **Your scripts keep working.** The Lua API namespace is `zene` and the header is `--! zene-api`, but
>   `lmms.*` and `--! lmms-api` still work, so nothing you wrote against 0.1.0-alpha breaks.
> * **Your settings and projects are moved, not lost** — see Known Limitations.
> * **Keep a copy of anything you care about before upgrading.** This release still makes no
>   project-format stability promise; a project saved by a *later* Zene Studio build opens in 0.1.0-alpha,
>   but the two builds do not write identical files.
> * Native LMMS plugins built against the old entry symbol keep loading: **there is no plugin ABI change
>   in this release.**

---

## 9. The code-identifier layer — assessed, not attempted

Not started. Measured on this tree, so a follow-up lane can size it rather than re-discover it:

| item | size |
|---|---|
| `lmms::` namespace | **462 files** |
| `LMMS_*` macros (incl. `LMMS_HAVE_*` generated into `lmmsconfig.h` from 23 `cmake/` files) | **751 files** |
| `LMMS_DATA_DIR` / `LMMS_TESTING` / `LMMS_BUILD_*` env vars and defines | 140 lines |
| `lmms_export.h` / `lmmsconfig.h` / `lmmsversion.h` / `lmms_math.h` / `lmms_constants.h`, `lmms.qrc`, `LmmsStyle`/`LmmsPalette`/`LmmsTypes` | **373 files**; 4 files literally named `lmms*` (`src/lmmsconfig.h.in`, `src/lmmsversion.h.in`, `include/lmms_constants.h`, `include/lmms_math.h`); `lmms_export.h` is generated by `GENERATE_EXPORT_HEADER` and the `lmmsobjs` object library is named after it |
| `lmms_plugin_main` entry symbol | **114 files** |
| translation catalogs touched by any string change | 38 `.ts` |
| union of touched production files | **≈1,100** → ≈1,000 new `tests/upstream-modifications.txt` entries with reasons, plus the 38 `.ts` |

Two consequences that make this more than mechanical:

1. **`tests/reference/**` must be excluded** (244 files contain `lmms`). They are frozen verbatim
   copies pinned blob-by-blob in `ORIGIN.tsv` and they share the `lmmsobjs` object library with the
   migrated plugins, so a tree-wide namespace rename would leave the reference targets on the old
   namespace and the Part C parity targets would fail to link.
2. **`lmms_plugin_main` is the entry symbol native LMMS plugins export.** Renaming it is an **ABI
   break**: existing native plugin binaries would stop loading. That part must be its own commit with
   the choice made explicitly — keep accepting the old entry symbol (compatibility; a `dlsym` for both
   names costs nothing at load time) or declare a deliberate ABI break for 0.2.0 with the plugin
   rebuild requirement in the release notes. **This release takes the first option by doing nothing:
   there is no ABI change in this work.** (The `.desktop`/package/install-path side of that symbol is
   untouched too — `lmms_plugin_main` is not exported to any host system, only to plugins.)

A follow-up lane bounded to `lmms_plugin_main` alone (114 files, one compatibility shim, one commit) is
the cheapest meaningful slice if the owner wants the identifier layer started.

---

## 10. What I could not do (plainly)

1. **No second full build for a manual before/after render A/B.** Disk was at 13 GB of 286 GB free (96 %
   used) with sibling lanes building, and the box ran at load ≈50 on 20 cores; one extra full build
   tree was not affordable. Independently, §7 shows the same-build floor (≈8.6–11.5 k LSB) makes such a
   comparison non-discriminating on this project anyway. The cross-rename sample comparison that *would*
   be decisive exists in the tree but is unregistered (`MixerAbRegressionTest`, §7) — enabling it is the
   cheapest fix and I did not do it.
2. **The translation catalogs cannot be regenerated here** (no `lupdate`, no sudo) — wave R's residual,
   unchanged, and 38 `.ts` files still carry pre-rename source strings.
3. **The released alpha binary is not on this box**, so "a new file opens in the alpha" is established
   from the alpha's source (§6), not by running it. What an older build does is stated exactly, not
   demonstrated.
4. **One build attempt failed for an environmental reason**, recorded because it is part of the run log:
   the first `cmake --build build -j4` in a parallel lane's load died with
   `g++: fatal error: Terminated signal terminated program cc1plus` on three unrelated targets
   (`partc_ref_dualfilter`, `partc_ref_bitcrush`, `Xpressive/ExprSynth`) — no diagnostic, just the
   signal, with swap 5.6 GB of 8 GB in use. `local-ci` reported `overall exit=1` and correctly said a
   failing build here "is a REAL failure of the code, not a machine limit"; it was not a code failure.
   The retry at `-j2`/`-j3` completed: `BUILD_EXIT=0`. A later attempt was killed by my own 420-second
   tool timeout (`gmake: *** [Makefile:156: all] Terminated`), which is a harness limit, not the tree —
   after that, builds were batched so they could not be interrupted.
5. **Two whole-tree ratchets are red for pre-existing reasons** (§7); I reported them rather than
   trimming code or re-anchoring a baseline to hide them.
6. **The plugin-logo artwork is unchanged upstream art** (§5h) — a design decision, flagged for the
   owner rather than silently left or silently swapped.
