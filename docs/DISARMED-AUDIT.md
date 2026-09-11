# Built-but-disarmed audit — `post-alpha/integration`

**Scope:** the eight merged lanes on `post-alpha/integration`, asked to prove by source trace whether a
USER can reach five named features, or whether each is wired to nothing.
**Method:** read-only source tracing (`grep`/`git`, no build, no run). Every verdict below is a
source-trace verdict — **the application was not built and was not run.**
**Worktree:** `projects/lmms-fl-research/zene-pa-integration` @ `ccd07f490` (branch `post-alpha/integration`).

## Headline

Two of the five features named for this audit are **not on this branch at all**. They exist only on
their own unmerged lane branches:

| Lane branch | Status vs `post-alpha/integration` |
|---|---|
| `post-alpha/gate-debt`, `post-alpha/docs-security`, `post-alpha/lufs-meter`, `post-alpha/instrument-hosting`, `post-alpha/pr594`, `post-alpha/readme-truth`, `post-alpha/midi-learn`, `post-alpha/clip-capture-spec` | **MERGED** (ancestor of HEAD) |
| `post-alpha/crash-report` | **NOT merged** |
| `post-alpha/plugin-scan` | **NOT merged** |

So the "eight merged lanes" are the eight above; the crash reporter and the plugin scan cache are a
ninth and tenth lane that were never merged here. Their sources do not exist in this tree:
`git ls-tree -r HEAD --name-only | grep -iE "crashreport|pluginscancache|CRASH-REPORTER|PLUGIN-SCAN"`
returns nothing, and the commits are `NOT-ancestor`
(`git merge-base --is-ancestor 98a706dbe HEAD` → NOT-ancestor, same for `023770f7b`, etc.).

## Verdict table

| Feature | Implementation file:line | User entry point | Call sites outside its own test/file | Verdict | Evidence |
|---|---|---|---|---|---|
| **LufsMeter** | `include/LufsMeter.h:83` (`class LMMS_EXPORT LufsMeter`); `src/core/LufsMeter.cpp` | **NONE** | **0** | **DISARMED-DOCUMENTED** | `docs/LUFS-METER.md:37` — "**Nothing calls it.** No mixer, engine, render or device code constructs a `LufsMeter`; the class is inert in the default audio path"; live grep returns no output |
| **MIDI learn** | `include/MidiLearn.h:55` (`class MidiLearn`); `src/core/MidiLearn.cpp:38`; `include/MidiLearnGui.h:44`; `src/gui/MidiLearnGui.cpp:39` | **Edit ▸ MIDI Learn**, `src/gui/MainWindow.cpp:354–359` | **5+** (MainWindow, MidiClient, MidiAlsaSeq) | **REACHABLE** | menu action → `MainWindow::toggleMidiLearn` (`:1291`) → `MidiLearnGui::setArmed` (`MidiLearnGui.cpp:48`) → `MidiLearn::setEnabled` (`MidiLearn.cpp:50`); MIDI input threads call `MidiLearn::handleMidiEvent` (`MidiClient.cpp:244`, `MidiAlsaSeq.cpp:570`) |
| **Crash reporter** | **absent from HEAD** | **NONE on this branch** | n/a | **NOT-ON-BRANCH** | files exist only on unmerged `post-alpha/crash-report` (`98a706dbe`, `36d0155fe`); branch-only trace below |
| **Plugin scan cache + quarantine** | **absent from HEAD** | **NONE on this branch** | n/a | **NOT-ON-BRANCH** | files exist only on unmerged `post-alpha/plugin-scan` (`023770f7b`, `897a22670`, `2e6ae6b56`, `49d29f51f`); branch-only trace below |
| **Session View** (`WANT_SESSION_VIEW`) | `include/SessionModel.h`; `src/core/SessionModel.cpp`; `src/core/SessionClip.cpp` (compiled only when flag ON) | **build flag only** — `OPTION(WANT_SESSION_VIEW … OFF)` at `CMakeLists.txt:110` | **0** (`sessionModel()` accessor is never called) | **DISARMED-DOCUMENTED** | `docs/STATUS.md:31` — "**Not on `main`; no UI.**"; flag genuinely defaults OFF; even ON there is no consumer |

## Per-feature traces

### 1. LufsMeter — DISARMED-DOCUMENTED

Definition: `include/LufsMeter.h:83`, `src/core/LufsMeter.cpp`. Compiled unconditionally into core
(`src/core/CMakeLists.txt:69`), test at `tests/CMakeLists.txt:14`.

Command and output:

