# Brand placeholders — upstream LMMS identity artwork audit and replacement

**Branch:** `post-alpha/brand-placeholders` (based on `post-alpha/rename-complete`)
**Date:** 2026-09-12
**Scope:** audit every shipped identity artwork file for upstream LMMS provenance, replace each
one with a hand-authored, licence-free placeholder, and prove the replacements load.
**Not in scope:** choosing the real product mark. That is the owner's decision; this change is a
placeholder layer that unblocks it.

---

## 0. Verdict

**41 shipped identity images were still upstream LMMS artwork — every one audited.** 39 of the 41
compared **byte-for-byte equal to `origin/master`** (24 at the same path, 15 as git-detected
renames); the other 2 — the plugin logo and the project-file SVG — kept upstream's **drawing data**
and differed only in renamed metadata text. **Zero** of the 41 had already been replaced by anyone.
All 41 are now hand-authored placeholders, and 0 files remain byte-identical to upstream.

The audit's own totals (`provenance-audit.txt`, Part C): `identical_same_path 24`,
`identical_renamed 15`, `art_only 2`, **`differs 0`**.

The headline item: `data/themes/default/zene-plugin-logo.svg`. The rename lane renamed the resource
and its `<dc:title>` and said so (its own report, §(h)); **the art itself was never redrawn** — the
`<path d>` drawing data still hashed `45346519bb38606eeb1f51d1f153537f`, identical to
`origin/master`'s copy.

> **Two facts in the task brief contradict the tree. Both are reported rather than quietly
> adjusted; the work continued on the tree's version.** See §8.

---

## 1. Provenance audit

Method — every row is command output, not impression. For each candidate:

```bash
# (a) same path
git show origin/master:<path> | cmp - <path>; echo $?
# (b) for renamed files, the pre-rename upstream path, from git's own rename map
git diff --name-status -M origin/master..HEAD | grep '^R'
# (c) what the file says about itself
grep -o '<dc:[a-z]*>[^<]*' <file>.svg        # SVG RDF metadata
python3 -c "...PNG tEXt/iTXt/XMP chunks..."  # raster metadata
```

Full raw logs: `tests/evidence/brand-placeholders/provenance-audit.txt`.

