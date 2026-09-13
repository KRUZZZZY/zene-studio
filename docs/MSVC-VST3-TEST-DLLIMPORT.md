# msvc-x64: `error C2491: definition of dllimport data not allowed` at plugins\Vst3Effect\Vst3Effect.cpp(45)

Status: **diagnosed, fix prepared, not applied here** — the fix belongs in `tests/CMakeLists.txt`,
which the runtime-failure lane owns for this wave. Patch: "The fix" below and
`/tmp/msvc-cycle8-vst3-dllimport.patch` (sha256 `6c8cc33c500fa0eadda339ed64d9d3d886808e87a186d78d64c970c47ae6d3e9`). It is an independent hunk from the
`partc_ref` pool patch (`/tmp/msvc-partc-ref.patch`, sha256 `f225d8cd26953ececc6d0893165eb7de3a96f8bd4bf319ba0e47ffccff712bb5`); both apply cleanly on their
own and together against `post-alpha/integration` (`b9b51296d`).

## The exact failure

`msvc-x64`, job `103706773250` (run `34750729517`, branch `v0.2.1-alpha`), edge `2011/2236`:

```
[2011/2236] Building CXX object tests\CMakeFiles\Vst3EffectIntegrationTest.dir\__\plugins\Vst3Effect\Vst3Effect.cpp.obj
FAILED: [code=2] tests/CMakeFiles/Vst3EffectIntegrationTest.dir/__/plugins/Vst3Effect/Vst3Effect.cpp.obj
... cl.exe /nologo /TP -DLMMS_STATIC_DEFINE -DLMMS_TESTING -DNDEBUG ... -DPLUGIN_NAME=vst3effect ...
D:\a\zene-studio\zene-studio\plugins\Vst3Effect\Vst3Effect.cpp(45): error C2491: 'lmms::vst3effect_plugin_descriptor': definition of dllimport data not allowed
```

That is the **only** `error C` line in the whole job log, and it is a compile error (`code=2`), not a
link error — the target's other objects (Vst3BusMap, Vst3EffectControlDialog, Vst3MidiEvent,
Vst3EffectControls, Vst3Host and the test TU itself) all compiled in the same burst.

## The mechanism

`tests/CMakeLists.txt` gives `Vst3EffectIntegrationTest` the **generated** export header of the
`vst3effect` plug-in target on its include path:

```cmake
	target_include_directories(Vst3EffectIntegrationTest PRIVATE
		# generated plugin_export.h (PLUGIN_EXPORT) of the vst3effect plug-in
		${CMAKE_BINARY_DIR}/plugins/Vst3Effect
```

and that header comes from `BUILD_PLUGIN` → `GENERATE_EXPORT_HEADER(${PLUGIN_NAME} BASE_NAME PLUGIN)`
(`cmake/modules/BuildPlugin.cmake`). CMake's generator writes, for a compiler that has `__declspec`
(`GenerateExportHeader.cmake:306-307`):

```
#  ifdef vst3effect_EXPORTS
        /* We are building this library */
#    define PLUGIN_EXPORT __declspec(dllexport)
#  else
        /* We are using this library */
#    define PLUGIN_EXPORT __declspec(dllimport)
```

The test target is not `vst3effect`, so `vst3effect_EXPORTS` is not defined for it and
`PLUGIN_EXPORT` resolves to **`__declspec(dllimport)`**. The target compiles the plug-in's own
sources — which is the point of the test, "the descriptor and controls under test are the shipped
ones" — and `plugins/Vst3Effect/Vst3Effect.cpp:45` *defines* the descriptor:

```cpp
Plugin::Descriptor PLUGIN_EXPORT vst3effect_plugin_descriptor =
```

Defining a `dllimport` symbol is C2491. On GCC/Clang the same header resolves both branches to
`__attribute__((visibility("default")))`, which is legal on a definition, so the whole class is
MSVC-only and Linux CI cannot see it. (The header's own static branch — `PLUGIN_STATIC_DEFINE` →
empty `PLUGIN_EXPORT` — is textually identical on every compiler.)

`PLUGIN_EXPORT` appears in exactly one TU per plug-in — the descriptor definition — and in the
IntegrationTest TU that re-declares it (`tests/src/plugins/Vst3EffectIntegrationTest.cpp:113`).
`Vst3BusMapTest` and `Vst3HostTest` compile the plug-in's other sources, touch no `PLUGIN_EXPORT`
symbol, and are unaffected: only the two `*IntegrationTest` targets need this.

The `ClapEffectIntegrationTest` target is built from the same shape (`plugins/ClapEffect/ClapEffect.cpp:44`
defines `clapeffect_plugin_descriptor` with `PLUGIN_EXPORT`) and carries the same latent defect. It
does not fail on `msvc-x64` today only because that job configures `-DWANT_CLAP=OFF`; the patch fixes
it too rather than leaving the twins different.

## The fix

`PLUGIN_STATIC_DEFINE` on the two targets that compile a plug-in's descriptor-defining TU into
themselves. That is the static branch of the plug-in's generated export header, which is the correct
one: these binaries link the plug-in's code in, they do not import it — the same idea as the
`LMMS_STATIC_DEFINE` that `target_static_libraries(... lmmsobjs)` already passes down to them.

