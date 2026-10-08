# plugins batch-04 — 6 findings

Branch `bugs/bunny-plugins-2026-10-05`. One finding was fixed; the other five are
classified from the current source with no product change.

### Cluster 5542 — CLAP init receives a non-UTF-8 path on some Windows locales

Verdict: SKIPPED-SCOPE

Evidence: `plugins/ClapEffect/ClapLoader.cpp` in `load()` sets
`library.pathUtf8 = QFile::encodeName(modulePath);` even though the CLAP field is
UTF-8 and the member is named `pathUtf8`. On POSIX, `QFile::encodeName()` already
returns UTF-8, so the defect exists only on a non-UTF-8 Windows code page. The OS
loader itself is fine: the POSIX `openPlatform` uses `QFile::encodeName` and the
Windows one uses `LoadLibraryW` with `path.utf16()`, so only the plug-in-visible
string is wrong. Platform-only, and no deterministic headless RED is possible on
this Linux build. Documented one-line change, not applied: `modulePath.toUtf8()`.

Commit: none

Test: none

Red/green: none

### Cluster 2707 — Note queue counter comparison can overflow signed arithmetic

Verdict: REAL-UNTESTED

Evidence: `plugins/ClapEffect/ClapNoteQueue.h:144-146` and `:175-177` cast the
`std::uint32_t` sequence and position to `std::int32_t` and subtract them. When the
counters straddle `0x80000000` the subtraction is formally signed-overflow UB, so
the finding is technically right — and the identical house idiom sits at
`plugins/Vst3Effect/Vst3MidiQueue.h:105-107,136-138`, which is untouched by this
lane's scope question but confirms the pattern is intentional. There is no
observable failure on a two's-complement target, and demonstrating it would need
roughly 2^31 queue operations, which no headless test can do. Per procedure, no
product change. Candidate hardening if the parent wants it later:
`static_cast<std::int32_t>(sequence - position)` (unsigned wrap first, so the
result always fits).

Commit: none

Test: none

Red/green: none

### Cluster 5547 — Queue reset races with live MIDI producers

Verdict: FALSE-POSITIVE

Evidence: `plugins/ClapNoteQueue.h:200-211` documents `reset()` as main-thread
only, and its sole caller is `plugins/ClapEffect/ClapHostLifecycle.cpp:84` inside
`prepare()`. That call is made under `engine->requestChangeInModel()` at
`plugins/ClapEffect/ClapEffect.cpp:134` and
`plugins/ClapInstrument/ClapInstrument.cpp:207`. `requestChangeInModel()` locks
`m_changeMutex` (`src/core/AudioEngine.cpp:879-883`) and `renderNextPeriod()` holds
that same mutex for the whole render period (`src/core/AudioEngine.cpp:525`), so
no producer can be inside the queue while `reset()` runs. Every note-queue producer
is audio-thread: the push sites in `plugins/ClapHostNotes.cpp:74-130` are only
reached from `ClapInstrument::play()`
(`plugins/ClapInstrument/ClapInstrument.cpp:254,268,272,279`). There is no race.

Commit: none

Test: none

Red/green: none

### Cluster 5543 — CLAP bundle directories are discovered but passed to the loader as files

Verdict: SKIPPED-SCOPE

Evidence: `plugins/ClapEffect/ClapSubPluginFeatures.cpp:71-78` includes `QDir::Dirs` and pushes
`entry.absoluteFilePath()` for every `*.clap`, including a directory. The loader
then hands that path straight to `dlopen`/`LoadLibrary`
(`plugins/ClapEffect/ClapLoader.cpp`, `openPlatform`), which cannot execute a
directory, so a macOS bundle would fail to load. But the behaviour is intentional and
platform-shared: the VST3 twin does exactly the same for `.vst3` directories at
`plugins/Vst3Effect/Vst3SubPluginFeatures.cpp:70-84`, and no bundle-resolution helper
(`Contents/MacOS`, executable-inside-bundle) exists anywhere in `plugins/`. Adding one
is a design change across both hosts, not a minimal fix, and `collectModules` is a
private static member so there is no seam to test it with. Bundle layout cannot be
validated headlessly on Linux either.