| # | Shipped path | Format / size | Byte-identical to upstream? | Upstream path | Own embedded metadata | Licence-bearing? |
|---|---|---|---|---|---|---|
| 1 | `data/themes/default/zene-plugin-logo.svg` | SVG 48×48, 1222 B | **drawing data identical** (`<path d>` md5 `45346519…`); file differs from upstream only in `<dc:title>` | `data/themes/default/lmms-plugin-logo.svg` | `<dc:title>Zene Studio plugin logo` (was `LMMS plugin logo`); `<dc:creator>Rebecca Noel Ati`; `cc:License` **CC0 1.0** | **YES — CC0, credited** |
| 2–14 | `cmake/linux/icons/{16x16,16x16@2,24x24,24x24@2,32x32,32x32@2,48x48,48x48@2,64x64,64x64@2,128x128,128x128@2,256x256}/apps/zene.png` (13 files) | PNG, 16–256 px | **IDENTICAL** (13/13, `cmp` exit 0) | same dirs, `lmms.png` (git rename R100) | 256 px copy carries `iTXt` XMP: `xmp:CreatorTool = www.inkscape.org` | no in-file licence; upstream project licence |
| 15 | `cmake/linux/icons/scalable/apps/zene.svg` | SVG 64×64 | **IDENTICAL** (git R100) | `…/scalable/apps/lmms.svg` | Inkscape/Sodipodi tree incl. `sodipodi:docname="lmms.svg"`, `inkscape:export-filename="64x64@2.png"` | no |
| 16–28 | `cmake/linux/icons/{same 13 dirs}/mimetypes/application-x-lmms-project.png` (13 files) | PNG, 16–256 px | **IDENTICAL** (13/13) | *same path* (never renamed) | none | no |
| 29 | `cmake/linux/icons/scalable/mimetypes/application-x-zene-project.svg` | SVG 64×64 | differs from upstream **only** in `sodipodi:docname` (git R099) | `…/mimetypes/application-x-lmms-project.svg` | `inkscape:export-filename="/home/umcaruje/d/Dropbox/lmms icons/project-256x256.png"` — the upstream author's own export path | no |
| 30 | `cmake/nsis/assets/Logo.png` | PNG 600×600 | **IDENTICAL** | same path | `tEXt Software = www.inkscape.org` | no |
| 31 | `cmake/nsis/assets/SmallLogo.png` | PNG 192×192 | **IDENTICAL** | same path | `tEXt Software = www.inkscape.org` | no |
| 32 | `cmake/nsis/icon.ico` | ICO, 8 entries (16/24/32/48/64/96/128/256), 32 bpp | **IDENTICAL** | same path | container: none | no |
| 33 | `cmake/nsis/project.ico` | ICO, same 8 entries | **IDENTICAL** | same path | none | no |
| 34 | `cmake/apple/icon.icns` | ICNS, chunks `info,ic09,ic05,ic08,ic04,ic07` | **IDENTICAL** | same path | container: none | no |
| 35 | `cmake/apple/project.icns` | ICNS, same chunks | **IDENTICAL** | same path | none | no |
| 36 | `cmake/apple/background.png` | PNG 705×400 | **IDENTICAL** | same path | none | no |
| 37 | `cmake/apple/background@2x.png` | PNG 1410×800 | **IDENTICAL** | same path | none | no |
| 38 | `data/themes/default/splash.png` | PNG 681×573 RGB | **IDENTICAL** | same path | **carries the LMMS wordmark and logo** (visually verified); used by `src/gui/GuiApplication.cpp:124` `embed::getIconPixmap("splash")` | no |
| 39 | `data/backgrounds/vinnie.png` | PNG 384×383 greyscale | **IDENTICAL** | same path | **LMMS mascot (the note character) plus the "LMMS" wordmark** (visually verified) | no |
| 40 | `data/backgrounds/newbg.png` | PNG 420×215 | **IDENTICAL** | same path | **the LMMS mark as a repeated watermark** (visually verified; the wordmark OCR'd loosely as "LENOS") | no |
| 41 | `data/backgrounds/zene_tile.png` | PNG 64×64, 1-bit indexed | **IDENTICAL** | `data/backgrounds/lmms_tile.png` (git R100) | none | no |

**Total: 41 files.** 39 byte-identical to upstream; 2 identical in drawing data and renamed in
metadata only (#1, #29).

### 1a. Scope boundary — what was NOT treated as identity artwork

`data/themes/default` and `data/themes/classic` hold **571 images**, of which the great majority are
generic interface glyphs (`play.png`, `knob01.png`, `note_quarter.png`, …). Those are upstream
provenance, but they are **not identity artwork** — they carry no mark, no wordmark and no brand
provenance metadata. Replacing them is a different (and much larger) decision, and doing it here
would be a mass edit of the theme for no branding reason. **Explicitly not replaced.**

The line drawn: a file is identity artwork if it is *the product's mark or a rendition of it*
(logo, app/project-file icon, installer & bundle art, splash, desktop watermark), or if its own
metadata says whose brand it is. Test files that merely reference a name are untouched.

### 1b. Where the credited originals live now

**Nothing was deleted and no attribution was removed.** The upstream artwork is still reachable:

```bash
git show origin/master:data/themes/default/lmms-plugin-logo.svg          # CC0, Rebecca Noel Ati
git show origin/master:cmake/linux/icons/256x256/apps/lmms.png
git show origin/master:cmake/nsis/assets/Logo.png
git show origin/master:data/themes/default/splash.png
```

The plugin logo's CC0 `dc:creator` is preserved verbatim in the ledger entry and in git history; the
only thing that changed is which asset the application *ships*. If the product ever wants to keep
the CC0 art, it is one `git show` away. **Where the credit must not be lost:** `NOTICES`/
`LICENSE.txt` are untouched and still cover the upstream-derived work as a whole. No copyright
notice was edited by this change.

The frozen upstream copies under `tests/reference/**` (25 files, blob-pinned in
`tests/reference/ORIGIN.tsv`) are **untouched** — they are deliberate verbatim copies.

---

## 2. What the placeholders are

Three hand-authored SVGs, and every raster derived from them by one script.

| Placeholder source (hand-authored) | What it is | Derived rasters |
|---|---|---|
| `data/themes/default/zene-plugin-logo.svg` | a plain eighth note **punched out of a white plate** — plate retained so the glyph stays legible on the plugin dialog's dark background, exactly as upstream's white-plate mark was | (SVG only) |
| `cmake/linux/icons/scalable/apps/zene.svg` | a plain eighth note on a neutral **slate** rounded plate | 13 app-icon PNGs, NSIS `Logo.png`/`SmallLogo.png`/`icon.ico`, `cmake/apple/icon.icns`, splash, backgrounds |
| `cmake/linux/icons/scalable/mimetypes/application-x-zene-project.svg` | a neutral **document sheet with a folded corner** carrying the same note | 13 mimetype PNGs, NSIS `project.ico`, `cmake/apple/project.icns` |

**Why hand-authored matters here:** nothing is traced, sampled, downloaded or vendored, so the
placeholder carries **no third-party licence and no attribution obligation** — the licensing
question is removed rather than answered. The three files are small enough to read in full.

### 2a. Sizing and legibility

The shapes are deliberately bold because they are drawn small:

* plugin logo — shipped at **48×48** (`embed::loadSvgPixmap` reads `defaultSize()`; the SVG declares
  `width="48" height="48"`), drawn so the note stays a legible silhouette at the plugin-browser
  size;
* app icon — drawn at **64×64** and used from **16 px** up; the plate is a single flat shape with no
  fine detail to alias away;
* project icon — the page's fold and the note are the only two features, both large.

Geometry is **cubic Bézier only** (plus `H`/`V`), which is the subset every renderer here draws. The
plugin logo's punch uses `fill-rule="evenodd"`; that was verified against the product's renderer, not
assumed — see §3.

### 2b. Marked as placeholders, in the artefacts and in the repo

* **SVG**: `<dc:title>` says `(placeholder)`, `<dc:description>` says
  `placeholder - pending the product mark`, `<dc:date>2026-09-12</dc:date>`, and `<dc:rights>` states
  it contains no third-party artwork. A leading comment says the same in prose.
* **Raster**: every generated PNG carries a `tEXt` `Description` chunk —
  `placeholder - pending the product mark (Zene Studio placeholder artwork, 2026-09-12)`. Verified
  present in all 34 PNGs.
* **Splash, DMG background and desktop backgrounds** carry the caption text
  *"placeholder artwork - pending the product mark"* rendered into the image, so they cannot be
  mistaken for finished brand art.
* **This file** is the repo-level record.

### 2c. One source of truth, and the renderer argument

`tools/brand/rasterise-placeholders.py` renders the three SVGs through **Qt's `QSvgRenderer`** — the
same engine `src/gui/embed.cpp` uses — at the upstream files' own pixel sizes. A pixel that renders
in the generator renders in the product.

**ImageMagick was tried first and rejected on evidence.** Its bundled `rsvg-convert` delegate is
missing on this host, so it falls back to the internal MSVG renderer, which **silently drops
`fill-rule="evenodd"` and mis-handles arc commands**: a test plate-plus-punched-hole rendered with
the hole *filled*. Rasterising through it would have shipped a solid white square where the mark
should be, and a green build would not have caught it. (Recorded because it is a trap: MSVG's
failure is silent.)

The generator is authoring-time only. The committed rasters have no build-time dependency on PySide6
or on the script — re-running it is how you *change* the artwork, not how you build it.

---

## 3. Proof that the placeholders load

### 3a. The rename lane's resource test — green, and the red control

The failure mode is silent: a name that does not resolve is a **1×1 transparent pixmap plus a
`qWarning`**, no exception and no non-zero exit, so a green build proves nothing. The rename lane
left `tests/src/core/PluginLogoResourceTest.cpp` for exactly this. This change replaces the file that
test covers, so it was run **in both directions**.

```bash
# green — resource present
cd build/tests && QT_QPA_PLATFORM=offscreen ./PluginLogoResourceTest

# red control — move the placeholder out of the way, so the assertion is not vacuous
mv data/themes/default/zene-plugin-logo.svg .g-red-control.svg.bak   # restored immediately
```

Observed, exit codes unpiped (full text: `tests/evidence/brand-placeholders/plugin-logo-test-red-green.txt`):

```
GREEN      QINFO : plugin logo 'zene-plugin-logo' resolved to 48 x 48 px from the artwork search path
           Totals: 4 passed, 0 failed, 0 skipped, 0 blacklisted, 15ms          GREEN_EXIT=0

RED        FAIL!  : pluginLogoLoads() 'logo.width() > 1 && logo.height() > 1' returned FALSE.
           (the plugin logo is the 1x1 fallback pixmap: the resource did not resolve)
           Totals: 3 passed, 1 failed, 0 skipped, 0 blacklisted, 7ms           RED_EXIT=1

GREEN      restored, re-run: 4 passed, 0 failed                            GREEN_AGAIN_EXIT=0
```

The green line is the proof that matters: the **placeholder** resolves through the product's own
artwork search path to a real **48×48** pixmap, not the fallback. The red control shows the
assertion can fail, so the green is not vacuous.

The **full suite** ran from `build/tests` via `tools/local-ci.sh --no-build`:

```
ctest EXIT=0         100% tests passed, 0 tests failed out of 28
local-ci: overall exit=0
```

28 tests, not 0 — a `0 tests` result is treated as an error, never a pass. Full log:
`tests/evidence/brand-placeholders/local-ci.txt`. (`local-ci.log` is the earlier full
configure+build run, which reached `[100%] Built target PluginPortsMigrationTest` with **0
`error:` lines** in `build/build.log`.)

### 3b. Resource-resolution sweep

`tests/brand-resource-sweep.py` walks **every** `PixmapLoader("…")`, `PluginPixmapLoader("…")` and
`getIconPixmap("…")` name in first-party code and checks a file exists behind it, resolving the way
the product resolves:

* a plugin's own name is prefixed with `PLUGIN_NAME` (`include/embed.h:104`) and embedded as
  `artwork/<PLUGIN_NAME>/…` by `cmake/modules/BuildPlugin.cmake:30`, and `:/artwork` is on the search
  path (`src/gui/GuiApplication.cpp:108`) — so the name resolves **from the plugin's own directory**;
* otherwise it resolves against the theme search path (`GuiApplication.cpp:106–108`).

Result on this tree (full text, with the negative control, in
`tests/evidence/brand-placeholders/resource-sweep.txt`):

```
first-party resource names referenced: 498
call sites scanned:                    989
call sites resolved:                   846
UNRESOLVED call sites:                 3  (3 pre-existing baseline, 0 NEW)

  pre-existing (pinned in KNOWN_PREEXISTING; not this change's):
    arp_down_on  <- src/gui/LfoControllerDialog.cpp
    arp_up_on    <- src/gui/LfoControllerDialog.cpp
    logo         <- plugins/GranularPitchShifter/GranularPitchShifterControlDialog.cpp
EXIT=0
```

**`zene-plugin-logo` resolves to `data/themes/default/zene-plugin-logo.svg`** — the placeholder is
behind the renamed key.

The sweep is a **ratchet**, not a tally: it exits 0 while the unresolved set is exactly the pinned
pre-existing baseline and fails on anything new. It was shown to go **red** — the negative control
hides the plugin logo and the sweep reports **28 NEW unresolved call sites**, exit 1:

```
--- NEGATIVE CONTROL: hide the plugin logo so a NEW name stops resolving ---
UNRESOLVED call sites:  31  (3 pre-existing baseline, 28 NEW)
  NEW - these fail the sweep:
    zene-plugin-logo  <- plugins/Amplifier/Amplifier.cpp
    zene-plugin-logo  <- plugins/BassBooster/BassBooster.cpp
    … 26 more, one per plugin …
EXIT=1
```

That is also an independent confirmation of the live call-site count (**28 under `plugins/`** plus
one test fixture — see §8). `--strict` makes the baseline itself fatal; it exits 1 here.

**All three unresolved names are pre-existing, and none is this change's:**

| Name | Referenced from | Pre-existing? | Evidence |
|---|---|---|---|
| `arp_down_on` | `src/gui/LfoControllerDialog.cpp` | **yes** | file not in this change set (`git diff --name-only <base> HEAD -- …` → 0 lines); same two names the rename lane found |
| `arp_up_on` | `src/gui/LfoControllerDialog.cpp` | **yes** | as above |
| `logo` | `plugins/GranularPitchShifter/GranularPitchShifterControlDialog.cpp:146` | **yes — and missed by the rename lane's sweep** | the plugin directory genuinely contains no `logo.*` (only `artwork.png`, `help_*`, `prefilter_*`); the file was last touched by upstream `478f5345d` and is not in this change set |

`logo` is a real upstream defect: that plugin's window icon has always been the 1×1 fallback. It is
**reported, not fixed** — adding art for it is a new asset, not a placeholder, and it is not this
task's decision. It is worth its own task.

**Sweep-vs-rename-lane difference, disclosed:** the rename lane reported 2 unresolved; this sweep
reports 3. The difference is `logo`. Their model resolves a name once across the tree, so
GranularPitchShifter's missing `logo.png` was masked by the 20-odd plugins that *do* ship one. This
sweep resolves **per call site**, which is why it catches it. The 498-name total matches theirs
exactly.

### 3c. Container and dimension proofs

`tests/evidence/brand-placeholders/verify-placeholders.txt` (`tests/evidence/brand-placeholders/verify-placeholders.py`,
exit 0) checks all 38 rasters:

* **dimensions equal the pre-change files exactly** — 34/34 PNGs match the inventory snapshotted
  *before* the change (`pre-change-dims.txt`);
* **placeholder metadata present** — 34/34 PNGs carry the `tEXt` Description;
* **ICO containers are structurally upstream's** — 8 entries, 32 bpp, in the same order, and the
  entry byte counts are **identical to upstream's own** (`1128, 2440, 4264, 9640, 16936, 38056,
  67624, 270376`). That is the check that the handmade container layout is right;
* **ICNS containers walk cleanly** — magic, declared length == actual length, every chunk length
  valid, every payload a PNG whose IHDR matches its declared slot;
* **0 of 41 files remain byte-identical to upstream**, and **0 are unchanged from the pre-change
  commit**.

---

## 4. Registration and gates

### 4a. Registration

* `tests/upstream-modifications.txt` — **24 new declarations** (13 mimetype PNGs, 4 NSIS, 4 Apple,
  3 backgrounds, the splash) plus **14 more for the old `*/apps/lmms.png` paths**, and **17 stale
  reasons corrected** (the app-icon entries said *"pixels unchanged"*, which stopped being true).
  Every reason is read off the diff and names this file.
* `tests/fork-sources.txt` — `tools/brand/rasterise-placeholders.py` added next to the other
  `tools/` entry.
* Staging was explicit paths only (`git add -- <44 paths>`), every path existence-checked before
  staging, and `git diff --cached --name-only` was checked before the commit. No `git add -A`.

**Why the 14 extra entries exist** — worth recording, because it is a trap for anyone who replaces
art later: git pairs `lmms.png → zene.png` as a rename only while the bytes are similar. Replacing
the art drops the similarity below git's threshold, so `git diff --name-only` stops pairing them and
the **old path surfaces as a deletion**. Gate 6 then fails on a path nobody touched by name. Declare
both halves of any art rename whose content changes.

### 4b. Gate exit codes — measured, unpiped

| Gate | Command | Exit | Note |
|---|---|---|---|
| `fork-sources-gate.sh` | `bash tests/fork-sources-gate.sh` | **127** | **the script does not exist on this branch** — `fork-sources-gate.sh` is absent from `tests/`. Reported as 127, not claimed as a pass. |
| Gate 6 — no upstream regression | `bash tests/no-upstream-regression-gate.sh` | **0** | `PASS: every change to upstream-inherited code since 01148947ea… is declared (393 file(s) in the ledger)` |
| `run-all-gates.sh` | `bash tests/run-all-gates.sh` | **0** | `RESULT: PASS — every executed gate passed` (Gate 2 SKIP) |
| Gate 9 | — | **127 / not on this branch** | see below |

`run-all-gates.sh` summary, as measured (`tests/evidence/brand-placeholders/run-all-gates.txt`):

```
gate   name                     result
1      ctest                    PASS      2  coverage    SKIP
3      no-tautology             PASS      4  complexity  PASS
5      mutation                 PASS      6  upstream-regression  PASS
7      file-length              PASS      8  duplication PASS
RESULT: PASS — every executed gate passed
```

**Two gates failed on my first pass and were fixed, not declared away.** That history is worth
recording because both failures were self-inflicted and neither was visible from the build:

| Gate | What failed | Fix |
|---|---|---|
| 4 — per-method complexity | `tests/brand-resource-sweep.py: main` measured **CCN 12** against a target of ≤ 10 — `REGRESSION: new function over target` | split into `scan_call_sites` / `resolve_call_sites` / `print_report`; the same was done to `provenance-audit.py: main` (CCN 14). `lizard -C 10` now reports nothing over target |
| 6 — no upstream regression | the evidence directory at `docs/evidence/**` matched **none** of the gate's allowlist patterns (`.sh/.py/.txt/.log/.png` are not `tests/**`, not build config, not `*.md`), so **29 evidence files read as undeclared divergence** | moved to `tests/evidence/brand-placeholders/`, which `tests/**` explicitly allows. Declaring non-product evidence in the shared divergence ledger would have been noise |

Both are green on the committed tip. The lesson for the next lane: `tests/**` is the only tree this
repo's Gate 6 treats as free, and a `.py` helper anywhere in scope is measured by the complexity
ratchet.

**Gate 9 does not exist on this branch.** `post-alpha/gate-debt` is **not** an ancestor of HEAD
(`git merge-base --is-ancestor post-alpha/gate-debt HEAD` → non-zero), and `find . -name
fork-sources-gate.sh` returns nothing. `docs/RENAME-COMPLETE.md:486` already records the same. So:
**exit 127, not a pass.** Following the rename lane's precedent the sibling copy was run read-only:

```bash
cp ../zene-pa-gatedebt/tests/fork-sources-gate.sh tests/.g9tmp.sh   # copied, run, deleted
bash tests/.g9tmp.sh
```

…with the exit code recorded in `tests/evidence/brand-placeholders/gate-9-sibling-copy.txt`. The tree
is left clean of `.g9tmp.sh`.

**Coverage (Gate 2) was skipped** — `run-all-gates.sh` skips it without `--with-coverage`, and a full
coverage build is not what this change can move (the change is image data plus one authoring tool).

---

## 5. What is NOT done

1. **The real product mark.** This is a placeholder layer. Choosing and drawing the mark is the
   owner's decision and is deliberately not pre-empted here. Swapping it in means editing the three
   SVGs and re-running `tools/brand/rasterise-placeholders.py`.
2. **`docs/KNOWN-LIMITATIONS.md` and the release notes were not edited** — they are the
   orchestrator's to apply centrally. Proposed wording is in §6.
3. **The 283 theme UI icons were not replaced.** They are upstream provenance but not identity
   artwork (§1a). If the owner wants a fully clean theme, that is a separate, much larger task.
4. **macOS bundle rendering is UNVERIFIED.** `cmake/apple/*.icns` were rebuilt from PNG chunks
   (`ic11/ic12/ic07/ic13/ic09`); upstream's legacy raw `ic04/ic05/ic06` chunks were dropped rather
   than faked. The containers were validated structurally on this host, but **no macOS was available
   to render them.** What would settle it: opening the `.icns` in Finder/Preview on macOS, or a CI
   job that runs `iconutil -c iconset`. Until then treat the Mac icon as container-valid,
   render-unverified.
5. **Windows installer rendering is UNVERIFIED in the same way** — the `.ico` entries are
   byte-count-identical to upstream's and structurally identical, but no Windows host was available.
6. **`logo` in `GranularPitchShifter`** (§3b) is a pre-existing upstream defect, reported not fixed.
7. **Colour type changed on three files.** `vinnie.png` was 8-bit greyscale and is now RGB;
   `zene_tile.png` was 1-bit indexed and is now RGBA; the mimetype PNGs keep RGBA. Pixel
   *dimensions* are identical everywhere (verified, §3c). No consumer in the tree depends on the
   colour type.
8. **The placeholder captions use a system font at authoring time** (resolved family recorded in the
   generator's output). The rendered text is committed as pixels, so there is no runtime font
   dependency — but the captions are not localised and are not meant to be.

---

## 6. Proposed wording for the known-limitations / release notes

**Not applied** — for the orchestrator (§5.2). Suggested addition to `KNOWN-LIMITATIONS.md`:

> **Branding is a placeholder (2026-09-12).** The application icon, project-file icon, plugin logo,
> splash screen, installer/bundle artwork and desktop backgrounds are **hand-authored placeholders**
> — a plain music note and a neutral document sheet — not the Zene Studio product mark. They replaced
> upstream LMMS identity artwork that was still shipping (41 files, 39 of them byte-identical to
> upstream). The placeholder carries no third-party licence and no attribution obligation, so
> shipping it is unencumbered; it is simply not the final mark. See `docs/BRAND-PLACEHOLDERS.md` for
> the provenance audit and the list of files. The macOS `.icns` and Windows `.ico` bundles were
> rebuilt and validated structurally but not rendered on their native platforms.

Suggested line for the release notes:

> Replaced all remaining upstream LMMS identity artwork with hand-authored, licence-free placeholders
> (`docs/BRAND-PLACEHOLDERS.md`). The product mark is still to be designed.

---

## 7. Reproducing this

```bash
cd <worktree>
python3 tools/brand/rasterise-placeholders.py            # regenerate all 38 rasters from the 3 SVGs
python3 tools/brand/rasterise-placeholders.py --check    # exit 0 iff the committed rasters match
python3 tests/evidence/brand-placeholders/verify-placeholders.py  # dimensions, metadata, containers, byte-identity
python3 tests/brand-resource-sweep.py                    # the resolution sweep (§3b)
bash tests/no-upstream-regression-gate.sh ; echo EXIT=$?  # Gate 6
bash tests/run-all-gates.sh ; echo EXIT=$?                # gates 1,3,4,5,6,7,8
```

Evidence lives in `tests/evidence/brand-placeholders/` **inside the repo**, not `/tmp` — an earlier
verification's `/tmp` evidence was destroyed by a disk reclaim.

---

## 8. Brief-vs-tree mismatches (reported, not silently adjusted)

Per the workspace rule that a quoted fact contradicting the tree must be reported rather than
guessed at:

| Claim in the brief | What the tree says | Command |
|---|---|---|
| the plugin logo is **"byte-identical to `origin/master`'s copy"** | **Not exactly.** The *drawing data* is byte-identical (`<path d>` md5 `45346519bb38606eeb1f51d1f153537f`), but the *file* is not: the rename lane changed one line, `<dc:title>`, in the commit this branch is based on. git records the rename as **R095**, not R100. The brief's substantive point — the art is upstream's — is correct and was acted on. | `git diff origin/master:data/themes/default/lmms-plugin-logo.svg HEAD:data/themes/default/zene-plugin-logo.svg` |
| its `<dc:title>` **"still reads `LMMS plugin logo`"** | **No** — it reads `Zene Studio plugin logo`. The rename lane changed it and documented doing so (`docs/RENAME-COMPLETE.md` §(h)). | `git show HEAD:data/themes/default/zene-plugin-logo.svg` |
| the logo is referenced from **"54 call sites"** | **29 live call sites in 29 code files** on this branch (28 under `plugins/`, 1 test fixture). The figure **54 is the pre-rename count** and included 25 frozen `tests/reference/**` upstream copies: `git grep -ohE '"lmms-plugin-logo"' HEAD~1 \| wc -l` → 54. This matches the rename lane's own correction. The 54 figure should not propagate. | `grep -rn 'PixmapLoader("zene-plugin-logo")' --exclude-dir=build . \| wc -l` → 29 |
| **"Gate 9 exists on this branch (it descends from `gate-debt`)"** | **It does not.** `post-alpha/gate-debt` is not an ancestor of HEAD and the script is absent. Recorded as **127**. The brief itself allowed for this ("if it reports exit 127 instead, say so"). | `git merge-base --is-ancestor post-alpha/gate-debt HEAD; echo $?` → 1 |
| run **`fork-sources-gate.sh`** | **That script does not exist on this branch** either — hence 127. A sibling lane's read-only copy was run instead (§4b). | `ls tests/fork-sources-gate.sh` → No such file |

---

## 9. The tests this change ships

* **`tests/src/core/PluginLogoResourceTest.cpp`** — pre-existing (rename lane); **re-run in both
  directions** against the replaced file, because this change alters exactly what it covers. Not
  modified.
* **`tests/brand-resource-sweep.py`** — new. The sweep of §3b, runnable and exit-coded as a
  **ratchet**: exit 0 while the unresolved set is exactly the pinned pre-existing baseline, exit 1
  the moment a new name stops resolving (`--strict` also fails on the baseline). Its negative
  control is in §3b. Registered in `tests/fork-sources.txt`.
* **`tests/evidence/brand-placeholders/verify-placeholders.py`** — new. The dimension/metadata/container/byte-identity
  checks of §3c.
* **`tools/brand/rasterise-placeholders.py --check`** — new. The negative-capable control for the
  rasters: it re-renders the three SVGs and exits 0 **only** if every committed bitmap is exactly
  what they produce, so a hand-edited or stale raster fails. Demonstrated in
  `tests/evidence/brand-placeholders/gen-check.txt`: exit **0** as committed → append one byte to
  `cmake/linux/icons/64x64/apps/zene.png` → exit **1**, naming that file → restore → exit **0**.

  (`QImage.save()` takes a file name or a QIODevice, not a Python buffer; the first version of
  `--check` passed it a `BytesIO` and raised instead of checking. Its exit code was read through a
  pipe at the time and looked green — the exact laundering the workspace's unpiped-exit-code rule
  exists to catch. It is fixed and now measured unpiped.)
