# QDEBUG-CLASS-AND-MIME-RENAME — the two release-blocking residues before the 0.2.1 tag

Lane: `fix/qdebug-class-and-mime-names` (worktree `zene-qdebug`), branched from the release tip
`post-alpha/integration` @ `0774ce444`. **Nothing was pushed and no tag was created.** Two findings,
one class each, both fixed here.

* **Finding 1** — a compile-error class (a hard error, not a `-Werror` promotion) that the next CI
  run would have hit on every Unix job: `q*() <<` streams in translation units that never include
  `<QDebug>`.
* **Finding 2** — the retired product name still shipping in the Linux icon set (13 raster MIME
  icons) and in one public install destination.

Sources used: the independent audit `docs/CI-FIX-AUDIT.md` (branch `audit/ci-fixes`, worktree
`zene-cifix-audit` — read, never merged and never modified), the v0.2.0-alpha CI logs, and a
Qt5-emulation harness built and run here.

---

## Part 1 — the `<QDebug>` class

### 1.1 The mechanism (re-derived here, not taken on trust)

`<QtGlobal>` forward-declares `QDebug` and defines the `qDebug()`/`qWarning()`/… macros; only
`<QDebug>` **defines** the class. `qWarning() << x` therefore compiles only if some other header in
the translation unit drags `qdebug.h` in. Qt 6.4's `<QCoreApplication>`, `<QVariant>`,
`<QLoggingCategory>`, `<QCborCommon>`, `<QAccessible>`, `<QGenericMatrix>` and `<QVulkanInstance>`
all include it; **Qt 5.15's counterparts do not** — that single difference is the whole class, and
it is why this box (Qt 6.4.2; no Qt5 development files) builds the tree while the CI's Qt5 jobs
cannot.

Measured boundary of the class, so the sweep below is a whole-tree sweep and not a sample:

| construct | needs the definition? | evidence |
|---|---|---|
| `qWarning() << x;` | **yes** — fails | `01-selftest.log`, CI log `linux-x86_64.clean.log:6799` |
| `qInfo().noquote() << s;` | **yes** — member call | same |
| `qDebug("x %d", 1);` (printf form) | no | `g++ -std=c++17 $(pkg-config --cflags Qt6Core) -fsyntax-only` against `<QtGlobal>` + `<QString>`, exit 0 |
| `qWarning("Unexpected poll error.")` | no | same form, e.g. `include/RemotePluginBase.h:520` |

So a translation unit can only be in this class if its text mentions a `q*()` logging function or
the `QDebug` type at all. Scanned over every tracked source under `src/ include/ plugins/ tests/
tools/ modules/` **and** over all **1393** distinct TUs in `build/compile_commands.json`: **84** TUs
are in the class, plus **one** (`tests/src/wasm/WasmSandboxTest.cpp`) that is in the class but is
not in this box's build because `WANT_WASM` is off here (see §1.4). Everything else is textually
incapable of the error.

### 1.2 How the proof was produced