```
$ grep -rn "LufsMeter" src/ include/ tests/ --include=*.cpp --include=*.h \
    | grep -v "src/core/LufsMeter.cpp" | grep -v "include/LufsMeter.h" \
    | grep -v "tests/src/core/LufsMeterTest.cpp"
(no output)
```

**Zero call sites outside its own implementation and its own test.** Nothing constructs a `LufsMeter`:
no mixer, engine, render, device or GUI code. There is no meter widget, no readout, no export value.

This is a **documented deferral, not a silent disarm** — the lane's own report states the disarm
explicitly, twice:

- `docs/LUFS-METER.md:5` — "GUI meter is deliberately out of scope."
- `docs/LUFS-METER.md:37` — "**Nothing calls it.** No mixer, engine, render or device code constructs a
  `LufsMeter`; the class is inert in the default audio path."
- `docs/LUFS-METER.md:155–161` — section "**Wiring: provably inert**" reproduces the same empty grep and
  concludes: "it is dead weight in the library until a consumer (the GUI meter, a render report) asks
  for it."

So it is built-and-inert **as designed and said aloud**. No defect. (It is the `RoutingGraph` pattern,
but with the required disclosure.)

### 2. MIDI learn — REACHABLE

The chain from a user action to the new code is complete:

```
Edit menu  →  "MIDI Learn" (checkable QAction)      src/gui/MainWindow.cpp:354-357
           →  MidiLearnGui::instance()->setAction()  src/gui/MainWindow.cpp:358
           →  toggleMidiLearn()                      src/gui/MainWindow.cpp:1291-1294
           →  MidiLearnGui::setArmed(bool)           src/gui/MidiLearnGui.cpp:48-72
           →  MidiLearn::setEnabled(bool)            src/core/MidiLearn.cpp:50-53
           →  qApp->installEventFilter(this)         src/gui/MidiLearnGui.cpp:56
```

While armed, the event filter takes `MouseButtonPress`/`FocusIn` on a `ModelView` widget and stores its
`AutomatableModel` as the learn target (`src/gui/MidiLearnGui.cpp:100-115` → `MidiLearn::setFocusTarget`,
`src/core/MidiLearn.cpp:66`). The MIDI input path then consumes it:

```
$ grep -rn "handleMidiEvent" src/ --include=*.cpp
src/core/midi/MidiClient.cpp:244:   MidiLearn::instance()->handleMidiEvent(m_midiParseData.m_midiEvent);
src/core/midi/MidiAlsaSeq.cpp:570:  MidiLearn::instance()->handleMidiEvent( ccEvent );
```

`MidiLearnGui::instance()` **is** constructed — at `src/gui/MainWindow.cpp:358`, during MainWindow
setup, i.e. on every normal launch. This is not the "class exists, never instantiated" trap; the menu
item is the instantiation site. A successful learn creates a real `MidiController` +
`ControllerConnection` on the focused model (`src/core/MidiLearn.cpp:127-146`) and auto-disarms
(`:149-150`). **A user can turn learn mode on today.**

### 3. Crash reporter — NOT-ON-BRANCH (on this branch it cannot be "disarmed"; it is absent)

```
$ git ls-tree -r HEAD --name-only | grep -iE "crashreport|CRASH-REPORTER"
(empty)
$ git merge-base --is-ancestor 98a706dbe HEAD ; echo $?
1        # NOT an ancestor
$ git branch -a --contains 98a706dbe
+ post-alpha/crash-report
```

`include/CrashReporter.h`, `src/core/CrashReporter.cpp`, `tests/src/core/CrashReporterTest.cpp` and
`docs/CRASH-REPORTER.md` exist only on `post-alpha/crash-report`. On `post-alpha/integration` the feature
is not compiled, not registered in `tests/fork-sources.txt`, and unreachable.

Read-only trace **on its own lane branch** (NOT this branch, not built): it is armed there — `install()`
is called at application startup and the pending-report path is reachable:

```
$ (in zene-pa-crash) grep -n "crashreporter::" src/core/main.cpp
704:  crashreporter::install( ConfigManager::inst()->workingDir().toStdString() );
705:  crashreporter::beginSession();
707:  if( coreOnly && crashreporter::hasPendingReport() )      # headless: offer on stderr
844:  crashreporter::install( ... )
```

So the crash reporter is *not* the disarmed pattern — but it is *not present here*, and if the parent
believed it shipped on this branch, that belief is wrong.

### 4. Plugin scan cache + quarantine — NOT-ON-BRANCH

