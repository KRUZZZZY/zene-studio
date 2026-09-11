# Plugin hosting in the release build

**Why the published `v0.1.0-alpha` contained no VST3 and no CLAP hosting while its
release notes advertised both, what now fetches them in CI, and the guard that
fails a build in which a documented feature has been compiled out.**

Worktree: `post-alpha/vst3-instrument-fixture` → branch `post-alpha/plugin-hosting-in-release`.
Produced 2026-09-12. Nothing here was validated by a GitHub Actions run: see
[§6 What this does not prove](#6-what-this-does-not-prove) before reading any of it
as a release claim.

---

## 1. The defect

`docs/RELEASE-NOTES-v0.1.0-alpha.md:121-122` told users:

> **VST3 and CLAP hosting, effects only.** Modern effects load, run and save their
> state. Instruments in either format do not load; see the limitations page.

The artefact that was published under that text contains neither. The published
release's build logs (`gh run view 34639862178 --repo KRUZZZZY/zene-studio --log`,
run 34639862178) show the same two lines on **all seven** jobs — `linux-x86_64`,
`linux-arm64`, `macos-x86_64`, `macos-arm64`, `msvc-x64`, `mingw64`,
`windows-arm64`:

```
-- VST3 hosting skipped: no SDK at '<runner>/build/vst3sdk' (clone instructions in cmake/modules/Vst3Sdk.cmake, then re-run cmake)
-- CLAP hosting skipped: no headers at '<runner>/build/clap/include/clap' (clone instructions in cmake/modules/ClapHeaders.cmake, then re-run cmake)
```

Those strings are `plugins/Vst3Effect/CMakeLists.txt:13-14` and
`plugins/ClapEffect/CMakeLists.txt:15-16`. Both hosts are gated on an option that
defaults to `AUTO`, and `AUTO` with the dependency missing is a `RETURN()` with a
`STATUS` line:

```cmake
SET(WANT_VST3 "AUTO" CACHE STRING "Build native VST3 hosting: AUTO, ON or OFF")
IF(WANT_VST3 STREQUAL "AUTO" AND NOT EXISTS "${LMMS_VST3_SDK_PATH}/LICENSE.txt")
	MESSAGE(STATUS "VST3 hosting skipped: no SDK at '${LMMS_VST3_SDK_PATH}' "...)
	RETURN()
ENDIF()
```

Nothing fetched the SDK, so every job took that branch; nothing failed, the
packages were built and uploaded, and the only trace was two `STATUS` lines in a
log nobody read until this task. `docs/KNOWN-LIMITATIONS.md` disclosed that the
hosts were **effects only** but not that they were **absent**, whereas it does
disclose `WANT_WASM=OFF` and `WANT_STEM_SPLIT=OFF` for the two other
configure-time features. A silent compile-out with a public claim on top of it is
the defect; a missing dependency is only the mechanism.

Note what a build reports about itself: `lmms --version` prints a `Build options:`
line built from the `WANT_*` CMake variables (`src/CMakeLists.txt:6-14` →
`src/lmmsversion.h.in:3` → `src/core/main.cpp:144`). **A skip is not merely
`AUTO`**: when the plugin directory returns early, the `SET(... CACHE ...)` that
creates `WANT_VST3` never runs, so the option is missing from the dump entirely.
Both failure shapes are covered in §5.

## 2. What changed

### 2.1 CI now fetches the dependencies, at the pins the tree already documents

New: **`.github/workflows/provision-plugin-hosting-deps.sh`** (used by the
workflow *and* by `tools/local-ci.sh`, so both fetch the same trees at the same
commits). It reads the tag, commit and URL out of `cmake/modules/Vst3Sdk.cmake`
and `cmake/modules/ClapHeaders.cmake` — it carries **no copy of the pins** — then
clones

* `v3.8.1_build_84` (`3cdf9ca5…`, Steinberg VST3 SDK) into `<build>/vst3sdk` with
  the four submodules the host subset needs (`base cmake pluginterfaces public.sdk`), and
* `1.2.10` (`195b42a0…`, CLAP headers) into `<build>/clap`,