`build/qt5-shadow/` holds the **seven** Qt6 headers above with their `#include <QtCore/qdebug.h>`
line removed; the harness compiles each TU through **its own command line from
`compile_commands.json`** (that is the point — the CI's flags, not an approximation) with
`-I<shadow>` first and `-fsyntax-only`. The shadow path must be absolute: the compile entry's cwd
is a build subdirectory.

```
bash tests/integration-logs-qdebug/qdebug-class-sweep/run.sh build
```

`--selftest` is the control that makes every other verdict worth something: it compiles a **copy**
of `src/core/ConfigManager.cpp` (never the tracked file) twice, through that TU's own command line,
once as-is and once with its `#include <QDebug>` line deleted.

```
SELFTEST as-is      rc=0
SELFTEST no-qdebug  rc=1   …/no-qdebug-ConfigManager.cpp:705:33: error: invalid use of incomplete type ‘class QDebug’
SELFTEST PASS: the shadow reproduces the CI error (one include is the difference)
```

The error text is the CI's verbatim text. The emulation is **one-directional on purpose**: stripping
a header can only remove a path, never add one, so `rc=0` is evidence of Qt5-safety while `rc!=0` is
a candidate that needs the include. The known limit is stated rather than hidden: this models Qt5 as
"Qt6 minus the seven headers that add `qdebug.h`", which is exact for those seven (they are the only
`QtCore/*.h` and `QtGui/*.h` files that include it) but is not a Qt5 header set; a chain that Qt5
supplies by some other route would make a verdict over-strict (never under-strict in the direction
that matters).

### 1.3 Finding 1 — the sites changed, pre-fix and post-fix

Five TUs needed the include. The four the audit named, plus `src/gui/embed.cpp` — the audit's
"one more candidate, flagged but not proven" (it is now proven: the emulation fails it at
`embed.cpp:50:25` with the CI's error text, and adding the include clears it; the include is a
no-op if the emulation was over-strict for it, which is the cheap side of the bet).

| TU | `q*() <<` sites | pre-fix (emulation) | post-fix |
|---|---|---|---|
| `src/core/Plugin.cpp` | 230, 256 | `rc=1  Plugin.cpp:230:33: error: invalid use of incomplete type ‘class QDebug’` | rc=0 |
| `src/core/ImportFilter.cpp` | 97, 128 | `rc=1  ImportFilter.cpp:97:33: …` | rc=0 |
| `src/core/PeakController.cpp` | 217 | `rc=1  PeakController.cpp:217:41: …` | rc=0 |
| `plugins/LadspaEffect/LadspaEffect.cpp` | 462, 478, 496, 524 | `rc=1  LadspaEffect.cpp:462:33: …` | rc=0 |
| `src/gui/embed.cpp` | 50, 57 | `rc=1  embed.cpp:50:25: …` | rc=0 |
| `tests/src/wasm/WasmSandboxTest.cpp` | 660 (`qInfo().noquote() <<`) | not in this box's build (WANT_WASM=OFF); fixed by inspection | not compilable here |

Evidence: `03-prefix-sweep-class.log` (pre-fix, 84 TUs: **5 fail, 22 inconclusive, 79 clean**),
`11-class-sweep.log` (post-fix), `01-selftest.log`. The 22 "inconclusive" pre-fix rows are build-tree
artefacts of running the sweep before any build had run in this worktree — a generated
`ui_about_dialog.h`, a `<name>.moc`, or (one case) a different incomplete type,
`MidiClipView.cpp`'s `QSet<Track*>` — never counted as passes; the post-fix run is after the build,
so nothing is inconclusive there.

The include is placed in the Qt include run in alphabetical position, with a three-line comment
naming the mechanism, exactly as the ConfigManager fix did. It is compile-only: no behaviour, no
output, no ABI change.

### 1.4 Finding 1 — what was found already correct (not changed)

* **79 of the 84** TUs in the class compiled clean under the emulation **before** the fix. The full
  per-TU table is in `03-prefix-sweep-class.log`. That set includes every TU the audit cleared —
  `tests/src/core/{ScriptEngineTest,StableTrackIdsTest,PluginScanCacheTest,PluginLogoResourceTest,
  AudioBufferTest}.cpp` and `tests/src/plugins/PluginPortsMigrationTest.cpp` — and the ones the
  project's own headers supply. In the post-fix run (`11-class-sweep.log`) **83 of the 84** are
  clean and the 84th is the inconclusive row below.
* **The only project header that supplies `QDebug` to other TUs is `include/PluginIssue.h`**
  (`#include <QDebug>` at line 28, for its `QDebug operator<<(QDebug, const PluginIssue&)`). That is
  the header the audit's "is not supplied it by one of the project's own headers" test turns on, and
  a scan of every tracked header shows no other one uses a `q*() <<` stream.
* `src/core/ConfigManager.cpp` — fixed at the v0.2.0-alpha tag, still correct
  (`rc=0` under the emulation, ledger entry present).
* `tests/src/wasm/WasmSandboxTest.cpp` is in the class but **outside this box's build** (`WANT_WASM`
  needs wasmtime): its `qInfo().noquote() <<` at line 660 cannot be compiled here. It is fixed by
  inspection — one include, no behaviour — and the document says so instead of claiming a sweep
  verdict it does not have. Everything else under `tests/src/wasm/` is out of the class.
