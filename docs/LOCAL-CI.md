# Local CI — reproducing the build matrix on one machine, honestly

Status: implemented and verified 2026-09-11 (task `#616`, Bar 1 of the v0.1 alpha gate).
One command, one summary, real exit codes. Verified green on this machine: `configure EXIT=0`,
`build EXIT=0`, `ctest EXIT=0`, `100% tests passed, 0 tests failed out of 24`, `overall exit=0`
— and red with the injected probe (both runs captured below). The script is `tools/local-ci.sh`;
this document says what it runs, what it catches, and — the part that matters — **which of the
seven CI jobs this machine cannot reproduce and how to classify a failure those jobs report**.

Background: the CI campaign (runs 8–22) burned roughly twenty job-cycles because most
failures were only visible on a runner. Two of the recurring classes reproduce locally
within minutes *once the CI's exact flags are used* — that is the gap this closes. The
rest cannot be reproduced on a Linux box at all and are classified, not guessed at.

---

## The command

```bash
tools/local-ci.sh                  # configure + build + ctest, linux-x86_64 configuration
tools/local-ci.sh --configure-only # cheapest: flags/dependencies only (~seconds, cached)
tools/local-ci.sh --no-build       # skip the compile; run ctest against an existing build
tools/local-ci.sh --jobs 4         # compile parallelism (default 4; the CI's cap is -j2)
tools/local-ci.sh --build-dir DIR  # build directory (default: build-ci)
```

- Exit status is **0 only if every step that ran passed**. `ctest` reporting 0 tests is
  treated as an ERROR, never a pass (workspace rule 7); the top-level build directory has
  no `CTestTestfile.cmake`, tests always run from `<build>/tests`.
- Every exit code is measured unpiped (`cmd > log 2>&1; echo EXIT=$?`, workspace rule 6);
  logs land in `build-ci/{configure,build,ctest}.log`.
- Every invocation prints the per-job coverage block below, whether or not it runs the
  full three steps.

### Exactly what it runs

`CI_CMAKE_OPTS` is the `linux-x86_64` job's `CMAKE_OPTS` from `.github/workflows/build.yml`,
byte for byte:

```
-DUSE_WERROR=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTARGET_UARCH=official
-DUSE_COMPILE_CACHE=ON -DWANT_DEBUG_CPACK=ON
```