Commit: none

Test: none

Red/green: none

### Cluster 4700 — Plug-in scan failures are silently discarded

Verdict: FIXED

Evidence: `plugins/ClapEffect/ClapSubPluginFeatures.cpp` `listSubPluginKeys()` called
the `listClasses()` overload without the typed loader status
(`plugins/ClapEffect/ClapHostLifecycle.cpp:132-165`), so the `QString error` it filled
was discarded and a failing module simply produced an empty class list. It now takes
a `clap::loader::Status`, and when `status.code != clap::loader::Code::None` it
`qWarning()`s the module path, `clap::loader::token(status.code)` (tokens defined at
`plugins/ClapEffect/ClapLoader.cpp:130-141`) and the error detail. This is
diagnostics only: no control command, schema or discovery behaviour changed.

Commit: 2bc05e292

Test: `ClapEffectIntegrationTest::testScanFailuresAreReported` (added to the existing
executable; the fixture path comes from `clap-test-broken-no-symbol` via
`tests/CMakeLists.txt`, and the dir is injected through
`ConfigManager::inst()->setVSTDir()`, which `pluginpaths::searchDirs(Format::Clap)`
includes per `src/core/PluginPaths.cpp:87-88`)

Red/green:

    cmake --build build --target zene ClapEffectIntegrationTest > build/bunny-b.log 2>&1   # EXIT=0
    cd build/tests && ctest -R '^ClapEffectIntegrationTest$' --output-on-failure > .../bunny-t.log 2>&1

- RED `EXIT=8` — `FAIL! ... 'named' returned FALSE. (the scan reported 0 warning(s), none naming clap-test-broken-no-symbol.clap and its typed reason: )`, Totals 14 passed / 1 failed
- GREEN `EXIT=0` — 1/1 passed; direct run of the single case `EXIT=0` with `PASS : ...::testScanFailuresAreReported()` (not skipped)
- Related suites: `ctest -R '^ClapLoaderErrorTest$|^ClapHostTest$|^ClapBusMapTest$'` → `EXIT=0`, 3/3 passed
- Gates: `bash tests/complexity-gate.sh --check` → `EXIT=0`; `bash tests/file-length-gate.sh --check` → `EXIT=0`

### Cluster 4679 — Instrument parameter controls mix plain and normalized values

Verdict: ALREADY-FIXED

Evidence: `plugins/ClapInstrument/ClapInstrument.cpp:146-151` carries the comment
"W6: in plain values, as the model's own range is", and the callback reads the plain
model value. `ClapParamModelList::create` in `plugins/ClapEffect/ClapParameter.cpp`
pushes via `setParamPlain(...)` while `pullValues` reads `paramPlain(...)`, and
`ClapParamModel`'s range is the plug-in's own plain min/max
(`plugins/ClapEffect/ClapParameter.cpp:51-60`) — so the model range and the values are
already consistently plain. `rg setParamNormalized plugins/ src/ include/` shows no
product call site in `plugins/`. This landed in commit `d92e54343` ("0.5.0 catch-up
wave 1").

Commit: none (pre-existing, d92e54343)

Test: none

Red/green: none

Leftovers: Cluster 5543's bundle-resolution helper and Cluster 2707's
`static_cast<std::int32_t>(sequence - position)` hardening are real but unfixed —
they need a cross-host design change (5543, touching the VST3 twin too) or a
change with no headless test (2707), both outside this lane. Cluster 5542's
`modulePath.toUtf8()` is a one-line Windows-only change that should go in with a
Windows CI run. Ratchet limits (FILE_LINE_LIMIT=500, TOLERANCE=0) were not touched.