* `tests/evidence/instrument-view-safety/qt-probe/probe-setwindowicon.cpp` streams with `<<` and
  already includes `<QDebug>` (line 14).

### 1.5 Ledger

Every file touched above is upstream-inherited and is declared in `tests/upstream-modifications.txt`
**in the same commit**: the four files the audit named already had entries, whose reasons now carry
the `tag v0.2.1-alpha CI fix` clause; `src/gui/embed.cpp` (byte-identical to upstream before this
change) gets a **new** entry. `tests/**` needs no ledger entry (Gate 6 allows it by rule) and
`tests/src/wasm/WasmSandboxTest.cpp` is fork code in any case.

---

## Part 2 — the retired name in the shipped icon set

### 2.1 The 13 raster MIME icons

`cmake/linux/icons/*/mimetypes/application-x-lmms-project.png` — 13 files, one per raster size
(16, 16@2, 24, 24@2, 32, 32@2, 48, 48@2, 64, 64@2, 128, 128@2, 256) — while the type those files
provide the icon *for* had already been renamed: `cmake/linux/zene.xml` declares
`application/x-zene-project`, `cmake/linux/zene.desktop` sets `MimeType=application/x-zene-project`,
and the macOS plist derives from `@CPACK_PROJECT_NAME@`. Only the **scalable** SVG had been renamed.
Result: the renamed MIME type had an icon at the scalable size and **none** at any raster size, so
`.mmp`/`.mmpz` fell back to a generic icon in file managers that do not read SVGs. Renamed
`application-x-zene-project.png` with `git mv` (git records all 13 as `R100` — pixels unchanged by
the rename).

The rename's own inventory is why this was invisible: `docs/RENAME-COMPLETE.md` listed only
`icons/scalable/mimetypes/` as the MIME icon surface, and its sweep row `x-lmms (MIME/clipboard) → 0`
was read past even though every one of the 13 paths matched the pattern. Both lines are corrected in
this change.

### 2.2 Every reference updated

