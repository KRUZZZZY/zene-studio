# msvc-x64: `FAILED: [code=1] tests/partc_ref_bassbooster.dll` is a Windows file-sharing race in the vcpkg post-link step, not a link error

Status: **diagnosed, fix prepared, not applied here** — the fix belongs in `tests/CMakeLists.txt`,
which the runtime-failure lane owns for this wave. The patch is "The fix" below and as
`/tmp/msvc-partc-ref.patch` (sha256 `f225d8cd26953ececc6d0893165eb7de3a96f8bd4bf319ba0e47ffccff712bb5`).
Every number below was read back out of the downloaded job logs, not inferred.

## The exact failure

`msvc-x64`, job `103701272689` (run `34748733618`, branch `v0.2.1-alpha`), edge `824/2190`:

```
[824/2190] Linking CXX shared module tests\partc_ref_bassbooster.dll
FAILED: [code=1] tests/partc_ref_bassbooster.dll
C:\Windows\system32\cmd.exe /C "cd . && C:/ProgramData/chocolatey/bin/ccache.exe "C:\Program Files\CMake\bin\cmake.exe"
  -E vs_link_dll --msvc-ver=1944 --intdir=tests\CMakeFiles\partc_ref_bassbooster.dir ... -- link.exe /nologo
  tests\CMakeFiles\partc_ref_bassbooster.dir\...\mocs_compilation.cpp.obj ... /out:tests\partc_ref_bassbooster.dll
  ... /dll /version:0.0 /machine:x64 /debug /INCREMENTAL  src\zene.lib  <Qt6 libs> ... <vcpkg libs> ...
  && C:\Windows\system32\cmd.exe /C "cd /D D:\a\zene-studio\zene-studio\build\tests &&
     C:\vcpkg\vcpkg.exe z-applocal --target-binary=D:/a/zene-studio/zene-studio/build/tests/partc_ref_bassbooster.dll
     --installed-bin-dir=D:/a/zene-studio/zene-studio/build/vcpkg_installed/x64-windows/bin""
The process cannot access the file because it is being used by another process.
```

That last line is the **only** diagnostic, and it is a Win32 sharing violation
(`ERROR_SHARING_VIOLATION`), not a linker message.

## What it is not

- **Not a duplicate-symbol collision.** Across the whole job log, `LNK2005`, `LNK1169`, `LNK2019`
  and `LNK1120` each match **zero** times. The two modules fixed earlier this week
  (`plugins/Vst3Effect`, `plugins/Vst3Instrument` — `LNK2005` on `zene.lib`'s exported
  `std::vector<float>` instantiations, fixed with a target-scoped `/FORCE:MULTIPLE`) were a
  different class; `/FORCE:MULTIPLE` would suppress nothing real here and fix nothing.
- **Not a compile error.** In that same job all four of the module's objects compiled:
  `[633]` mocs_compilation, `[634]` BassBooster.cpp, `[635]` BassBoosterControlDialog.cpp,
  `[636]` BassBoosterControls.cpp.
- **Not deterministic.** The same target links cleanly in every other msvc-x64 job checked —
  sibling `main` run `34748729956` (edge `825/2190`), run `34750729517` (edge `851/2236`), and
  runs `34750728373`, `34731094289`, `34728692245`, `34725347297`, `34724261094`, `34723455378`,
  `34708116217`, `34643190172`: 0 hits for "another process", 0 `partc_ref` FAILED edges. A race,
  not something the tree encodes.

## The mechanism

The vcpkg toolchain appends a POST_BUILD step to every `MODULE`/`SHARED` target in a Windows
triplet — `scripts/buildsystems/vcpkg.cmake`, the `add_library`/`add_executable` wrappers:

```cmake
add_custom_command(TARGET "${target_name}" POST_BUILD
    COMMAND "${Z_VCPKG_EXECUTABLE}" z-applocal
        "--target-binary=$<TARGET_FILE:${target_name}>"
        "--installed-bin-dir=${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
    VERBATIM)
```

CMake chains that command onto the link rule, which is why a failure inside it is reported as
`FAILED: tests/partc_ref_bassbooster.dll`. `z-applocal` copies each imported vcpkg dependency DLL
into **the directory the binary lives in** (vcpkg-tool `src/vcpkg/commands.z-applocal.cpp`,
`deploy_binary` → `copy_file(..., CopyOptions::update_existing)` into `target_binary_dir`).

All `partc_ref_*` modules map to `build\tests`, they link in one burst, and they link **first**:

| what | where it links | when, in the passing run `34748729956` |
|---|---|---|
| 41 `tests\partc_ref_*.dll` modules | `build\tests` | all 41 between 09:13:40 and 09:13:47 (edges 824–878) |
| `tests\synthetic_audio_plugin.dll` | `build\tests` | edge 1315 |
| 75 test executables | `build\tests` | edges 2117+, from 09:24:52 over ~3 min that run's log covers |
| 63 plugin modules | `build\plugins` | edges 1247+, separate directory |

