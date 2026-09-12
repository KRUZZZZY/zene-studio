# The v0.2.0-alpha CI failures: causes, fixes, and what verified them

Tag `v0.2.0-alpha` (commit `b099fd6cb41394ebf364c755b227ee9980e24970`) turned
**every job red**: `build.yml` 7/7 and `checks.yml` 1/3.

- `build.yml` run [34708003925](https://github.com/KRUZZZZY/zene-studio/actions/runs/34708003925)
- `checks.yml` run [34708003952](https://github.com/KRUZZZZY/zene-studio/actions/runs/34708003952)
  — only `scripted-checks` failed, at the `Run check-namespace` step (`shellcheck`
  and `yamllint` passed).

The failure was **five distinct causes, not three**, and two of them are not what
the triage brief assumed. Each job's raw log is kept in
`tests/integration-logs-ci-fix/` (`*.clean.log`, ANSI-stripped) with the harnesses
that reproduced or exercised every fix; read `tests/integration-logs-ci-fix/README.md`
first.

| # | jobs | cause | fix | how it was verified |
|---|---|---|---|---|
| 1 | `linux-x86_64`, `linux-arm64` | `ConfigManager.cpp:700`: `qWarning() << …` with `QDebug` only forward-declared | `#include <QDebug>` by name, plus a ledger reason | red/green probe **+** the file compiles in its real target under `-DUSE_WERROR=ON` |
| 2 | `macos-arm64` | the fork-vendored `include/fenv.h` pokes `fenv_t.__control/__mxcsr`, which only exist in Darwin's *x86_64* `fenv_t` | a separate arm64 branch over `__fpcr`, arch-guarded | compiled **and run** against Darwin's own arm64 declarations; x86_64 branch proven byte-identical to the pre-fix text |
| 3 | `mingw64` | `FIND_PATH … HINTS <build>/clap/include` is re-rooted by the MinGW toolchain's `CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY`, so the provisioned checkout is invisible | `NO_CMAKE_FIND_ROOT_PATH` on that one call | the real pre-fix module fails / the fixed module configures, both under the MinGW find policy **— CI still must confirm on Windows** |
| 4 | `msvc-x64`, `windows-arm64` | `cmake/install/CMakeLists.txt:28` still passed `TARGETS lmms`; the rename wave made the executable target `zene` | `TARGETS zene`, plus a ledger reason | the real module rejects `lmms` / accepts `zene` locally **— CI still must confirm on Windows** |
| 5 | `macos-x86_64` | the VST3 SDK's `module_mac.mm` refuses to build without ARC | `-fobjc-arc` on that one source, the SDK's own idiom | **not verifiable here** — read from the SDK's own sample host CMakeLists; CI is the verifier |

---

## 1. `linux-x86_64` + `linux-arm64` — `ConfigManager.cpp:700` needs `<QDebug>`

```
src/core/ConfigManager.cpp: In member function ‘void lmms::ConfigManager::saveConfigFile()’:
src/core/ConfigManager.cpp:700:33: error: invalid use of incomplete type ‘class QDebug’
  700 |                         qWarning() << title << message;
```

**Cause.** `saveConfigFile()` gained an unattended branch (the `#625` headless-load
divergence) that logs instead of opening a `QMessageBox`. `<QtGlobal>` declares
`class QDebug;` and provides the `qWarning()` macro, but the class is only
*defined* by `<QDebug>`; the file's other Qt includes happened to bring it in
before. **The brief's premise that this was a `-Werror`-promoted warning under
Qt6 is wrong on both counts**: the diagnostic is a hard error, not a warning, and
the failing jobs build **Qt5** — their configure step runs
`source /opt/qt5*/bin/qt5*-env.sh` and reports `Experimental Qt6 support :
Disabled`, and the error's own include chain names
`/usr/include/x86_64-linux-gnu/qt5/QtCore/qglobal.h`. CI's Qt6-vs-Qt5 split is
the whole reason this reproduces nowhere else (the macos/US jobs are Qt6, where
`<QApplication>` does drag `QDebug` in).

**Fix.** `src/core/ConfigManager.cpp`: `#include <QDebug>` in the Qt include
block, with a comment saying why it is named explicitly. `ConfigManager.cpp` is
upstream-inherited, so its existing ledger entry in
`tests/upstream-modifications.txt` gains this reason in the same commit.

**Verified by.** `tests/integration-logs-ci-fix/qdebug-probe/run.sh` (exit 0):
one probe compiled twice, differing only in that include — without it the
compiler emits exactly `invalid use of incomplete type ‘class QDebug’`, with it
the file compiles. Plus the real target: `build/src/CMakeFiles/lmmsobjs.dir/core/ConfigManager.cpp.o`
built with the CI's `-DUSE_WERROR=ON` (`build.log`). This box has Qt6 only
(6.4.2) and no Qt5 anywhere, so the probe reproduces the *error class* with the
minimal include set; the Qt5 instance itself is evidenced by the job log and
finally by CI.

## 2. `macos-arm64` — the vendored `include/fenv.h` assumed x86_64's `fenv_t`

```
include/fenv.h:20:24: error: no member named '__control' in 'fenv_t'
include/fenv.h:24:10: error: no member named '__mxcsr' in 'fenv_t'
   … 6 errors, through line 43
```

**Cause.** `include/fenv.h` is **upstream's file, unmodified**
(`git diff origin/master -- include/fenv.h` is empty at this tag). It exists to
polyfill `feenableexcept`/`fedisableexcept` on macOS, which Darwin's `<fenv.h>`
does not declare, and it did so by poking `fenv_t.__control` and
`fenv_t.__mxcsr` — the x87/SSE members of Darwin's **x86_64** `fenv_t`:

```c
/* macOS 11.3 SDK, usr/include/fenv.h */
#elif defined __i386__ || defined __x86_64__
typedef struct { unsigned short __control; unsigned short __status;
                 unsigned int __mxcsr; char __reserved[8]; } fenv_t;
#elif defined __arm64__
typedef struct { unsigned long long __fpsr; unsigned long long __fpcr; } fenv_t;
```

Darwin selects that typedef *per architecture*, so on arm64 both member names are
simply absent — a hard error, not a warning. It surfaced now because the new
VST3 hosting compiles the SDK's `fcondition.cpp`, whose `CoreServices.h → AE.h →
CarbonCore.h` include chain does `#include <fenv.h>` and therefore picks up this
repository's shadowing header (`fcondition.cpp:54 → CoreServices.h:23 → AE.h:20 →
CarbonCore.h:162 → include/fenv.h:20`). Upstream never compiled the SDK, so the
header had never been reached from an Apple framework header before.
`macos-x86_64` does **not** show this error (its log contains no `fenv` at all):
on x86_64 the members exist, which is also why the Polyfill ever worked.

**Fix — and why this is not silencing.** The header still needs to shadow Darwin's
(that is how `feenableexcept` reaches `src/core/main.cpp`'s `LMMS_DEBUG_FPE`
block; it is the only consumer, and it is included as `<fenv.h>`, so the shadow is
load-bearing and deleting the header would remove the function). What was wrong is
the assumption that one register layout applies to every Apple architecture.
The header now branches:

- **x86 / x86_64** — the upstream code, byte-identical (`x86-branch` check).
- **arm64** — `__fpcr`, using Darwin's own bit definitions: the trap-enable bits
  (`__fpcr_trap_*`, 0x100…0x8000) sit exactly eight places above the `FE_*` flag
  bits, so `FE_ALL_EXCEPT << 8` (0x9f00) is precisely the controllable mask. Note
  the **opposite polarity** to x87: a set FPCR bit means the exception *traps*,
  where a set x87 control bit means *masked*. `feenableexcept` therefore sets
  those bits and `fedisableexcept` clears them; both return the previous *masked*
  set so the two branches keep one contract. `__fpcr_flush_to_zero` (0x01000000)
  is a mode, not a trap enable, and is deliberately outside the mask so it
  survives untouched.

(An intermediate draft of this fix had the polarities inverted — the harness
caught it at check 3 before it was committed. That is the value of running the
arm64 code rather than only type-checking it.)

**Verified by.** `tests/integration-logs-ci-fix/fenv-arch-check/run.sh` (exit 0)
compiles and **runs** the polyfill against synthetic `<fenv.h>` headers carrying
Darwin's per-architecture typedefs, macros and `__fpcr` enum verbatim, with
`-U__x86_64__ -D__arm64__ -nostdinc` so the arch guard selects arm64 and no other
`fenv.h` is reachable. Five expectations: the fixed header compiles and passes ten
bit-arithmetic checks on arm64; the pre-fix header fails naming `__control`, exactly
as CI did; both headers pass on x86_64; and the x86_64 function bodies diff empty
against the pre-fix text.

**Not verified here:** that the real FPCR behaves as the simulated register file
does, and that `fegetenv`/`fesetenv` round-trip on Darwin — those need macOS. CI
is the verifier for this fix.

## 3. `mingw64` — CLAP headers provisioned into the build tree, then not found

```
CMake Error at cmake/modules/ClapHeaders.cmake:45 (MESSAGE):
  No CLAP headers at
  '/home/runner/work/zene-studio/zene-studio/build/clap/include/clap'.
Call Stack (most recent call first):
  plugins/ClapEffect/CMakeLists.txt:20 (INCLUDE)
```

**Cause.** Not the provisioning — the log shows the provisioning step succeeding
and cloning CLAP 1.2.10 into exactly that directory:

```
CLAP: fetching 1.2.10 (195b42a00) from https://github.com/free-audio/clap.git into …/build/clap
CLAP     1.2.10 (195b42a00, MIT)  -> …/build/clap
```

The cause is the **find policy of the cross-compile toolchain**.
`cmake/toolchains/common/mingw-vcpkg.cmake` sets
`CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY` (line 48) with three
`CMAKE_FIND_ROOT_PATH` entries. Under `ONLY`, `find_path` re-roots its **HINTS**
under the root paths, so `HINTS "${LMMS_CLAP_PATH}/include"` — a path inside the
build tree — is searched as `<sysroot>/…/build/clap/include` and can never be
found. The other four jobs configure natively and find the same checkout (their
logs print `Found CLAP 1.2.10 headers (MIT) at …`). This is why the failure is
new at all: until this tag nothing provisioned the headers and every job took the
"CLAP hosting skipped" STATUS path, so the rooted `find_path` was never asked for
something it could not see.

**Fix.** `cmake/modules/ClapHeaders.cmake`: add `NO_CMAKE_FIND_ROOT_PATH` to that
`FIND_PATH`. The CLAP headers are architecture-neutral third-party headers and
they live in the build tree by design; un-rooting this one call restores the
pinned checkout without letting a host-installed `clap/clap.h` be picked up ahead
of it (the ordinary default search paths stay rooted). `Vst3Sdk.cmake` needs no
equivalent — it tests absolute paths with `EXISTS`, which is not re-rooted.

**Verified by.** `tests/integration-logs-ci-fix/cmake-probes/run.sh` (exit 0)
drives the **real** modules: the pre-fix `ClapHeaders.cmake` fails at
`ClapHeaders.cmake:45` with that message while a genuine CLAP checkout sits at the
path the module was given; the fixed module prints `Found CLAP 1.2.10`. Both runs
use `-DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY` and fake root paths, mirroring the
MinGW toolchain.

**Not verified here:** no MinGW cross toolchain or vcpkg tree exists on this box,
so the Windows job itself has not run. CI is the verifier.

## 4. `msvc-x64` + `windows-arm64` — `Not a target: lmms` (the rename's loose end)

```
CMake Error at cmake/modules/InstallTargetDependencies.cmake:37 (message):
  Not a target: lmms
Call Stack (most recent call first):
  cmake/install/CMakeLists.txt:26 (INSTALL_TARGET_DEPENDENCIES)
```

**Cause.** Also not the provisioning — both jobs found the SDK and headers
(`Found CLAP 1.2.10 headers (MIT) at …`, `Found VST3 SDK 3.8 (MIT) at …`) and
failed two lines later. `cmake/install/CMakeLists.txt:28` — upstream-inherited and
unmodified — still passed `TARGETS lmms`, but the rename wave changed the
executable target to `zene` (`src/CMakeLists.txt:150` `ADD_EXECUTABLE(zene …)`).
The whole block is guarded by `IF(LMMS_BUILD_WIN32 OR LMMS_INSTALL_DEPENDENCIES)`,
which is why only the three Windows jobs reach it: on Linux and macOS it is not
entered, so the stale name sat unnoticed. It is new since the last green run
because run 33 (`main@2f0cd8689`, 2026-09-11) still had `ADD_EXECUTABLE(lmms)`;
the rename landed after it.

**Fix.** `cmake/install/CMakeLists.txt`: `TARGETS lmms` → `TARGETS zene`, declared
in `tests/upstream-modifications.txt` in the same commit (the file is
upstream-inherited; the old name is retired deliberately so a rebase does not
restore it).

**Verified by.** `tests/integration-logs-ci-fix/cmake-probes/run.sh` (exit 0):
the **real** `InstallTargetDependencies.cmake` aborts with exactly
`Not a target: lmms` when handed `lmms`, and configures when handed `zene`. The
check it aborts on is the module's plain `IF(NOT TARGET …)` and is platform
independent; the Windows-ness that enters it is the `LMMS_BUILD_WIN32` guard, and
that is what the two job logs show.

**Not verified here:** no Windows compiler on this box, so the fix needs the next
Windows run. It is also worth noting that neither of the two Windows causes is
about the VST3/CLAP *provisioning* being wrong — it worked on all three Windows
jobs. Two independent pre-existing conditions surfaced at the same time as it.

## 5. `macos-x86_64` — the VST3 SDK's Objective-C++ needs ARC

```
build/vst3sdk/public.sdk/source/vst/hosting/module_mac.mm:22:2: error:
this file needs to be compiled with automatic reference counting enabled
```

This is a **fourth failure the brief did not list** (it describes both macOS jobs
as failing on `fenv.h`; `macos-x86_64`'s log contains no `fenv` error at all —
its build died in a different translation unit first, and `macos-arm64` never got
this far).

`module_mac.mm` opens with `#if !__has_feature(objc_arc)` /
`#error this file needs to be compiled with automatic reference counting enabled`,
and `cmake/modules/Vst3Sdk.cmake` compiled it without ARC. The SDK's own sample
hosts set ARC on that one file
(`public.sdk/samples/vst-hosting/audiohost/CMakeLists.txt`:
`set_source_files_properties(… module_mac.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")`),
so the fix copies the SDK's idiom: the same `SET_SOURCE_FILES_PROPERTIES` on this
translation unit under `IF(APPLE)`. Per-file, so the other two Objective-C++
units in the target keep their current mode; and it sits inside the existing
`IF(NOT TARGET lmms_vst3_sdk)` guard, i.e. in the one directory that creates the
target.

**Verified how:** not here. The VST3 SDK is not on this box's macOS side at all —
no Darwin toolchain exists — so this rests on the SDK's own `CMakeLists.txt`
(copied, not invented) and is **unverified until CI runs**. It is marked as such
in the source comment too.

---

## What was not fixed, and why

### `python3 tests/scripted/check-namespace` — four errors, all pre-existing

```
Error: include/ScriptLuaQtTypes.h: File has no namespace lmms
Error: src/core/ControlDeviceHosted.cpp:139: Missing comment // LMMS_HAVE_LV2
Error: src/core/ControlDeviceHosted.cpp:245: Missing comment // LMMS_HAVE_LV2
Error: tools/ncpu-shim.c: File has no namespace lmms
4 errors.
```

Unchanged by this work (the log is `tests/integration-logs-ci-fix/check-namespace.log`),
and they are the **only** thing keeping `checks.yml` red on the tag — that run's
`scripted-checks` job failed at exactly this step. None of the four is in a file
this change touches, so per the brief they are reported rather than fixed:

- `src/core/ControlDeviceHosted.cpp:139` and `:245` are the `#endif`s closing
  `#ifdef LMMS_HAVE_LV2` blocks; the checker wants the end-of-block comment
  (`#endif // LMMS_HAVE_LV2`) that the rest of the tree carries. **Trivial and
  clearly correct** (comment-only, fork-NEW file, already registered in
  `tests/fork-sources.txt`), but unrelated to this CI task and inside a
  subsystem three live lanes touch (`zene-pa-lv2`, `zene-pa-ctrlhard`) — a
  comment-only edit there can turn into a merge conflict for no CI gain. Worth
  dispatching to whoever owns the LV2 slice.
- `include/ScriptLuaQtTypes.h` "has no namespace lmms" — needs a real decision
  (wrap the contents, or exempt the file) and a check that every consumer still
  compiles; not a mechanical fix.
- `tools/ncpu-shim.c` "has no namespace lmms" is a **false positive by
  construction**: a C file cannot have a C++ namespace. The checker's own
  mechanism for this is its `known_no_namespace_lmms` path list (which already
  exempts `tools/wasm/wat2wasm.cpp`); the honest fix is to add this file there.
  That means editing the gate script, which is the project's to decide — note the
  precedent that the *previous* commit (`d8d2114be`) resolved the identical
  conflict for its own `tools/*.c` abort shim by deleting the file instead, which
  is not an option for a shim other tooling needs. **Recommend** adding
  `tools/ncpu-shim.c` to that list in the checker, in a change that says so.

### Not investigated further

- The **release-honesty guard** and other steps of the tag's jobs ran on some
  platforms; no evidence was found of a *second* failure per job behind these
  five, but every job died at its first error, so a job that was further along
  (`linux-*`, `msvc-x64`) may still hold surprises behind the fixed one. That is
  the expected shape of a first green run after a 7/7-red tag.
- No change was made to the provisioning script itself: it worked on all seven
  jobs (`provision.log` is its run on this box, also exit 0).

## Ledger and scope effects

- `tests/upstream-modifications.txt` gains two entries (`include/fenv.h`,
  `cmake/install/CMakeLists.txt`) and an extra reason on the existing
  `src/core/ConfigManager.cpp` line — the three inherited files this change
  diverges.
- No file was added to `tests/fork-sources.txt` or `tests/all-sources.txt`, so
  the complexity / file-length / duplication ratchets keep their existing scope
  and baselines. The verification harnesses are shell scripts that generate their
  own probes into a gitignored `out/` directory; no new translation unit is
  registered, and no test count changes.
- `ctest` is **86 tests**, unchanged (see the local run recorded in
  `tests/integration-logs-ci-fix/ctest.log`).

## The final tree's own measurements

All of these are on the committed tree, at the tip of `fix/ci-tag-failures`:

| command | result | log |
|---|---|---|
| `.github/workflows/provision-plugin-hosting-deps.sh build` | exit 0 | `provision.log` |
| `cmake -S . -B build … -DWANT_QT6=ON -DUSE_WERROR=ON -DUSE_COMPILE_CACHE=ON -DWANT_VST3=ON -DWANT_CLAP=ON` | exit 0 | `configure.log` |
| `cmake --build build -j2` | exit 0 (`BUILD_EXIT=0`), i.e. the whole tree including the new arm64/x86_64 fenv branch compiles under `-Werror` | `build.log` |
| `ctest` in `build/tests` | **86/86 passed**, exit 0 | `ctest.log` |
| `bash tests/fork-sources-gate.sh` | exit 0 (242 fork-NEW, 1036 inherited, 34 tooling, 0 stale) | `fork-sources-gate.log` |
| `bash tests/no-upstream-regression-gate.sh` | exit 0 | `no-upstream-regression-gate.log` |
| `python3 tests/scripted/check-namespace` | exit 1, the same four pre-existing errors as before this change | `check-namespace.log` |

The local configure is the CI Linux job's options plus `-DWANT_QT6=ON`, because
this box has Qt6 6.4.2 and no Qt5 (the CI Linux jobs have the reverse); the
VST3/CLAP options are on so the CMake modules this change touches are actually
configured.