which are exactly the paths both `CMakeLists.txt` files default to
(`${CMAKE_BINARY_DIR}/vst3sdk`, `${CMAKE_BINARY_DIR}/clap`), so the configure line
needs no extra `-D`. It then **verifies**, and fails non-zero with a reason if any
of these does not hold: the checkout is at the pinned commit; `LICENSE.txt` is the
MIT licence; no `pluginterfaces/vst2.x` exists (the GPLv2 constraint — VST2 stays
Vestige-only). A cached tree is re-verified against the pin on every run and
re-cloned if it is not at it, so a stale or truncated cache cannot be used
silently.

Every job that packages a release (`Package` + `Upload artifacts` exist on all
seven instances, gated the same way for all of them) gained three steps before
`Configure`:

```yaml
      - name: Cache the pinned VST3 SDK
        uses: actions/cache@v4
        with:
          path: build/vst3sdk
          key: "vst3sdk-${{ runner.os }}-${{ hashFiles('cmake/modules/Vst3Sdk.cmake') }}"
      - name: Cache the pinned CLAP headers
        uses: actions/cache@v4
        with:
          path: build/clap
          key: "clap-${{ runner.os }}-${{ hashFiles('cmake/modules/ClapHeaders.cmake') }}"
      - name: Provision the pinned VST3 SDK and CLAP headers
        run: bash .github/workflows/provision-plugin-hosting-deps.sh build
```

The cache key hashes the file that owns the pin, so bumping a pin in the cmake
module invalidates the cache by itself and there is no second place to edit. The
macOS job's `mkdir build` became `mkdir -p build`: a cache restore may already have
created `build/`, and the old form would have failed the step.

### 2.2 The release configuration asks for the features

`-DWANT_VST3=ON -DWANT_CLAP=ON` joined `CMAKE_OPTS` on `linux-x86_64`,
`linux-arm64`, `macos-x86_64/arm64`, `mingw64` and `windows-arm64`, and the inline
flag list of `msvc-x64` (the one job with no `CMAKE_OPTS`). This is deliberate:
with the SDK absent, `ON` is a configure `FATAL_ERROR` from `Vst3Sdk.cmake`
instead of a silent skip, so the failure mode is "the job is red and says what to
clone", never a package that quietly lacks the feature. For a build that already
had the SDK this changes nothing: `AUTO` with the SDK present and `ON` with the
SDK present create the identical targets from the identical source list.

`tools/local-ci.sh` mirrors it: its `CI_CMAKE_OPTS` is documented as the
`linux-x86_64` job's flags byte for byte, so it now carries the same two flags,
and it runs the same provisioning script as a new step 0.

### 2.3 A release-honesty guard, with one home for the advertised list

New: **`tests/advertised-features.tsv`** — the single home. Each row names a
feature, the `WANT_*` option that must hold for the release's claim about it to be
true, the plugin module that proves it was built, and the claim itself:

```
vst3-hosting	WANT_VST3	ON	vst3effect	VST3 hosting (effects) is in the release
clap-hosting	WANT_CLAP	ON	clapeffect	CLAP hosting (effects) is in the release
wasm-sandbox	WANT_WASM	OFF	-	The WASM DSP sandbox is documented as absent from these builds
stem-separation	WANT_STEM_SPLIT	OFF	-	Offline stem separation is documented as absent from these builds
```

New: **`tests/release-honesty-gate.sh`** — reads that manifest and nothing else; it
carries no feature list of its own. Given the build's own report (either
`--dump <lmms --version output>` or `--header <build>/lmmsversion.h`, which is the
same string the binary prints) and optionally `--artifacts <build tree>`, it fails
when

* a feature documented as **present** does not report the required value
  (`WANT_VST3='AUTO'` is a failure — the release says the feature is there, so the
  build must ask for it), or the option is missing from the report entirely (the
  skipped-host case), or
* the module the option implies was never built (`*vst3effect*.so/.dll/.dylib`
  under `--artifacts`), which is what catches "the option says ON but the plugin
  directory was not built", or
* a feature documented as **absent** reports itself as on — the documents would
  then understate the build, which is the same defect with the sign flipped.

It runs on every build, as a step between the tests and `Package` (so a dishonest
tree is never packaged), on all six job definitions; `msvc-x64` gets `shell: bash`
explicitly because its default shell is PowerShell. The release path is covered
because the release path is one of those jobs: a tag run packages from the same
tree the guard just judged.

