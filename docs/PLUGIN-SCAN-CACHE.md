# Plugin scan cache + quarantine — behaviour record, design and limits

**Branch:** `post-alpha/plugin-scan` (worktree `zene-pa-scan`, base `0c23587d2`, the commit
`v0.1.0-alpha` tags). **Status:** implemented on this branch, suite green; see §5.

This document is the lane's contract, in the order the scope asked for it:

1. §1 today's behaviour, with file:line evidence (written down **before** anything was changed)
2. §2 what was added (cache, quarantine, no-scan-at-startup)
3. §3 the tests, with the exact commands and the red/green runs
4. §4 where the new code runs (threads) and the real-time safety argument
5. §5 what is **not** proven

---

## 1. Today's behaviour (base commit `0c23587d2`), with file:line

### 1.1 When does discovery run?

**At startup — during `GuiApplication`'s constructor, before `QApplication::exec()`.** The chain,
verified in the tree:

| # | file:line | what happens |
|---|---|---|
| 1 | `src/core/main.cpp:812` | `new GuiApplication()` — before `app->exec()` at `src/core/main.cpp:962` |
| 2 | `src/gui/GuiApplication.cpp:158` | `m_mainWindow = new MainWindow;` |
| 3 | `src/gui/MainWindow.cpp:114` | MainWindow ctor: `sideBar->appendTab( new PluginBrowser( splitter ) );` |
| 4 | `src/gui/PluginBrowser.cpp:89` | PluginBrowser ctor calls `addPlugins()` (its last-but-one statement) |
| 5 | `src/gui/PluginBrowser.cpp:165` | `addPlugins()`: `auto descs = getPluginFactory()->descriptors(Plugin::Type::Instrument);` |
| 6 | `src/core/PluginFactory.cpp:95-101` | `PluginFactory::instance()` constructs on first use (`s_instance = std::make_unique<PluginFactory>();`) |
| 7 | `src/core/PluginFactory.cpp:56-60` | `PluginFactory::PluginFactory()` runs `setupSearchPaths(); discoverPlugins();` |
| 8 | `src/gui/GuiApplication.cpp:196` | `m_mainWindow->finalize();` (still before `exec()`) |
| 9 | `src/gui/MainWindow.cpp:363-374` | `finalize()` builds the Tools menu from `getPluginFactory()->descriptors(Plugin::Type::Tool)` **and** instantiates every tool plugin view (`ToolPlugin::instantiate(...)->createView(this)`) |