```diff
diff --git a/tests/CMakeLists.txt b/tests/CMakeLists.txt
--- a/tests/CMakeLists.txt
+++ b/tests/CMakeLists.txt
@@ -706,12 +706,20 @@ if(TARGET lmms_clap)
 		${QT_QTTEST_LIBRARY}
 		lmms_clap
 		${CMAKE_DL_LIBS}
 	)
 	target_compile_definitions(ClapEffectIntegrationTest PRIVATE
 		LMMS_TESTING
+		# Same construct as the VST3 twin below - this target compiles the
+		# plug-in's own ClapEffect.cpp into the test executable, and that TU
+		# defines clapeffect_plugin_descriptor with PLUGIN_EXPORT, so the
+		# plug-in's generated plugin_export.h must not mark it dllimport. Latent
+		# on msvc-x64 today only because that job configures -DWANT_CLAP=OFF
+		# (build.yml); fixed here so the twins stay twins. The C2491 evidence is
+		# in the VST3 block.
+		PLUGIN_STATIC_DEFINE
 		PLUGIN_NAME=clapeffect
 		CLAP_TEST_PLUGIN_PATH="$<TARGET_FILE:clap-test-gain>"
 	)
 endif()
 
 # Native VST3 hosting tests — the twins of the CLAP block above, and the reason
@@ -815,12 +823,25 @@ if(TARGET lmms_vst3_sdk)
 		${QT_QTTEST_LIBRARY}
 		lmms_vst3_sdk
 		${CMAKE_DL_LIBS}
 	)
 	target_compile_definitions(Vst3EffectIntegrationTest PRIVATE
 		LMMS_TESTING
+		# This target compiles the plug-in's own sources into the test executable,
+		# Vst3Effect.cpp included - and that TU *defines*
+		# vst3effect_plugin_descriptor with PLUGIN_EXPORT. For a consumer, the
+		# generated plugin_export.h of the vst3effect target resolves
+		# PLUGIN_EXPORT to __declspec(dllimport) (GENERATE_EXPORT_HEADER in
+		# cmake/modules/BuildPlugin.cmake), and defining a dllimport symbol is
+		# "error C2491: definition of dllimport data not allowed" - msvc-x64 job
+		# 103706773250 (run 34750729517) died at plugins\Vst3Effect\Vst3Effect.cpp(45)
+		# with exactly that. The static branch of that same header is the correct
+		# one here: this binary links the plug-in's code in, it does not import it.
+		# (LMMS_STATIC_DEFINE, which target_static_libraries already passes down to
+		# these targets, is the same idea for lmms' own export header.)
+		PLUGIN_STATIC_DEFINE
 		PLUGIN_NAME=vst3effect
 		VST3_TEST_PLUGIN_PATH="${LMMS_VST3_TEST_EFFECT_BUNDLE_DIR}"
 	)
 	add_dependencies(Vst3EffectIntegrationTest vst3-test-effect)
 endif()
 
```

## Local verification (Linux, GCC, on the exact patch bytes above)

| step | command | result |
|---|---|---|
| fresh configure with the SDK | `cmake -S . -B /tmp/build-c8 -DWANT_QT6=ON -DWANT_VST3=ON -DLMMS_VST3_SDK_PATH=/tmp/vst3sdk -DLMMS_CLAP_PATH=/tmp/clap -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTARGET_UARCH=official -DUSE_COMPILE_CACHE=ON` | exit 0; `Found VST3 SDK 3.8 (MIT)`, `Found CLAP 1.2.10 headers (MIT)` |
| build both affected targets | `cmake --build /tmp/build-c8 --target Vst3EffectIntegrationTest ClapEffectIntegrationTest -j4` | exit 0, both linked |
| run them | `cd /tmp/build-c8/tests && QT_QPA_PLATFORM=offscreen ctest -R "Vst3EffectIntegrationTest\|ClapEffectIntegrationTest" --output-on-failure` | exit 0, **2/2 passed** (1.35 s / 1.40 s) |
| the define actually selects the static branch | `g++ -E -P -I/tmp/build-c8/plugins/Vst3Effect` on a TU that includes `plugin_export.h` and uses `PLUGIN_EXPORT`, with and without `-DPLUGIN_STATIC_DEFINE` | without → `__attribute__((visibility("default"))) void f();`; with → `void f();` — i.e. the consumer branch is gone when the define is set, and the static branch is compiler-independent |

## What is still unverified

- **The MSVC compile itself.** `cl.exe` does not exist on this box; the local build proves the
  targets still configure, compile and run with the define, and the preprocessor check proves it
  selects the static branch, but the removal of C2491 is an argument from the generated header's
  text plus CMake's own template, not a reproduced Windows build.
- **The CLAP twin's msvc behaviour**, for the same reason (and because that job builds it never).

## Cycle 8 vs the `partc_ref_bassbooster.dll` failure (cycle 6)

Cycle 8 does **not** mask it: `[851/2236] Linking CXX shared module tests\partc_ref_bassbooster.dll`
succeeded in this very job, no "another process" line appears anywhere in its log, and the C2491
compile above is the job's only FAILED edge. The two are independent causes — a compile-time
MSVC export defect in a newly registered test target, and the intermittent `vcpkg z-applocal`
sharing race documented in `docs/MSVC-APPLOCAL-LINK-RACE.md` — and each needs its own fix.

## Reproducing the evidence

```sh
TOKEN=$(cat ~/.config/gh-token-zene | tr -d '\n\r')
curl -sL -H "Authorization: Bearer $TOKEN" \
  "https://api.github.com/repos/KRUZZZZY/zene-studio/actions/jobs/103706773250/logs" > /tmp/msvc-c8.log
grep -n "error C\|FAILED:\|partc_ref_bassbooster" /tmp/msvc-c8.log
```
