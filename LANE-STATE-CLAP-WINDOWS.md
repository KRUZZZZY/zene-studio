# LANE STATE — CLAP hosting on Windows and the loader's typed error path (task #667, advertised-features row `clap-hosting`)

Branch `030/clap-windows`, worktree `…/zene-030/wclapwin`, reset to `3fae5a5e1` by the parent. Seven
commits on top of it, all local (no push): `b86667938`, `f8eba6ec5`, `a7960227f`, `51c0cdb4f`, `ef36dbe38`,
`f02a507fc`, `95107fa84`.

**Read this first: the Windows port itself was already in the tree at the base.** Commit `714ffa37f`
("clap: port the CLAP module loader to Windows (LoadLibraryW/GetProcAddress)", 2026-09-13) landed the
`#ifdef _WIN32` loader half, `-DWANT_CLAP=ON` on all three Windows jobs and the `clap-hosting` row back to
`*`, and it is an ancestor of `3fae5a5e1`. This lane did not re-do it; it verified it against the gate and
the workflow, and delivered the half that was still missing: **the typed error path**, its **test**, its
**proof**, and the honest **documents**.

## 1. What is in the tree now

| file | state | what it carries |
|---|---|---|
| `plugins/ClapEffect/ClapLoader.h` | **new** (200 lines) | `loader::Code` (11 codes + `Count`), `loader::Status` (code + detail + `token()`/`summary()`/`message()`), `loader::Module` (opaque, movable, RAII), `loader::Library` (module + entry + factory, `reset()` = deinit-then-close), `loader::load()` — the ladder open → `clap_entry` → version → `init()` → factory, with a typed answer at every step. |
| `plugins/ClapEffect/ClapLoader.cpp` | **new** (287 lines) | The platform boundary: POSIX `dlopen`/`dlsym`/`dlclose`/`dlerror` **verbatim** (same call text, same `RTLD_NOW \| RTLD_LOCAL`), Windows `LoadLibraryW`/`GetProcAddress`/`FreeLibrary`/`FormatMessageW` on the wide path. Plus the one row per code table behind `token()`/`summary()` (`static_assert` ties its row count to `Code::Count`). |
| `plugins/ClapEffect/ClapHost.cpp` | edited (**it shrank**: 1001 → 890 lines) | The local loader block is gone (it is ClapLoader's now); `load()` records a typed code for the plug-in-level failures too; `unload()` unwinds through `Library::reset()`; `listClasses()` uses the same ladder and reports the code through a new overload. Under its file-length baseline of 953 again. |
| `plugins/ClapEffect/ClapHost.h` | edited | `HostedPlugin::lastLoadFailure()`; the typed `listClasses(modulePath, loader::Status*, QString*)` overload. |
| `plugins/ClapEffect/CMakeLists.txt` | edited | `ClapLoader.cpp` joins the plug-in sources. |
| `tests/data/clap-test-plugin/clap-test-broken.c` | **new** (152 lines) | ONE source, four fault modules (`no-symbol`, `old-version`, `init-fails`, `no-factory`), each fault structural, `#error` if compiled with no fault. |
| `tests/data/clap-test-plugin/CMakeLists.txt` | edited | Builds the four fault modules with the same `MODULE`/`PREFIX ""`/`SUFFIX ".clap"` shape and `-Wall -Wextra` guard as `clap-test-gain`. |
| `tests/src/plugins/ClapLoaderErrorTest.cpp` | **new** (390 lines) | 14 assertions: each failure's code, the six failures distinct as a set, the whole token/summary table one-to-one, scan ≡ instance for the same module, the sentences unchanged, and a good module gives `Code::None`. `QTEST_GUILESS_MAIN`; no audio device, no widgets, no engine. |
| `tests/CMakeLists.txt` | edited | Registers the ctest `ClapLoaderErrorTest` (+ coverage instrumentation); adds `ClapLoader.cpp` to `ClapHostTest` and `ClapEffectIntegrationTest`; comment records WHICH CI job runs it. |
| `tests/prove-clap-loader-unchanged.sh` | **new** (231 lines) | The local proof, 5 claims (below). Its last lines say, unconditionally, what it has NOT measured. |
| `tests/advertised-features.tsv` | edited (notes only) | The **data row is unchanged** (`clap-hosting  WANT_CLAP  ON  clapeffect  …  *`); two sentences record what the typed path added and repeat that nothing is locally verified. |
| `tests/fork-sources.txt`, `tests/all-sources.txt` | edited | Regenerated from their own header recipes (`REPRODUCES` verified for both). |
| `tests/upstream-modifications.txt` | edited | The `tests/CMakeLists.txt` entry carries this lane's reason (trailing newline and `#`-prefixed notes left intact). |
| `docs/KNOWN-LIMITATIONS.md`, `docs/RELEASE-NOTES-v0.3.0-alpha.md` | edited (appended) | The row's honest statement: built on all three platforms, the codes, the proof, the two bounds, the UI-absence line. |

## 2. What was PROVEN locally, with the command

```
$ bash tests/prove-clap-loader-unchanged.sh            # -> CLAP-LOADER PROOF: PASS (0 skipped) EXIT=0
  COMPILES   ClapLoader.cpp / ClapLoader.h / ClapHost.cpp   (borrowed real flags, -fsyntax-only, -Werror)
  IDENTICAL  the five POSIX loader lines vs release/0.3.0   (call text AND mode flags, whole lines)
  ONE-PLACE  ClapHost.cpp has 0 loader calls; the Windows half carries all four APIs
  GUARDED    ClapLoader.cpp contributes 0 Windows-only lines to a POSIX build
  BUILDS     all four one-fault fixtures; REFUSES to compile with no fault macro
```

A **real CMake build** of the CLAP targets (not a hand-rolled compile):

```
$ cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DWANT_QT6=ON -DWANT_CLAP=ON \
      -DLMMS_CLAP_PATH=<program>/vendor/clap-195b42a0 -DWANT_VST3=OFF -DWANT_WASM=OFF \
      -DWANT_STEM_SPLIT=OFF -DWANT_CARLA=OFF -DWANT_CALF=OFF -DWANT_SOUNDIO=OFF -DWANT_VST=OFF \
      -DUSE_WERROR=ON -DUSE_COMPILE_CACHE=ON                    # CONFIGURE EXIT=0 (54 s)
$ cmake --build build --target ClapLoaderErrorTest ClapHostTest -j2   # BUILD EXIT=0
$ cd build/tests && ctest -R "ClapLoaderErrorTest|ClapHostTest" --output-on-failure
    1/2 ClapHostTest ......... Passed   (12 assertions, the pre-existing host test — regression check)
    2/2 ClapLoaderErrorTest .. Passed   (14 assertions, the new typed-error test)
    100% tests passed, 0 tests failed out of 2                 # CTEST EXIT=0
    ctest -N in that tree: 176 tests registered (not 0 — the CMake wiring is live)
```

Gates (fork scope, `tests/run-all-gates.sh --no-mutation`): **PASS** on 3 no-tautology, 6
upstream-regression, 8 duplication, 9 fork-sources, 10 unregistered-tests, 11 evidence; **FAIL** on 4
complexity and 7 file-length — both from **inherited** reds (DawProject*/Wasm*, `ControlRegistry*.h`,
`Vst3Host.cpp`, `control-stable-ids-slice2.py`, …), and **no REGRESSION line in either gate names a file
this lane touched** (`HostedPlugin::load` measures CCN 26 against its baseline entry 31 — improved; and the
two switch functions this lane first wrote were replaced by a table after gate 4 flagged them, so nothing
here needs a re-anchor). Skips: gate 1 (no build dir), 2 (coverage), 5 (mutation).

Release-honesty gate, this row, both platform directions (release-shaped header = the Linux jobs'
`-DWANT_CLAP=ON` set, `WANT_WASM=OFF`):

```
$ bash tests/release-honesty-gate.sh --header <release-shaped>/lmmsversion.h   -> PASS 6/6, EXIT=0 (linux)
$ RELEASE_HONESTY_PLATFORM=windows bash tests/release-honesty-gate.sh --header <same>  -> PASS 6/6, EXIT=0
  [PASS] clap-hosting     ON matches ON           (both directions)
```
Controls (run in /tmp against a copy of the gate — the repo manifest was never edited for them):
scoping the row to `linux,macos` with `WANT_CLAP='ON'` on `RELEASE_HONESTY_PLATFORM=windows` → **FAIL**
("…documents the feature as absent there"), and the row at `*` with `WANT_CLAP='OFF'` → **FAIL** ("requires
ON"). The row is live in both directions; nothing here weakens it.

Against this tree's own build options the same gate prints `[PASS] clap-hosting ON matches ON` on linux and
windows with `-DWANT_CLAP=ON` coming from the local configure; its overall FAIL on that header is the two
VST3 rows, because this lane's local configure passed `-DWANT_VST3=OFF` (no SDK provisioned on this box).

## 3. What is CI-ONLY (no local green exists for these)

* The `#ifdef _WIN32` branch of `ClapLoader.cpp` **has never been compiled here** — no MinGW, no MSVC.
  All three Windows jobs configure with `-DWANT_CLAP=ON` (`build.yml`: mingw64, msvc-x64,
  windows-arm64/msys2) and build the clapeffect module.
* Of those three, **msvc-x64 is the only one that runs `ctest`**, so it is the only Windows job where a
  module is actually put through the loader and `ClapLoaderErrorTest` runs against the Windows half. The
  other two compile it and their release-honesty guard asserts the module exists in their artifacts.
  No third-party CLAP plug-in has been loaded on any platform; the witness is our own MIT fixture.
* Gate 5 (mutation) and gate 1 (ctest via CMake's own gate runner) were skipped: 5 by `--no-mutation`, 1
  because this lane's build tree was deleted at the end of the pass (93 MB, recreatable in ~1 minute with
  the configure line in §2).

## 4. Single next action

Re-run the suite and the gates on the merged tip (the honest `clap-hosting` row and `ClapLoaderErrorTest`
both need a real CI run to become a Windows green; nothing in this lane claims one). If a socket-visible
half is wanted next: the typed code is reachable from a caller today (`HostedPlugin::lastLoadFailure()`,
`listClasses(path, &status, &error)`) but **no command reports it yet** — the natural follow-up is a `code`
field on the plugin-scan result, which touches the capped registry/A16 area and was deliberately left out
of this pass.