### 2.4 The SDK's Objective-C++ sources now have a compiler

`cmake/modules/Vst3Sdk.cmake` gained, for `APPLE` only: `ENABLE_LANGUAGE(OBJCXX)`
(the SDK's `module_mac.mm`, `systemclipboard_mac.mm`, `threadchecker_mac.mm` are
Objective-C++ and no other `.mm` exists anywhere in this tree, so CMake had no
compiler for them) and the three frameworks those files call
(`CoreFoundation` — `CFBundle*`; `Foundation`/`Cocoa` — `NSPasteboard`). Windows
needs nothing added: `ole32` (`OleInitialize`, `CoCreateInstance`) and `shell32`
(`SHGetKnownFolderPath`) from `module_win32.cpp` are already in
`CMAKE_CXX_STANDARD_LIBRARIES` for MSVC and MinGW alike. **This is unverified on
macOS** — see §6.

The `SYSTEM` property fix on `lmms_vst3_sdk` that this branch inherited is a hard
prerequisite for all of the above and is untouched: the SDK is compiled with the
third-party flag set (`-w` / `/W0`), the product's own sources keep
`-Wall -Werror` (`/WX`), which is why `pluginterfaces/base/ustring.cpp:228`'s
`%lld`-vs-`Steinberg::int64` diagnostic no longer kills the Linux `-Werror` job.
Without it, provisioning the SDK would turn a silently-skipped feature into a
broken build on every job.

## 3. What a CI run must show

No Actions run was executed for this work (it is prohibited here, and every report
of one would be a claim, not evidence). The next push's `build` run is the first
verification. Concretely, per job:

1. **`Provision the pinned VST3 SDK and CLAP headers`** succeeds, printing
   `VST3 SDK v3.8.1_build_84 (3cdf9ca5d, MIT)  -> build/vst3sdk` and
   `CLAP     1.2.10 (195b42a00, MIT)  -> build/clap` (cold run) or
   `already at <tag> … — nothing to fetch` (cache hit).
2. **`Configure`** prints, in place of the two `hosting skipped` lines:

   ```
   -- Found CLAP 1.2.10 headers (MIT) at '<build>/clap/include'
   -- Found VST3 SDK 3.8 (MIT) at '<build>/vst3sdk'
   ```

   A `FATAL_ERROR` here (`No VST3 SDK at …`, `No CLAP headers at …`) means the
   provisioning step failed and the job is correctly red rather than dishonestly
   green.
3. **`Build`** reaches the `vst3effect` and `clapeffect` targets, i.e. the job's
   `build/plugins/` contains `libvst3effect.so` / `vst3effect.dll` and
   `libclapeffect.so` / `clapeffect.dll` (names differ per platform; the guard
   globs `*vst3effect*` / `*clapeffect*` over `*.so/.dll/.dylib`).
4. **`Release-honesty guard`** prints, on a good build:

   ```
   === release honesty: what the release documents vs what the build contains ===
     [PASS] vst3-hosting     ON matches ON; module build/plugins/libvst3effect.so
     [PASS] clap-hosting     ON matches ON; module build/plugins/libclapeffect.so
     [PASS] wasm-sandbox     OFF,OFF matches OFF
     [PASS] stem-separation  OFF matches OFF
   RESULT: PASS — all 4 documented feature(s) are what this build contains
   ```

   and exits 0. On a build with the SDK absent it prints, and exits 1:

   ```
     [FAIL] vst3-hosting     WANT_VST3 is not reported by this build at all, so the feature cannot be in it
            claim it must keep true: VST3 hosting (effects) is in the release
     [FAIL] clap-hosting     WANT_CLAP is not reported by this build at all, so the feature cannot be in it
            claim it must keep true: CLAP hosting (effects) is in the release
   RESULT: FAIL — 2 of 4 documented feature(s) do not match this build
   ```

   The step fails the job *before* `Package`, so no package is produced from a
   build in that state. That is the property the alpha lacked: the dishonest build
   was green from `Configure` to `Upload artifacts`.

If a job is red anywhere in steps 1-4, the next place to look is the job's own log
for those exact strings, not for a test failure — this class of defect does not
fail tests, which is how it shipped.