then `cmake --build build-ci -j4` (the CI uses `MAKEFLAGS: -j2`; this workspace caps at
`-j4` because sibling lanes build concurrently — AGENTS.md rule 2), then from
`build-ci/tests`: `ctest --output-on-failure -j2` (exactly the CI's test command).

Not run locally, by design: the fltk-from-source step, wine, `--target package`
(AppImage/NSIS/dmg) and artifact upload. Those are packaging steps, not signal for code
regressions, and they are exactly the part of the matrix that is platform-specific.

`TARGET_UARCH=official` means `-march=x86-64-v2` on this host (CMakeLists.txt:172–179);
`USE_WERROR` means `-Wall -Werror` for first-party code and `-w` for targets marked
`SYSTEM` (the vendored-code treatment, cmake/modules/ErrorFlags.cmake).

---

## Proof it is not a no-op

The acceptance test for `#616`: inject a deliberate `-Werror`-triggering warning into a
first-party source file, run the documented command, and observe the build step fail.

Reproduce it yourself (the probe is reverted afterwards; nothing is committed):

```bash
# 1. inject: an unused local variable is a -Wall warning, promoted by -Werror
#    (the same mechanism that killed runs 14-20 in vendored and first-party code)
$ sed -i 's|void OscilloscopeGraph::mousePressEvent(QMouseEvent\* me) { m_mousePos = lmms::position(me).x(); }|void OscilloscopeGraph::mousePressEvent(QMouseEvent* me) { int zene_ci_probe_unused = 0; m_mousePos = lmms::position(me).x(); }|' \
    plugins/Oscilloscope/OscilloscopeGraph.cpp

# 2. run the documented command and capture the unpiped exit code
$ bash tools/local-ci.sh > /tmp/ci-injected.log 2>&1; echo EXIT=$?

# 3. after capturing, revert
$ git checkout -- plugins/Oscilloscope/OscilloscopeGraph.cpp
```

**Captured proof** — this machine, 2026-09-11. `build-ci/` was warm, so only the injected
translation unit recompiled: the run takes ~40 s and stops at the build step, which is why
ctest never runs on this one.

```
$ bash tools/local-ci.sh > /tmp/ci-injected.log 2>&1; echo EXIT=$?
EXIT=1

--- [2/3] build (cmake --build build-ci -j4) ---
build EXIT=2   (log: build-ci/build.log)
--- build failed; last 30 lines: ---
...
/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-ci/plugins/Oscilloscope/OscilloscopeGraph.cpp:168:64: error: unused variable ‘zene_ci_probe_unused’ [-Werror=unused-variable]
  168 | void OscilloscopeGraph::mousePressEvent(QMouseEvent* me) { int zene_ci_probe_unused = 0; m_mousePos = lmms::position(me).x(); }
      |                                                                ^~~~~~~~~~~~~~~~~~~~
cc1plus: all warnings being treated as errors
gmake[2]: *** [plugins/Oscilloscope/CMakeFiles/oscilloscope.dir/build.make:130: plugins/Oscilloscope/CMakeFiles/oscilloscope.dir/OscilloscopeGraph.cpp.o] Error 1
gmake[1]: *** [CMakeFiles/Makefile2:7185: plugins/Oscilloscope/CMakeFiles/oscilloscope.dir/all] Error 2
gmake: *** [Makefile:156: all] Error 2
```

The same tree with the probe reverted, run the same way:

```
configure EXIT=0   (log: build-ci/configure.log)
build EXIT=0   (log: build-ci/build.log)
ctest EXIT=0   (log: build-ci/ctest.log)
ctest totals: 100% tests passed, 0 tests failed out of 24
local-ci: overall exit=0 (0 = every executed step passed)
```

That pair is the proof: red when a first-party warning is injected, green when it is not — so a
green run is evidence, not a no-op.

---

## Coverage table — this machine vs. the matrix

Machine the table was produced on: Linux x86_64, Ubuntu 24.04.4 LTS, gcc 13.3.0,
cmake 3.28.3, Qt 6.4.2 (no Qt5 dev, no sudo), ccache 4.14 (installed under
`~/.local/bin`, see *Deviations*).

| job (build.yml) | runner | status here | reason / closest proxy | classification rule for a failure it reports |
|---|---|---|---|---|
| `linux-x86_64` | ubuntu-22.04 | **REPRODUCED** (with documented deviations) | — | A red linux-x86_64 is a *real* failure: rerun `tools/local-ci.sh`. Same TU, same flags, same compiler family — if it's red in CI it should be red here. |
| `linux-arm64` | ubuntu-24.04-arm | **NOT-REPRODUCIBLE-HERE** | x86_64 host and no aarch64 cross toolchain. Proxy: none faithful; arm64-only diagnostics (e.g. Eigen NEON `-Wdeprecated-enum-enum-conversion`, run #14) cannot fire. | Platform-specific **only if** the log names an arm64-only construct (NEON/intrinsics, `__aarch64__`, size/type widths). Otherwise the same TU compiles on x86_64 — try `tools/local-ci.sh` before touching code. |
| `mingw64` | ubuntu-latest + vcpkg | **NOT-REPRODUCIBLE-HERE** | no `x86_64-w64-mingw32-g++` on this box; the job needs the vcpkg manifest and `cmake/toolchains/x64-mingw-vcpkg.cmake`. Proxy: none installed. | Classify as *non-Unix linker semantics*: unresolved symbols without a Unix linker's tolerance, `-Wa,-mbig-obj`-scale objects, export-import modelling. Guards, then CI verifies. |
| `macos-x86_64` | macos-15-intel | **NOT-REPRODUCIBLE-HERE** | Darwin toolchain + Xcode SDK. Proxies: none for SDK floors; for pure C++ standard issues, compiling the same TU under `tools/local-ci.sh` catches the portable half. | Darwin-only library floors (`std::filesystem` availability before macOS 10.15), libc++ vs libstdc++ behaviour, brew link paths (`ld: library not found`). Platform-specific when the log names one of these; else reproduce here. |
| `macos-arm64` | macos-15 | **NOT-REPRODUCIBLE-HERE** | as above, plus Apple-Silicon layout/inlining differences. | as above. |
| `msvc-x64` | windows-2022 | **NOT-REPRODUCIBLE-HERE** | MSVC toolchain required. Proxy: compile the same TU with gcc/clang locally — this separates *standard C++ breakage* (reproducible here) from *MSVC-isms* (not). | MSVC-isms: `/WX` + `/W2` severity differences; **no libm** (MSVC keeps math in the CRT — a bare `-lm`/`m` link fails with `LNK1104: cannot open file 'm.lib'`); **import-library semantics** (`LNK2005`/`LNK1169` duplicates from exported STL instantiations); `C3861 'visibility'` from bare `__attribute__`; `/bigobj`-scale objects. |
| `windows-arm64` | windows-11-arm (msys2 CLANGARM64) | **NOT-REPRODUCIBLE-HERE** | Windows 11 ARM64 + msys2/clangarm64 required. Proxy: none. | Path-generator class: CPack/NSIS path handling where `GET_FILENAME_COMPONENT` yields backslashes — a strip that works with forward slashes leaves an absolute path and the installer dies with "file cannot create directory … Maybe need administrative privileges". Platform-specific, always. |

The same lines are printed live by every run of the script (the reason strings are
*probes* — `uname`, `command -v x86_64-w64-mingw32-g++`, `command -v cl.exe` — not
hard-coded guesses).

---

## Worked examples — four real failures from runs 17–22

All four are quoted from the job logs as recorded in the fixing commits; the point of the
table is which of them the local reproduction could have caught **before** a push.

| # | run / job | symptom (from the job log) | class | would `local-ci.sh` have caught it? |
|---|---|---|---|---|
| 1 | #17 · `mingw64` | `ExprSynth.cpp.obj: 'file too big'` — exprtk past the assembler's section limit | cross-target object-format limit | **No.** Needs the mingw cross toolchain and `-Wa,-mbig-obj`. Fixed blind (`/bigobj` on MSVC, big-obj on mingw), verified by CI. |
| 2 | #17 · `macos-x86_64` | `'create_directories' is unavailable: introduced in macOS 10.15` (TwoTrackRecordingHarness) | Darwin SDK deployment floor | **No.** The fix (`::mkdir` on Apple, `std::filesystem` elsewhere) is verifiable only on Apple SDKs. |
| 3 | #21 · `msvc-x64` | `LINK : fatal error LNK1104: cannot open file 'm.lib'` | Unix-only libm | **No.** MSVC has no libm; the guard is `IF(NOT MSVC)`. CI-only class. |
| 4 | #22 · `msvc-x64` | `LNK2005: std::vector<float>::~vector(...) already defined in NamModelLoader.cpp.obj` … `LNK1169: one or more multiply defined symbols found` | MSVC import-library/STL export collision | **No, not the failure itself** — but the *reasoning* is checkable here: the export surface (`LMMS_EXPORT` + `std::vector<float>` in inline members) is visible in the source; the diagnostics are MSVC-only. |

The same window contains the two classes the local script **does** close, and one Linux
failure it reproduces outright:

- #17/#19/#20 · `msvc-x64`/`linux` — vendored RNNoise diagnostics (`C4305`, `C4244` at
  `rnnoise/vec.h:324`; `#warning` promoted by `-DUSE_WERROR` to `[-Werror=cpp]` on Linux).
  This is a compiler-warning class and *is* reproducible on Linux with the CI's flags —
  the injected-warning proof above is exactly this mechanism.
- #17/#18 · `linux-x86_64` (ctest) — `Lv2Effect did not instantiate
  http://drobilla.net/plugins/mda/Ambience`: the runner lacked the `mda-lv2` bundle the
  tests render through. This is a **ctest red on Linux**, i.e. precisely what the script's
  third step runs. (The fixture-skip defence now makes a bundle-less runner pass cleanly;
  this box has `mda-lv2`, so the LV2 checks actually run.)
- #18 · `linux-x86_64` — `E: Unable to locate package #` from a comment line in
  `deps-ubuntu-24.04-gcc.txt` consumed by `xargs`. A dependency-list regression; the
  configure step here would not see it (the local script installs nothing), but the
  `--configure-only` mode at least proves the *flags* still configure on a machine whose
  packages are known-good.
- **This script's own first full run** — three first-party translation units failed to
  compile under `-DUSE_WERROR=ON` + `-DWANT_QT6=ON`:

  ```
  src/gui/editors/PianoRoll.cpp:2865:34: error: ‘int QMouseEvent::y() const’ is deprecated: Use position() [-Werror=deprecated-declarations]
  src/gui/editors/PianoRoll.cpp:2866:30: error: ‘int QMouseEvent::x() const’ is deprecated: Use position() [-Werror=deprecated-declarations]
  plugins/Oscilloscope/OscilloscopeGraph.cpp:167:78: error: ‘int QMouseEvent::x() const’ is deprecated: Use position() [-Werror=deprecated-declarations]
  tests/reference/Oscilloscope/OscilloscopeGraph.cpp:171:98: error: ‘int QMouseEvent::x() const’ is deprecated: Use position() [-Werror=deprecated-declarations]
  ```

  Qt 6 deprecates `QMouseEvent::x()`/`y()`/`globalPos()`/`localPos()`/`windowPos()`/
  `screenPos()` (`pos()` is *not* deprecated — the two are not the same) in favour of
  `position()`. This class is **invisible to CI**: the six Qt5 jobs never compile those
  headers' Qt6 branch, and `msvc-x64` compiles Qt 6.8 with `/external:anglebrackets`
  (cmake/modules/ErrorFlags.cmake), so MSVC treats Qt's angle-bracket headers as external
  and never reports the deprecation at the call site. A gcc/Qt6 `-Werror` build is the only
  configuration on this project that sees it. Fixed in the tree with the repository's own
  adapter (`lmms::position()` in `include/DeprecationHelper.h`, the same one upstream uses):
  the two product call sites were rewritten; the third is in the frozen Part C reference
  tree (`tests/reference/`, verbatim copies with blob ids in `ORIGIN.tsv`) where editing the
  file would destroy the evidence the harness compares against, so the `partc_ref_*` targets
  - and only they - compile with `-Wno-deprecated-declarations` (see `tests/CMakeLists.txt`).

---

## Deviations on this machine (read before trusting a green run)

1. **Qt6 instead of Qt5.** The CI's `linux-x86_64` runner installs `qtbase5-dev` and the
   workflow passes no `WANT_QT6` flag, so CI configures Qt5. This box has **no Qt5
   development files and no sudo**. Running the exact flags without a Qt flag fails with:

   ```
   CMake Error at CMakeLists.txt:315 (find_package):
     Could not find a package configuration file provided by "Qt5" (requested
     version 5.15.0) with any of the following names: Qt5Config.cmake, qt5-config.cmake
   ```

   The script therefore auto-adds `-DWANT_QT6=ON` (which is how this fork is developed,
   and what task `#616`'s spec lists) and prints it under the coverage line as a
   deviation. Everything else in the flag set is untouched.

   This deviation has a consequence worth knowing before trusting a red run: **Qt 6
   deprecates APIs that neither CI configuration reports.** The Qt5 jobs never see the Qt6
   branch of those headers, and `msvc-x64` (Qt 6.8 + `/external:anglebrackets`) never sees
   them either. The full list of what this box found on its first run, and the fix, is the
   last worked example below — all three sites are already fixed, so a red run from this
   class now means new code, not old code.
2. **gcc 13.3 instead of the runner's gcc 11.4.** Newer compilers have newer warnings: a
   *red* run here can in principle be a warning gcc-11 does not emit, and vice versa.
   Treat a warning that only gcc-13 emits as a finding to classify, not automatically as
   a CI failure.
3. **ccache.** `-DUSE_COMPILE_CACHE=ON` without ccache is a warning, not an error
   (`cmake/modules/CompileCache.cmake` prints `USE_COMPILE_CACHE enabled, but no ccache
   found` and continues). The recorded runs used a static ccache 4.14 placed in
   `~/.local/bin` without root:
   `curl -L …/ccache-4.14-linux-x86_64-musl-static.tar.gz | tar xz; install -m755 ccache ~/.local/bin/`.
4. Host is Ubuntu 24.04 vs the runner's ubuntu-22.04 (glibc and library versions differ).
5. The build is capped at `-j4` and a full cold build takes a while; `build-ci/` is
   gitignored (`/build*/`).

None of these affect the *warning policy* — `-Wall -Werror`, `RelWithDebInfo`,
`-march=x86-64-v2`, the `SYSTEM`-target third-party treatment — which is the part that
catches the recurring classes.

---

## Classifying a red job without pushing (procedure)

1. **linux-x86_64 red →** rerun the documented command. If it reproduces, it is a real
   failure of the code, fix and re-verify locally. If it does not reproduce, the
   deviation list above is where the difference lives (Qt version, gcc version).
2. **Any other job red →** read the failure mechanism from the log *before* touching code,
   then pick the row from the classification rules in the coverage table. The decision is:
   does the log name something this platform family does not have (libm, Darwin SDK floor,
   MSVC linker/import-library semantics, backslash path generators, an arm64-only
   construct)? If yes → platform-specific; write the portable guard and expect CI to
   verify it (state that in the commit, as the run 17–22 fixes do). If no → treat it as a
   portable failure and reproduce it here with the same flags first.
3. **Never push a guess.** Every one of the four worked examples above was fixed from its
   own log with the mechanism named in the commit message; the ones that could not be
   reproduced locally say so explicitly in the commit ("CI is the verifier for those").

## Maintenance

- If `build.yml`'s `CMAKE_OPTS` change, update `CI_CMAKE_OPTS` in `tools/local-ci.sh`
  (single source) and this document.
- If a matrix job is added or a probe becomes available on this box (e.g. a mingw
  toolchain is installed), add/adjust the coverage line in the script — the reasons are
  probe-driven, so they update by themselves once the tool exists.
- If a new failure class is classified, add it to the classification column; that column
  is the deliverable for the jobs this machine cannot run.