```
$ git ls-tree -r HEAD --name-only | grep -iE "pluginscancache|PLUGIN-SCAN"
(empty)
$ git merge-base --is-ancestor 023770f7b HEAD ; echo $?
1
$ git branch -a --contains 023770f7b
+ post-alpha/plugin-scan
```

`docs/PLUGIN-SCAN-CACHE.md`, `include/PluginScanCache.h`, `src/core/PluginScanCache.cpp`,
`tests/src/core/PluginScanCacheTest.cpp` exist only on `post-alpha/plugin-scan`. Absent from this tree
and from the ledger.

Read-only trace **on its own lane branch**: there the cache and quarantine ARE consulted by the
discovery path a user triggers, and quarantine IS honoured:

```
$ (in zene-pa-scan) grep -n "m_scanCache" src/core/PluginFactory.cpp
139:  m_scanCache(PluginScanCache::defaultFilePath())
305:  // Re-read the cache on every scan, so an edited quarantine list takes effect
308:  m_scanCache.load();
329:  if (m_scanCache.isQuarantined(file.absoluteFilePath())) { hidden.insert(file); }
345:  const PluginScanRecord* record = m_scanCache.lookup(file);
446/466/497/504/512:  m_scanCache.store(record) ... save()
```

The discovery path is the plugin browser — `src/gui/PluginBrowser.cpp:153-176` (`addPlugins()` →
`getPluginFactory()->descriptors(...)`), reached from `MainWindow.cpp:114` (`new PluginBrowser(...)`,
the sidebar tab). So on that branch the cache is consulted by a user-triggered path.

**Quarantine has no user surface** — only a hand-edited JSON file. The lane says so plainly
(`docs/PLUGIN-SCAN-CACHE.md:161-163`): "`PluginFactory::scanCache()` — the store itself, so a UI (or a
script, or a test) can quarantine a plugin … **Today the documented way to mark a plugin bad is to add
`{"path": …, "reason": …}` to the `quarantine` list** [in the JSON]." That is a documented limitation,
not a silent one.

### 5. Session View (`WANT_SESSION_VIEW`) — DISARMED-DOCUMENTED

The option is genuinely OFF by default — verified from the tree, not repeated:

```
CMakeLists.txt:110:  OPTION(WANT_SESSION_VIEW "Include the Session View data layer (<session> project block, opt-in)" OFF)
CMakeLists.txt:147:  IF(WANT_SESSION_VIEW) → SET(LMMS_HAVE_SESSION_VIEW 1)
src/core/CMakeLists.txt:20-25:  IF(LMMS_HAVE_SESSION_VIEW) → core/SessionClip.cpp, core/SessionModel.cpp
tests/CMakeLists.txt:47-51:     IF(LMMS_HAVE_SESSION_VIEW) → src/core/SessionModelTest.cpp
include/Song.h:334-339:         #ifdef LMMS_HAVE_SESSION_VIEW → SessionModel& sessionModel()
include/Song.h:474-476:         #ifdef LMMS_HAVE_SESSION_VIEW → SessionModel m_sessionModel;
```

So with the default OFF build, `SessionModel` is not compiled and `Song` has no session member at all.
There is genuinely **no UI code** that constructs it:

```
$ grep -rn "sessionModel()" src/ include/ --include=*.cpp --include=*.h
include/Song.h:337:  SessionModel& sessionModel() { return m_sessionModel; }
include/Song.h:338:  const SessionModel& sessionModel() const { return m_sessionModel; }
$ grep -rniE "sessionview|session_view|clipLaunch|scenes" src/ include/ --include=*.cpp --include=*.h | grep -vE "SessionModel|WANT_SESSION_VIEW|LMMS_HAVE_SESSION_VIEW"
src/core/SessionClip.cpp:2: * SessionClip.cpp - ClipSlot and Scene serialisation (Session View)
```

`sessionModel()` has **zero callers** — not even a persistence path in `Song`/`Engine`. Therefore the
data layer is disarmed **twice over**: (a) compiled out by the default-OFF flag, and (b) even with
`-DWANT_SESSION_VIEW=ON`, nothing consumes the model; there is no scene launcher, no clip grid, no
serialisation caller.

This matches the lane's own disclosure — `docs/STATUS.md:29-33`: "**Session View data layer** —
`SessionModel` / `ClipSlot` / `Scene`, versioned `<session version="1">` XML behind `WANT_SESSION_VIEW`
(default **OFF**). Branch `feat/session-view-model`, PR #5 / task #594, 1677 insertions across 15 files.
**Not on `main`; no UI.**" **DISARMED-DOCUMENTED.**

## Integration check — `tests/fork-sources.txt`