So there is no on-demand path at all: the first `getPluginFactory()` call — which is one of the two
GUI start-up sites above — pays for the whole scan inside the splash-screen phase ("Preparing plugin
browser"). Discovery is not repeated afterwards; the result is stored in the factory (§1.3) and the
GUI re-reads that stored list (`PluginBrowser.cpp:165`, `EffectSelectDialog.cpp:67`,
`ImportFilter.cpp:64`, `MainWindow.cpp:364`).

`PluginFactory::discoverPlugins()` is a public slot (`include/PluginFactory.h:94-95`), but nothing
in the shipped GUI re-invokes it.

### 1.2 Environment-variable exclude/filter patterns

* `LMMS_PLUGIN_DIR` — `src/core/PluginFactory.cpp:89-90` (`setupSearchPaths()`): adds a `plugins`
  search path. Read **once**, in the constructor.
* `LMMS_EXCLUDE_PLUGINS` — `src/core/PluginFactory.cpp:273` (`filterPlugins()`, called from
  `discoverPlugins()` at line 158): a comma-separated list of **Qt regular expressions** matched
  against the *absolute file path* (`PluginFactory.cpp:280-294`); `getExcludePatterns()` at
  `PluginFactory.cpp:249-268` trims, validates and warns on an invalid regex.
* `LMMS_EXCLUDE_LADSPA` — same mechanism, different consumer: `src/core/LadspaManager.cpp:51`.
* File *name* filter: `nameFilters` — `"lib*.so"` on Unix, `"*.dll"` on Windows
  (`src/core/PluginFactory.cpp:48-52`), used at `PluginFactory.cpp:153`.

### 1.3 Where the discovery result is stored

In the `PluginFactory` singleton, in memory only — `include/PluginFactory.h:98-104`:
`m_descriptors` (`QMultiMap<Plugin::Type, Plugin::Descriptor*>`), `m_pluginInfos`
(`QList<PluginInfo>` of `{QFileInfo file; shared_ptr<QLibrary>; Plugin::Descriptor*}`,
`include/PluginFactory.h:47-55`), `m_pluginByExt`, `m_errors`. Nothing is persisted, so every
process start pays the full scan again.

### 1.4 What the scan costs

`discoverPlugins()` (`PluginFactory.cpp:144-246`) loads **every** candidate file twice: a pre-load
pass (`PluginFactory.cpp:162-165`) and the resolve pass (`PluginFactory.cpp:167-242`), which
`QLibrary::load()`s the file and resolves `lmms_plugin_main` plus the
`<basename>_plugin_descriptor` symbol (`PluginFactory.cpp:177-192`; the `lib` prefix is stripped at
lines 180-183). A file that is not an LMMS plugin, or that fails to load, still costs the load and
lands in `m_errors` (`PluginFactory.cpp:171`). There is no fingerprinting, no cache and no skip
list; the only way to hide a plugin today is the `LMMS_EXCLUDE_PLUGINS` regex — a *process
environment* setting, not persisted, and it also prints nothing about what it dropped.

---

## 2. What this lane adds

### 2.1 The cache and the quarantine list — `PluginScanCache`

New, fork-added files:

* `include/PluginScanCache.h` — `PluginScanRecord` (one remembered file scan) and
  `PluginScanCache` (the store).
* `src/core/PluginScanCache.cpp` — JSON persistence, lookup, quarantine API.

The file lives at `<workingDir>/plugin-scan-cache.json` (`PluginScanCache::defaultFilePath()`,
where `workingDir` is `ConfigManager::inst()->workingDir()` — the same directory whose `plugins/`
sub-directory `setupSearchPaths()` already searches). `LMMS_PLUGIN_SCAN_CACHE=<path>` overrides it;
an empty value means "no persistence", which is what a process with no working directory gets.

Shape:

```json
{
  "version": 1,
  "quarantine": [ { "path": "/abs/path/libfoo.so", "reason": "hangs on load" } ],
  "files": [
    { "path": "/abs/path/libfoo.so", "size": 123456, "mtime": 1726000000000,
      "status": "has-descriptor", "name": "foo", "displayName": "Foo", "description": "...",
      "author": "...", "version": 256, "type": 0, "supportedFileTypes": "",
      "logoName": "foo/logo", "logoHasInlinePixmap": false, "subPluginFeatures": false }
  ]
}
```

* `status` is one of `has-descriptor`, `not-a-plugin` (it loads, or not, but no LMMS descriptor is
  exposed) and `load-failed` (with the `QLibrary` error text).
* A record is only *used* when the file's path, size **and** mtime still match
  (`PluginScanCache::lookup()`, `src/core/PluginScanCache.cpp`), so a replaced or rebuilt library is
  always re-scanned.
* Failure tolerance is the contract: a missing file, an unreadable file, invalid JSON, a wrong
  format version or an individually malformed record all degrade to a full scan — `load()` returns
  false and the cache stays empty. `save()` writes through `QSaveFile`, so a failed write leaves the
  previous cache intact. Nothing in the discovery result depends on the cache file's state; it only
  decides how much work a scan repeats.

### 2.2 Where discovery changes — `src/core/PluginFactory.cpp`

`discoverPlugins()` now, in order (`src/core/PluginFactory.cpp`):

1. loads the cache (failure-tolerant, per 2.1);
2. enumerates the search paths exactly as before (`nameFilters`, `filterPlugins()` for
   `LMMS_EXCLUDE_PLUGINS` — untouched);
3. **quarantine pass**: every file whose absolute path is in the quarantine list is removed from the
   candidate set, counted, and remembered in `ScanStats.quarantinedPaths` /
   `quarantinedReasons` — the "what did you skip and why" report;
4. **planning pass**: each remaining file is either *loaded* (no record, a changed record, or a
   record that cannot be rebuilt faithfully), *served from the cache*, or *skipped* (remembered as
   `not-a-plugin` / `load-failed`);
5. the cheap pre-load pass (still there, unchanged in shape) covers only the files this run loads;
6. the resolve loop: cache-served entries get a rebuilt descriptor and a `QLibrary` that is
   **pointed at the file but not loaded**; everything else takes the original code path verbatim and
   records its outcome in the cache;
7. the cache is saved if anything changed, and the scan logs one line:
   `plugin-scan: N file(s), Q quarantined, S served from cache, B known-bad skipped, L scanned, P plugin(s)`.

What a cache hit actually skips: `QLibrary::load()`, the `lmms_plugin_main` /
`<basename>_plugin_descriptor` symbol resolution, and the double load of the pre-load pass. The
descriptor is rebuilt from the recorded strings/metadata and is owned by the factory
(`PluginFactory::CachedDescriptorStore`); a descriptor's logo is rebuilt as a host-side
`PixmapLoader` from the recorded pixmap name (`tripleoscillator/logo`), so browser icons and menu
icons are the same pixmap as before.

Two shapes are deliberately **never** served from the cache, and take the original loading path:

* a descriptor with `subPluginFeatures` — sub-plugin enumeration is a virtual call into the library
  (`PluginFactory.cpp:226-238` in the base tree), so VST/VST3/CLAP/LADSPA hosts always load;
* a descriptor whose logo is a compiled-in XPM (`PixmapLoader::xpm() != nullptr`, accessor added in
  `include/embed.h`) — there is no name to store and no way to rebuild it.

The deferred library loads on first use: `Plugin::instantiate()` calls
`pi.library->resolve("lmms_plugin_main")`, and `QLibrary::resolve()` loads the library it is called
on if it is not loaded yet (verified with a standalone Qt 6 program before relying on it).

### 2.3 Reporting and the API surface

* `PluginFactory::ScanStats` (`include/PluginFactory.h`) — candidate files, quarantined, served from
  cache, known-bad skipped, scanned, descriptors, plus the quarantined paths and reasons.
* `PluginFactory::scanReport()` — the one-line summary above.
* `PluginFactory::scanCache()` — the store itself, so a UI (or a script, or a test) can quarantine a
  plugin: `getPluginFactory()->scanCache().addToQuarantine(path, reason)` then `save()`. Today the
  documented way to mark a plugin bad is to add `{"path": ..., "reason": ...}` to the `quarantine`
  array of `plugin-scan-cache.json` (it is re-read on every scan) or to call that API; there is **no
  GUI for it yet** — see §5.
* Quarantine wins over everything: a quarantined plugin is hidden even if the environment would
  allow it, and it is skipped before any planning happens.

### 2.4 Discovery no longer happens at start-up

The two start-up call sites in §1.1 were the whole problem, and both are now deferred:

* `src/gui/PluginBrowser.cpp` — the constructor no longer calls `addPlugins()`; the new
  `PluginBrowser::showEvent()` (`src/gui/PluginBrowser.cpp`, declared in `include/PluginBrowser.h`)
  builds the tree the first time the tab is shown. Nothing scans plugins when the browser object is
  constructed.
* `src/gui/MainWindow.cpp` — `finalize()` no longer queries `descriptors(Plugin::Type::Tool)` and no
  longer instantiates every tool plugin view. The new slot `MainWindow::updateToolsMenu()`
  (declared in `include/MainWindow.h`, connected to `QMenu::aboutToShow` in `finalize()`) builds the
  menu the first time it is opened; a build with no tool plugins drops the menu from the menu bar
  instead of leaving an empty one.

After the change, the only `getPluginFactory()` call sites left in the start-up path are inside
those two deferred handlers. With no cache file and the quarantine list empty the *discovery result*
is identical to before — the cache/quarantine code has no say in what is found, only in how much
work is repeated — and that is what `testCacheDisabledBehavesLikeAColdCache` asserts.

### 2.5 Threads, and why none of this is on the audio thread

Every line of the new code runs on the thread that calls `PluginFactory::discoverPlugins()`, plus
`main()`'s construction of the factory:

* the factory constructor is called from `PluginFactory::instance()`
  (`src/core/PluginFactory.cpp`), whose call sites are start-up code (`src/gui/MainWindow.cpp` —
  `updateToolsMenu`), a widget's first show (`src/gui/PluginBrowser.cpp` — `showEvent`), project
  load/import (`src/core/DataFile.cpp`, `src/core/ImportFilter.cpp`), and GUI actions
  (`FileBrowser`, `EffectSelectDialog`, `TrackContainerView`, `InstrumentTrackWindow`,
  `PresetPreviewPlayHandle`) — all main-thread;
* `PluginScanCache` is referenced from exactly one place in the product
  (`grep -rn PluginScanCache src/ include/` → `PluginFactory.{h,cpp}` + its own header/source);
* nothing in the audio rendering path (`AudioEngine`'s render loop → `Effect::process` /
  `Instrument::play`) touches `PluginFactory` or the cache: it uses already-loaded plugin objects.
  The scan does no locking at all (no mutex, no condition variable, no thread), so it cannot
  block or wake an audio thread; it allocates, which is why it must never be called from one, and
  it isn't.

---

## 3. Tests

`tests/src/core/PluginScanCacheTest.cpp` — registered in `tests/CMakeLists.txt` (`LMMS_TESTS`,
plus `ENABLE_EXPORTS`, `LMMS_TEST_SCAN_PLUGIN_DIR` and `QT_QPA_PLATFORM=offscreen` for this test
only), and listed with the new sources in `tests/fork-sources.txt` / `tests/all-sources.txt`.

The fixture is the **real built plugin module**, copied into a temporary plugin directory:
`<build>/plugins/libtripleoscillator.so` → `libtripleoscillator.so`, and the search path is pinned
to that directory (`pinFixtureAndRescan()`), so every count asserted is independent of what else
lives on the machine. That means the enumeration path — directory listing, `QLibrary::load()`,
`lmms_plugin_main` and descriptor resolution — is exercised for real; only the two shapes the cache
refuses to serve (SubPluginFeatures, compiled-in XPM logos) have no fixture.

Slots: `testCacheRoundTrip`, `testLookupMissesOnChangedFingerprint`,
`testQuarantineSurvivesSaveAndLoad`, `testCorruptCacheDegradesToFullScan`,
`testRealPluginIsScannedThenServedFromCache`, `testChangedPluginFileIsRescanned`,
`testCacheHitDoesNotLoadTheLibrary`, `testQuarantinedPluginIsHiddenFromDiscovery`,
`testKnownBadFileIsNotLoadedAgain`, `testCacheDisabledBehavesLikeAColdCache`,
`testPluginBrowserDefersDiscoveryToFirstShow`.

Exact commands:

```
cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-pa-scan
JOBS=6 bash tools/local-ci.sh --build-dir build --jobs 6      # configure + build + ctest, unpiped exit codes
cd build/tests && ctest -R PluginScanCacheTest --output-on-failure   # the new test alone
```

(`tools/local-ci.sh` is committed mode 100644, so the invocable form is `bash tools/local-ci.sh`.)

Red and green runs are pasted verbatim in `docs/PLUGIN-SCAN-CACHE-RUNS.md` (raw logs, unpiped exit
codes).

---

## 4. What is NOT proven

* **No third-party plugin was loaded.** The fixture is the in-tree `tripleoscillator` module. A
  real VST3/CLAP/LV2/LADSPA descriptor is *never* cache-served by construction (SubPluginFeatures),
  so that path is unchanged — but it is also not exercised here, and no test asserts it stays
  uncached with a real host plugin.
* **Compiled-in XPM logos have no fixture.** The code refuses to cache-serve them; untested.
* **A plugin that hangs inside `dlopen()` cannot be timed out by this design.** The scan is
  in-process and blocking; the cache only stops the hang from recurring *after* the user quarantines
  the plugin by hand (edit `plugin-scan-cache.json`). There is no out-of-process scan, no timeout
  and no automatic "it hung, so quarantine it" — that is the documented next step, not this lane.
* **The Tools-menu deferral is verified by inspection, not by a test.** Constructing `MainWindow`
  needs a full GUI application and `Engine`; the test covers `PluginBrowser` instead. The evidence
  for `MainWindow` is structural: the only remaining `getPluginFactory()` call in
  `src/gui/MainWindow.cpp` is inside `updateToolsMenu()`, which is connected to
  `QMenu::aboutToShow` in `finalize()` — nothing calls it at start-up.
* **No process-level before/after measurement.** This box has no display and no Xvfb run was done,
  so "the alpha no longer scans during the splash screen" is evidenced by the call-site chain in
  §1.1 vs §2.4 plus the widget-level test, not by `strace`/a timed GUI launch.
* **Cache-served plugins change *when* the library is loaded** — first use instead of scan time.
  The scan's pre-load pass therefore covers fewer files (only the ones this run loads). In this tree
  the only dependency that pass existed for, `ZynAddSubFxCore`, is a *static* library linked into
  the plugin (`plugins/ZynAddSubFx/CMakeLists.txt`), so nothing in-tree depends on load ordering;
  a third-party plugin that relies on a sibling library having been loaded first could still resolve
  that dependency later than before.
* **Only one finding per file is remembered for a *changed* file's old state**: the cache keeps one
  record per path (the latest scan), by design; a stale record is replaced, not kept for rollback.
* The scan logs one `qInfo` line per scan (`plugin-scan: ...`). On a cold run that is one new line
  in the log; on a warm run the per-file `QLibrary` warnings for known-bad files are *not* repeated
  (the failure text is restored into `PluginFactory::errorString()` instead, so
  `Plugin::instantiate()`'s message is unchanged).