Five of those module links started within 0.3 s of each other in the failing run
(09:18:48.508–09:18:48.800: bassbooster, amplifier, bitcrush, dualfilter, waveshaper) — the
reference modules are the first burst of writers into a freshly created `build\tests`, so they are
the ones whose `z-applocal` steps actually copy the shared dependency DLLs. Every later link into
that directory finds the files present and skips them (`CopyOptions::update_existing`). Two of
those copies racing over the same destination file is the sharing violation, and the module that
loses the race is reported as the build failure even though its own link succeeded.

## Why the obvious vcpkg knob does not apply

`X_VCPKG_APPLOCAL_DEPS_SERIALIZED` ("Add USES_TERMINAL to VCPKG_APPLOCAL_DEPS to force
serialization") is upstream's answer to this class — but the toolchain passes its `EXTRA_OPTIONS`
(`USES_TERMINAL`) only inside the `add_executable` wrapper. The `add_library` wrapper, the one a
`MODULE` target goes through, never sets it, so the knob cannot serialise `partc_ref_*` at all, and
turning it on would force every executable's applocal through a jobserver for no reason.

## The fix

A Ninja pool of one for these targets only: it orders the 41 links — and with them their post-link
steps — against each other and nothing else, and unlike `/FORCE:MULTIPLE` it cannot hide a real link
error. `JOB_POOL_LINK`/`JOB_POOLS` are Ninja-only properties (ignored by other generators), and
ninja errors if a rule names a pool `JOB_POOLS` does not declare, so both halves travel together
under the same `MSVC` guard the Windows-specific code here already uses.

```diff
diff --git a/tests/CMakeLists.txt b/tests/CMakeLists.txt
--- a/tests/CMakeLists.txt
+++ b/tests/CMakeLists.txt
@@ -396,6 +396,53 @@ set(PART_C_MIGRATED_TARGETS "")
 set(PART_C_REFERENCE_DEFINITIONS "")
 set(PART_C_MIGRATED_DEFINITIONS "")
 
+# --- MSVC: the reference modules link one at a time ---------------------------
+# CI msvc-x64 (job 103701272689, run 34748733618, edge 824/2190) failed the build
+# on one of these modules, and not on a symbol error:
+#
+#   FAILED: [code=1] tests/partc_ref_bassbooster.dll
+#   ... cmd.exe /C "cd . && ccache "cmake" -E vs_link_dll ... -- link.exe ... && cmd.exe
+#        /C "cd /D ...\build\tests && C:\vcpkg\vcpkg.exe z-applocal
+#           --target-binary=...build/tests/partc_ref_bassbooster.dll
+#           --installed-bin-dir=...build/vcpkg_installed/x64-windows/bin""
+#   The process cannot access the file because it is being used by another process.
+#
+# The job's whole log carries no LNK2005, LNK1169, LNK2019 or LNK1120 (grep count
+# 0), so this is NOT the duplicate-symbol collision the two VST3 modules hit
+# (plugins/Vst3Effect/CMakeLists.txt) and /FORCE:MULTIPLE is the wrong tool: it
+# would suppress nothing and fix nothing. The only diagnostic is a Win32 sharing
+# violation, charged to the module because that is the edge the failing command
+# belongs to - the vcpkg toolchain appends `vcpkg z-applocal` as a POST_BUILD step
+# of every MODULE/SHARED target in a Windows triplet (scripts/buildsystems/vcpkg.cmake,
+# the add_library wrapper) and CMake chains it into the link rule. z-applocal copies
+# that binary's imported vcpkg dependency DLLs into the binary's own directory
+# (vcpkg-tool commands.z-applocal.cpp: deploy_binary -> copy_file into
+# target_binary_dir), and every module above maps to build\tests. They also link in
+# one burst - all 41 of them between 09:13:40 and 09:13:47 in the passing run
+# 34748729956 - several at a time, and they link *before* the 75 test executables and
+# the test-double module that share that directory. So they are the ones whose
+# post-link steps really do copy the shared dependency DLLs; every later link into
+# build\tests finds those files present and skips them. Two of those copies racing
+# over the same destination file is the sharing violation, and the module that loses
+# that race is reported as a build failure even though its own link succeeded
+# (bassbooster's four objects compile and its link is clean in every other msvc-x64
+# run, and in the main-branch run of the identical commit).
+#
+# vcpkg's own knob for this class, X_VCPKG_APPLOCAL_DEPS_SERIALIZED, passes
+# USES_TERMINAL only from the toolchain's add_executable wrapper - the add_library
+# wrapper never sets it - so it cannot cover a MODULE target. A Ninja pool of one
+# (ninja is the Windows generator) orders these links, and with them their post-link
+# steps, against each other and nothing else, and it cannot hide a real link error.
+# HONEST LIMIT: this closes the window the race needs, the first burst of writers
+# into a fresh build\tests. If a later change makes another target in this file the
+# first writer into that directory, the same property belongs on it too, or on every
+# target here via CMAKE_JOB_POOL_LINK.
+if(MSVC)
+	# Ninja-only properties: ignored by other generators, and ninja errors if a rule
+	# names a pool that JOB_POOLS does not declare, so the two travel together.
+	set_property(GLOBAL APPEND PROPERTY JOB_POOLS zene_partc_ref_link=1)
+endif()
+
 foreach(ENTRY IN LISTS PART_C_REFERENCE_MODULES)
 	string(REPLACE "|" ";" FIELDS "${ENTRY}")
 	list(GET FIELDS 0 PLUGIN_ID)
@@ -414,6 +461,11 @@ foreach(ENTRY IN LISTS PART_C_REFERENCE_MODULES)
 	endif()
 
 	add_library(partc_ref_${PLUGIN_ID} MODULE ${PLUGIN_SOURCES})
+	if(MSVC)
+		# The link and its post-link applocal step, one reference module at a time;
+		# see the pool note above this loop.
+		set_property(TARGET partc_ref_${PLUGIN_ID} PROPERTY JOB_POOL_LINK zene_partc_ref_link)
+	endif()
 	target_include_directories(partc_ref_${PLUGIN_ID} PRIVATE
 		"${PART_C_REFERENCE_DIR}"
 		"${PART_C_REFERENCE_DIR}/${PLUGIN_DIR}"
```

`git apply --check` against `post-alpha/integration` (`b9b51296d`) exits 0. The patch goes above the
`foreach` and immediately after `add_library`, so it needs no other edit. It does not touch
`tests/reference/` (the frozen copies stay frozen — this is a consuming-target change, the same
shape as the HiDPI/Qt6 `-Wno-deprecated-declarations` waiver already in that loop), and it does not
move Gate 7: `tests/CMakeLists.txt` is in neither `tests/fork-sources.txt` nor `tests/all-sources.txt`,
so the added comment lines are not measured by the line ratchet.

## Local verification (Linux, GCC, on the exact patch bytes above)

| step | command | result |
|---|---|---|
| configure, unpatched | `cmake -S . -B /tmp/build-msvc-fix -DWANT_QT6=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTARGET_UARCH=official` | exit 0 |
| fresh configure, patched | same into a new dir, with `-DUSE_COMPILE_CACHE=ON` | exit 0 |
| build the target, patched | `cmake --build /tmp/build-msvc-fix-final --target partc_ref_bassbooster -j4` | exit 0; 369 objects compiled, `zene` linked, `tests/partc_ref_bassbooster.so` 1472864 bytes exporting `lmms_plugin_main` |
| Ninja mechanism check | `cmake -G Ninja` with the two guards forced to `if(TRUE)` (so the `MSVC`-only branch is reachable on Linux), then read the generated files | `CMakeFiles/rules.ninja` declares `pool zene_partc_ref_link` / `depth = 1`; **43/43** `tests/partc_ref_*.so` link edges carry `pool = zene_partc_ref_link`; **0** of the 44 non-`partc_ref` tests link edges do; `ninja -n tests/partc_ref_bassbooster.so` loads the manifest (exit 0), which ninja refuses if a pool is undeclared |

43 locally vs 41 in CI is the expected platform difference: `sf2player` and `gigplayer` are gated
on a soundfont / `.gig` this box has and the Windows runner does not (`malletsstk` is absent from
both).

## What is still unverified

- **Any MSVC behaviour.** The Windows link, the `vcpkg z-applocal` step and the sharing violation
  cannot be reproduced on this Linux box by construction. The mechanism above is read out of the CI
  log and the vcpkg/CMake sources; it is not reproduced.
- **That this fix removes the failure in CI.** Only an `msvc-x64` run can show that — and the
  failure appeared once in the ~12 msvc-x64 jobs checked, so a single green run is weak evidence
  either way.
- **Which process held the file.** A sharing violation can also come from outside the build
  (Defender scanning `D:\a\...`). Ordering the link bursts removes the in-build race; it does not
  by itself prove the holder was a sibling `z-applocal`.

## Later cycles

Cycle 8 (`msvc-x64` job `103706773250`, run `34750729517`) did **not** repeat this failure:
`[851/2236] Linking CXX shared module tests\partc_ref_bassbooster.dll` succeeded there, no "another
process" line appears in that job's log, and its only FAILED edge is a compile error in a different
target (`error C2491` at `plugins\Vst3Effect\Vst3Effect.cpp(45)`, diagnosed and patched separately in
`docs/MSVC-VST3-TEST-DLLIMPORT.md`). The two are independent causes; neither masks the other.

## Reproducing the evidence

```sh
zene-gh-token gh run list --repo KRUZZZZY/zene-studio --workflow build --limit 20
TOKEN=$(cat ~/.config/gh-token-zene | tr -d '\n\r')
curl -sL -H "Authorization: Bearer $TOKEN" \
  "https://api.github.com/repos/KRUZZZZY/zene-studio/actions/jobs/103701272689/logs" > /tmp/msvc.log
grep -n "FAILED:\|another process\|LNK2005\|LNK1169\|LNK2019\|LNK1120" /tmp/msvc.log
```