## 4. Cost: what provisioning adds per job

Measured on this box, cold and cached (the runner's network is comparable; the
runner omits `Makefile` churn, so treat these as the right order of magnitude):

| | measured |
|---|---|
| Cold: clone both trees (SDK 39 MB + CLAP 6.5 MB incl. `.git`) | **9.2 s** |
| Cache hit: both trees present, pins re-verified, nothing fetched | **0.12 s** |
| Trees on disk in `<build>/` | 39 MB + 6.5 MB |

So the cache step does remove the re-clone: a restore is the 0.12 s path plus
whatever the runner spends unpacking ~45 MB, and the clone happens once per
operating system (the key carries `runner.os`) per pin change — not once per run,
and not seven times per run.

**What the SDK costs the compile.** The targets the two hosts add are 62 translation
units — `lmms_vst3_sdk` 31, `vst3effect` 12, `clapeffect` 11 (each host's count includes
its own moc) plus 8 first-party units swept in by the regeneration — against ~1,100
first-party files in the product. Rebuilding exactly those from scratch objects at `-j4`
on this box took **2 m 08 s** with a warm ccache (52 of the 62 compiles served from it),
module link steps included. That is the direct measurement available here: it answers
"is it meaningful" with "a few percent of a 23-47 minute job, and a compile that any
warm ccache largely absorbs", not with an end-to-end per-job delta (§6). A cold ccache
pays the full compilation of those 62 units.

## 5. Local proof

Every command below was run in this worktree on 2026-09-12; every exit code was
measured unpiped (`cmd > log 2>&1; echo EXIT=$?`).

### 5.1 The acceptance command: configure, build, ctest

```
$ JOBS=4 bash tools/local-ci.sh --build-dir build --jobs 4
--- [0/3] provision the pinned VST3 SDK and CLAP headers (the CI step) ---
  VST3 SDK: already at v3.8.1_build_84 (3cdf9ca5d) in …/build/vst3sdk — nothing to fetch
  CLAP: fetching 1.2.10 (195b42a00) from https://github.com/free-audio/clap.git into …/build/clap
provision EXIT=0   (log: build/provision.log)
--- [1/3] configure (cmake -S . -B build ...) ---
configure EXIT=0   (log: build/configure.log)
--- [2/3] build (cmake --build build -j4) ---
build EXIT=0   (log: build/build.log)
--- [3/3] ctest (from build/tests, -j2) ---
ctest EXIT=0   (log: build/ctest.log)
ctest totals: 100% tests passed, 0 tests failed out of 29
…
  deviation: Qt5 development files not found: added -DWANT_QT6=ON (CI's runner installs qtbase5-dev; this box has Qt6 only)
local-ci: overall exit=0 (0 = every executed step passed)
LOCAL_CI_EXIT=0
```

**Both hosts were FOUND**, in place of the two `hosting skipped` lines the release
run printed (`build/configure.log:83-84`):

```
-- Found CLAP 1.2.10 headers (MIT) at …/zene-pa-vst3fix/build/clap/include
-- Found VST3 SDK 3.8 (MIT) at …/zene-pa-vst3fix/build/vst3sdk
```

**The build reached both plugin targets** (`build/build.log`):

```
[ 26%] Built target lmms_vst3_sdk          # 31 objects, the pinned SDK's hosting subset
[ 78%] Built target vst3effect
[ 95%] Built target clapeffect
```

and the modules exist:

```
$ ls -l build/plugins/libvst3effect.so build/plugins/libclapeffect.so
-rwxrwxr-x 1 kruzzzzy kruzzzzy 6890336 … build/plugins/libvst3effect.so
-rwxrwxr-x 1 kruzzzzy kruzzzzy 4337992 … build/plugins/libclapeffect.so
```