| reference | what it said | now |
|---|---|---|
| `tools/brand/rasterise-placeholders.py` | writes and re-checks the 13 PNGs (3 occurrences: docstring, write path, `--check` expectations) | new name — the tool would otherwise write the art to the retired path and report 13 "DIFFERS" |
| `tests/evidence/brand-placeholders/provenance-audit.py` | `IDENTITY` list, `…/mimetypes/application-x-lmms-project.png` | new name + a comment recording the rename so a re-run against a pre-rename revision still means the same thing |
| `docs/BRAND-PLACEHOLDERS.md` | row 16–28 named the old path with upstream path "*same path* (never renamed)" | new path; upstream path is now the old name via git rename, with the date and this document |
| `docs/RENAME-COMPLETE.md` | MIME row listed only `icons/scalable/mimetypes/`; sweep row `x-lmms → 0` | MIME row now "all 14 sizes"; the sweep row says **0** with the history (was 13) |
| `tests/upstream-modifications.txt` | 13 entries, reason "path is unchanged, only the image" | 13 old-path entries re-pointed to the rename (a rename deletes the path from upstream's namespace) **plus 13 new-path entries** carrying the rename reason and the placeholder provenance |

No install rule, icon-theme index or packaging manifest names these files by hand:
`cmake/linux/CMakeLists.txt:1` installs the whole `icons/` tree, and `cmake/linux/zene.spec.in`,
the NSIS/Apple asset lists and `data/**` do not mention the mimetype raster names.

### 2.3 `CMakeLists.txt` — two shipped-name destinations

1. **`CMakeLists.txt:979`** `DESTINATION "include/lmms/"` — the public header install root (LINUX
   only; `${LMMS_INCLUDES}` + `lmmsconfig.h` + `lmmsversion.h` + `embed.cpp`). Every other install
   destination had already been renamed; this one still carried the retired product name.
   Now `DESTINATION "include/${PROJECT_NAME}/"`, so a future rename cannot leave it behind again.
   Nothing in the tree installs to or `#include`s `<lmms/…>`, so nothing consumes it.
2. **`CMakeLists.txt:993,1003`** (found in the same class, not in the audit) — the `dist` target
   built `${TMP} = "lmms-${VERSION}"` and tarred `lmms-${VERSION}-src.tar.bz2`. That is a *shipped
   artefact name*, so it is a trace, not an identifier: now `${CMAKE_PROJECT_NAME}-…`
   (`zene-0.2.1-alpha-src.tar.bz2`). Nothing in the tree referenced the old tarball name.

### 2.4 The repo-wide proof

```
$ git ls-files cmake/linux/icons | grep -c 'application-x-zene-project.png'   # 13
$ git ls-files cmake/linux/icons | grep -c 'application-x-lmms-project.png'   # 0
$ git grep -n 'x-lmms' -- . ':!tests/*' | wc -l                               # 7
```

The 13 renamed files exist; no tracked path carries the old raster name; and **every one of the 7
remaining mentions outside `tests/` is prose or provenance, not a name anything reads**:

| where | what it is |
|---|---|
| `cmake/linux/icons/scalable/mimetypes/application-x-zene-project.svg:7,28` | the renamed SVG's own comment and `<dc:rights>`, which name the upstream file it replaces — a licence/provenance statement |
| `docs/BRAND-PLACEHOLDERS.md:54,55` | the provenance audit's own "upstream path" column, i.e. the column that exists to name the old path |
| `docs/RENAME-COMPLETE.md:134` | the corrected `x-lmms` sweep row, which records that it was 13 and is now 0 |
| `docs/WAVE-R-RENAME.md:262-264` | the wave-R record's decision to keep the upstream MIME ids, now carrying an explicit *superseded* note pointing at rename layer 1 and this document — the wave-R record itself is not rewritten |

Inside `tests/` the mentions are **run logs, transcripts and byte-level audit outputs produced at
earlier commits** — the brand-placeholder lane's evidence
(`tests/evidence/brand-placeholders/{gen.log,pre-change-dims.txt,provenance-audit.txt,gate-*}`), the
per-lane gate logs (`tests/integration-logs-*/**`), and one recorded command transcript. Rewriting a
log of a run that happened is fabricating evidence, so they are kept verbatim; each records a state
that was true when it was written. The live tools and the audit script among them
(`rasterise-placeholders.py`, `provenance-audit.py`) *were* updated — §2.2.

Full command output: `tests/integration-logs-qdebug/30-mime-rename-greps.log`.

### 2.5 Traces vs internal identifiers — what was left, and why

The same class was swept for the rest of the tree. Nothing else was renamed, and here is the reason
for each group:

| what | count | verdict | why |
|---|---|---|---|
| `set(lmms …)` / `${lmms}` / `${LMMS}` CMake *variable names* in `cmake/apple/MacDeployQt.cmake`, `cmake/linux/LinuxDeploy.cmake` | ~26 uses, 2 files | **internal identifier — left** | every one resolves to `zene`/`ZENE` at runtime (`set(lmms "${CPACK_PROJECT_NAME}")`); the variable *name* is stale, the value is the product. No user ever sees it, and renaming it churns the AppImage/DMG deploy scripts that cannot be executed on this box |
| `lmms_*` file names (`lmmsconfig.h`, `lmmsversion.h`, `lmms_export.h`, `lmms_constants.h`, `lmms_math.h`, `lmms.qrc`), the `lmms::` namespace, `LMMS_*` macros, the `lmms_plugin_main` entry symbol, `lmmsobjs` | 373 files / 462 / 751 / 114 | **internal identifiers — left** | declared deliberately kept in `docs/RENAME-COMPLETE.md` (rename layer 2 is bounded); `lmms_plugin_main` is an ABI, not a string |
| read-both legacy paths (`.lmmsrc.xml`, `~/Documents/lmms`, `<lmms/>`, `lmms-workspace`, the `lmms` Lua alias, `lmms_SOURCE_DIR`) | ~1 per site, commented | **deliberate legacy reads — left** | they exist *to be adopted, never to be written* |
| upstream URLs / identities (`lmms.io`, `github.com/LMMS/*`, the ZynAddSubFX `.rc` string, the OpenBSD port URLs) | ~1305 header URLs | **provenance — left** | true statements about a third party |
| `build/lmms` defaults in developer tools (`tests/{automation-modes-render-check.sh,data/loudness/render-evidence.sh,data/oop-hosting/*.sh,vst3-instrument-render-proof.sh,data/*/render-proof.*}`, `plugins/RnnoiseDenoiser/testdata/*.sh`, `tools/render-determinism-probe.sh`, `tools/mmpz-git/render-recipe.sh`, `tools/mcp-zene-control/tests/*`) | ~15 files | **stale tooling default — left, reported** | these are dev-only scripts, none invoked by `build.yml` or `ctest` (checked); several already prefer `build/zene` with a `build/lmms` fallback, and two of the hits are a recorded `commands_snapshot.json` fixture and a captured transcript that must not be rewritten. They do not ship and cannot be reached by a user; the audit's item 4 records the same list. **The fix, if the owner wants it, is one mechanical pass** replacing each default with `build/zene` and keeping `build/lmms` as the fallback — the pattern `tools/mmpz-git/mmpz_git.py` already uses |
| `tests/QA-GATES.md`, `tests/agent-surface-negative-control.md`, prose in the same scripts | wording | **documentation — left** | usage text that *takes* the binary as an argument |

---

## Verification and evidence

All logs under `tests/integration-logs-qdebug/`; every exit code recorded unpiped (workspace rule 6).

| step | command | result |
|---|---|---|
| Qt5 emulation, self-test | `run.sh` → `00-shadow.log`, `01-selftest.log` | **PASS** — reproduces the CI error verbatim; one include is the difference |
| Qt5 emulation, pre-fix class sweep | `qdebug_emulate.py --all` on the pre-fix tree → `03-prefix-sweep-class.log` | 84 TUs swept, **5 fail** with the CI error text, 79 clean, 22 inconclusive (no build had run yet) |
| Qt5 emulation, post-fix class sweep | `run.sh build` → `11-class-sweep.log` | **sweep EXIT=0** — 84 TUs, **0 fail**, 1 inconclusive (`MidiClipView.cpp`'s `QSet<Track*>` incomplete type, a shadow side-effect, explicitly not counted as a pass) |
| build | `cmake --build build -j2` → `10-build.log` | **BUILD_EXIT=0**, 0 `error:` lines |
| tests | `cd build/tests && ctest -j2` → `12-ctest.log` | **CTEST_EXIT=0 — 100% tests passed, 0 tests failed out of 86** |
| `bash tests/fork-sources-gate.sh` | `20-…log` | **PASS** — 1298 sources scanned, 0 stale, 0 unregistered |
| `bash tests/no-upstream-regression-gate.sh` | `21-…log` | **PASS** — 457 changed paths declared (was 443); the ledger holds 470 entries (was 456) |
| `python3 tests/scripted/check-namespace` | `22-…log` | **0 errors** |
| `bash tests/release-version-gate.sh` | `23-…log` | **PASS** — 0.2.1-alpha is the tree's, its release notes' and its download link's version |

The gate logs above are the runs **on the committed tree** (both commits in), because Gate 6 and Gate 9
diff `base..HEAD`: an uncommitted fix is invisible to them.