The ledger must list every new source. Using the ledger's own documented baseline (`4e677cb6c`, per the
file header) rather than a ref:

```
$ git diff --name-only --diff-filter=A 4e677cb6c HEAD -- src include plugins \
    | grep -E '\.(cpp|c|h|hpp|cc|cxx)$' \
    | grep -vE '^(src/3rdparty/|plugins/NeuralAmp/rtneural/|plugins/NeuralAmp/nam/|plugins/NeuralAmp/tests/|plugins/RnnoiseDenoiser/rnnoise/)' \
    | sort > /tmp/regen2.txt        # 109 files
$ comm -23 /tmp/regen2.txt /tmp/ledger.txt   # added-but-unregistered
(empty)
```

**Every fork-added source is registered — zero unregistered sources.** No ledger defect of the
previously-recorded class.

Ledger-hygiene note (not a defect): the ledger's documented regeneration command uses `origin/master`,
but the local `origin/master` ref is stale (`518a7e8ef`) relative to the documented base `4e677cb6c`.
Run as written it reports seven **pre-existing upstream** files as if unregistered — `include/AudioOss.h`,
`include/MidiOss.h`, `include/AudioSoundIo.h`, `plugins/Sf2Player/fluidsynthshims.h`,
`src/core/audio/AudioOss.cpp`, `src/core/audio/AudioSoundIo.cpp`, `src/core/midi/MidiOss.cpp`. All seven
are present in `4e677cb6c` (`git ls-tree 4e677cb6c --name-only include/AudioOss.h …` returns them) and
are correctly absent from the ledger. The regeneration command should be pinned to the baseline commit,
not to a moving remote ref. (`tools/local-ci.sh` is listed in the ledger but is not a `src/include/plugins`
source, so the command's own output does not cover it — also not a defect.)

## Defects found

1. **Branch-composition defect (process, not code):** two of the five audit targets — the crash reporter
   and the plugin scan cache — are **not merged** into `post-alpha/integration`. They live on
   `post-alpha/crash-report` and `post-alpha/plugin-scan`. Auditing them "as merged on this branch" is
   impossible; a reviewer reading this branch will not find them. If the parent's lane accounting assumed
   ten merged lanes, the count is eight.
2. **No DISARMED-SILENT feature found among the three targets actually present on the branch.** LufsMeter
   and Session View are disarmed *and say so* (quoted above); MIDI learn is reachable.
3. **No unregistered source.** `tests/fork-sources.txt` is complete against its documented baseline.

## UNPROVEN

Because no build or run was performed, the following could not be established:

- **MIDI learn, runtime efficacy of the event filter.** The filter relies on controls inheriting both
  `QWidget` and `ModelView` so a single `dynamic_cast<ModelView*>` catches every control type
  (`src/gui/MidiLearnGui.cpp:104-115`). The *reachability* of learn mode is proven from source; whether
  every control widget in the app is actually caught by that cross-cast (and thus bindable) is a
  source-assertion, not a run. Would need: a launch, arm MIDI Learn, and a click/keyboard-focus on a
  range of control types with a hardware/virtual CC.
- **Crash reporter, the two offer paths.** On its own branch the GUI "a previous run crashed" offer and
  the headless `coreOnly` stderr offer (`src/core/main.cpp:707-717`) are traced but not exercised.
  Would need: a build of `post-alpha/crash-report`, a deliberate crash, and a second launch.
- **Plugin scan cache, end-to-end cache/quarantine effect.** That `PluginBrowser` is the user-triggered
  discovery path is traced (`src/gui/PluginBrowser.cpp:153-176` → `PluginFactory::descriptors`), but
  whether a quarantine entry actually hides a plugin in the running browser was not run. Would need: a
  build of `post-alpha/plugin-scan`, a planted plugin, a quarantine entry, and a browser reload.
- **Session View, `-DWANT_SESSION_VIEW=ON` behaviour.** That the flag compiles the model in, and that it
  persists the `<session>` block, was not run in either state. Would need: a build with the flag ON and a
  save/load round-trip.
- **LufsMeter never affects a render.** The "no consumer" claim is proven by grep, not by a
  behaviour-preserving render diff. Would need: two renders (linked in / not) and a null A/B.

---

*Read-only audit. The application was neither built nor run; every verdict above is from source tracing
with `grep`/`git` in `zene-pa-integration` @ `ccd07f490`. No file other than this report was created or
modified. Supplementary traces in §3 and §4 were read from the separate worktrees `zene-pa-crash` and
`zene-pa-scan` and are labelled as not-on-this-branch.*