ctest: **29/29, 0 failed** (the suite was 26 tests before; the CLAP and VST3 hosts,
the VST3 bus-map/host/integration tests and the instrument-fixture probe are in this
build because the headers and SDK now exist, and the fixture is enabled by
`WANT_VST3_TEST_INSTRUMENT=ON` in this build directory's cache).

The build directory was also reconfigured with **both options ON**, which is what the
binary now reports — the release configuration this work makes CI ask for:

```
$ tr ' ' '\n' < build/lmmsversion.h | grep -E 'WANT_(VST3|CLAP)='
WANT_CLAP='ON'
WANT_VST3='ON'
```

### 5.2 The guard bites

**Red — the alpha's configuration, `AUTO` + no checkout.** A scratch configure of this
same tree with neither checkout present (`/tmp/zene-nosdk-build`) reproduced the
published defect locally:

```
-- CLAP hosting skipped: no headers at '/tmp/zene-nosdk-build/clap/include/clap' (clone instructions in cmake/modules/ClapHeaders.cmake, then re-run cmake)
-- VST3 hosting skipped: no SDK at '/tmp/zene-nosdk-build/vst3sdk' (clone instructions in cmake/modules/Vst3Sdk.cmake, then re-run cmake)
configure EXIT=0
```

Asked to judge that build:

```
$ bash tests/release-honesty-gate.sh --header /tmp/zene-nosdk-build/lmmsversion.h --artifacts /tmp/zene-nosdk-build
=== release honesty: what the release documents vs what the build contains ===
manifest : tests/advertised-features.tsv
build    : /tmp/zene-nosdk-build/lmmsversion.h  (generated header; the exact string the binary prints)
artifacts: /tmp/zene-nosdk-build

  [FAIL] vst3-hosting     WANT_VST3 is not reported by this build at all, so the feature cannot be in it
         claim it must keep true: VST3 hosting (effects) is in the release
  [FAIL] clap-hosting     WANT_CLAP is not reported by this build at all, so the feature cannot be in it
         claim it must keep true: CLAP hosting (effects) is in the release
  [PASS] wasm-sandbox     OFF,OFF matches OFF
  [PASS] stem-separation  OFF matches OFF

RESULT: FAIL — 2 of 4 documented feature(s) do not match this build
GUARD_EXIT=1
```

**Green — the build of §5.1**, with the exact command the workflow runs, and again
against the real binary's own `--version` output:

```
$ bash tests/release-honesty-gate.sh --header build/lmmsversion.h --artifacts build
  [PASS] vst3-hosting     ON matches ON; module build/plugins/libvst3effect.so
  [PASS] clap-hosting     ON matches ON; module build/plugins/libclapeffect.so
  [PASS] wasm-sandbox     OFF,OFF matches OFF
  [PASS] stem-separation  OFF matches OFF
RESULT: PASS — all 4 documented feature(s) are what this build contains
GUARD_EXIT=0

$ QT_QPA_PLATFORM=offscreen ./build/lmms --version > /tmp/zene-green-dump.txt   # VERSION_DUMP_EXIT=0
$ bash tests/release-honesty-gate.sh --dump /tmp/zene-green-dump.txt --artifacts build
… same four [PASS] lines …
GUARD_EXIT=0
```

All 83 `WANT_*`/`LMMS_HAVE_*` tokens in `build/lmmsversion.h` appear verbatim in that
binary's `Build options:` line (checked token by token), which is the claim that judging
the header is judging the binary.

The green run was then repeated after the objects of `lmms_vst3_sdk`, `vst3effect` and
`clapeffect` were deleted and rebuilt from scratch (the §4 timing run): both modules are
back at their previous byte sizes and the guard still exits 0, so a from-scratch build of
exactly those targets produces the same verdict.

**Control — the artifact leg is load-bearing.** The green header with `--artifacts` pointed
at an empty directory must fail even though the options say `ON`, and does:

```
$ bash tests/release-honesty-gate.sh --header build/lmmsversion.h --artifacts /tmp/zene-empty-artifacts
  [FAIL] vst3-hosting     WANT_VST3 says ON but no '*vst3effect*.so/.dll/.dylib' exists under /tmp/zene-empty-artifacts: the module was never built
  [FAIL] clap-hosting     WANT_CLAP says ON but no '*clapeffect*.so/.dll/.dylib' exists under /tmp/zene-empty-artifacts: the module was never built
RESULT: FAIL — 2 of 4 documented feature(s) do not match this build
GUARD_EXIT=1
```

### 5.3 Gates

```
$ bash tests/fork-sources-gate.sh          # Gate 9
bash: tests/fork-sources-gate.sh: No such file or directory
FORK_SOURCES_GATE_EXIT=127                 # ABSENT on this branch — see §6

$ bash tests/no-upstream-regression-gate.sh
PASS: every change to upstream-inherited code since 01148947ea4d8bdb05c237942758d61acc867223 is declared (31 file(s) in the ledger)
NO_UPSTREAM_REGRESSION_GATE_EXIT=0

$ bash tests/run-all-gates.sh
gate   name                     result
1      ctest                    PASS
2      coverage                 SKIP
3      no-tautology             PASS
4      complexity               PASS
5      mutation                 PASS
6      upstream-regression      PASS
7      file-length              PASS
8      duplication              PASS

RESULT: PASS — every executed gate passed
RUN_ALL_GATES_EXIT=0
```

No ledger entry was needed for these changes, and that is the gate's own verdict rather
than an assumption: `*.cmake` is "build/config (allowed)", `.github/*` is "CI config
(allowed, non-runtime)", `tests/**` and `*.md` are allowed, and `tools/local-ci.sh` is
already listed in `tests/fork-sources.txt` (line 113) as product-new. The gate prints
`fork-NEW (allowed)` for it in its per-file table.

## 6. What this does not prove

* **No GitHub Actions run was executed.** Everything above is local configure/build/ctest
  plus the *published* run's log. The workflow edit is verified by the local build being
  equivalent to the job's configuration, by `yamllint` over every tracked `*.yml`
  (the `checks` workflow's own gate, exit 0) and by a structural check that all six job
  definitions carry the three provisioning steps in the right order. §3 is the checklist
  for the real verification. **The GitHub Actions run itself could not be executed from
  here** — no job of any workflow was run, re-run, cancelled or dispatched.
* **`tests/fork-sources-gate.sh` (Gate 9) does not exist on this branch** (exit 127 above).
  That gate arrived with the `post-alpha/gate-debt` lane, and this branch is not descended
  from it. No result is reported for it, because there was no script to run.
* **macOS is unverified here, and changed more than the other jobs.** The
  `ENABLE_LANGUAGE(OBJCXX)` and the three frameworks in §2.4 are reasoned from the SDK's
  own sources (`CFBundle*`, `NSPasteboard`), not built. Both macOS jobs are the likeliest
  place for the first red.
* **`windows-arm64` (msys2) and `mingw64` are unverified.** `git` is in
  `deps-msys2-clangarm64.txt`, so the provisioning script has its tools there, and this
  box reproduces neither job.
* **The MSVC artifact glob is unverified.** The guard looks for
  `*vst3effect*`/`*clapeffect*` over `*.so/.dll/.dylib`, which covers both Ninja layouts
  (`build/plugins/` and `build/plugins/Release/`) but was only exercised on Linux.
* **`-DUSE_WERROR=ON` holds for the SDK on Linux only.** The macOS jobs run with
  `-DUSE_WERROR=OFF` and `windows-arm64` never sets the option, so the `SYSTEM` property
  fix is only *necessary* on the jobs that enforce a warning policy; nothing here proves
  the SDK is warning-clean under MSVC, because the third-party flag set hides its
  diagnostics there by design.
* **The per-job wall-clock delta is not isolated.** §4 gives the measured clone/restore
  cost and what the SDK adds to the compile, not an end-to-end per-job delta.
* **The guard does not read prose.** A claim added to the release notes without a row in
  `tests/advertised-features.tsv` is not detectable mechanically; the one-home rule makes
  the manifest authoritative, so the mitigation is process (add the row in the same commit
  as the claim), not a check.

## 7. The alpha's own documents

Deliberately **not** corrected here, on the owner's instruction (2026-09-12): the
published `v0.1.0-alpha` was a shipping rehearsal to prove the pipeline, not a product
milestone to be re-documented, and the programme is now aiming at beta. For the record:
`docs/RELEASE-NOTES-v0.1.0-alpha.md:121-122` and `docs/KNOWN-LIMITATIONS.md:59` are
**knowingly stale** about the artefact they describe — they advertise VST3/CLAP hosting
that that build does not contain, and this file is the correction of record. The beta's
notes must take their advertised feature list from `tests/advertised-features.tsv`, which
is what the guard reads, and the beta's build is the first one where that claim is true.
