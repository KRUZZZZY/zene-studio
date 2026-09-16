# Native CLAP hosting (task #588). CLAP is MIT licensed (github.com/free-audio/
# clap), so it needs no licence exception, no clean room and no Vestige shim.
# The headers are not vendored; see ../../cmake/modules/ClapHeaders.cmake for
# the pin (1.2.10, commit 195b42a004144fab0b3cf95e9c067187d15365b7) and how to
# obtain them.
SET(LMMS_CLAP_PATH "${CMAKE_BINARY_DIR}/clap"
	CACHE PATH "CLAP header checkout (>= 1.2.10)")
SET(WANT_CLAP "AUTO" CACHE STRING "Build native CLAP hosting: AUTO, ON or OFF")

IF(WANT_CLAP STREQUAL "OFF")
	MESSAGE(STATUS "CLAP hosting disabled (-DWANT_CLAP=OFF)")
	RETURN()
ENDIF()
IF(WANT_CLAP STREQUAL "AUTO" AND NOT EXISTS "${LMMS_CLAP_PATH}/include/clap/clap.h")
	MESSAGE(STATUS "CLAP hosting skipped: no headers at '${LMMS_CLAP_PATH}/include/clap' "
		"(clone instructions in cmake/modules/ClapHeaders.cmake, then re-run cmake)")
	RETURN()
ENDIF()

INCLUDE(ClapHeaders)

SET(LMMS_CLAP_EFFECT_SOURCES
	ClapBusMap.cpp
	ClapEffect.cpp
	ClapEffectControlDialog.cpp
	ClapEffectControls.cpp
	ClapHost.cpp
	ClapHostLifecycle.cpp
	ClapHostNotes.cpp
	ClapHostParams.cpp
	ClapLoader.cpp
	ClapParameter.cpp
	ClapSubPluginFeatures.cpp
)

SET(LMMS_CLAP_EFFECT_MOCFILES
	ClapEffectControlDialog.h
	ClapEffectControls.h
	ClapParameter.h
)

BUILD_PLUGIN(clapeffect
	${LMMS_CLAP_EFFECT_SOURCES}
	MOCFILES
		${LMMS_CLAP_EFFECT_MOCFILES}
	EMBEDDED_RESOURCES
		"${CMAKE_CURRENT_SOURCE_DIR}/logo.png"
)

TARGET_LINK_LIBRARIES(clapeffect lmms_clap ${CMAKE_DL_LIBS})

IF(MSVC)
	# This module links the executable's import library (zene.lib), which offers definitions of the
	# same std::vector<float> members ClapHost.cpp.obj instantiates itself, so the link collides:
	#   zene.lib(zene.exe) : error LNK2005: "public: __cdecl std::vector<float,...>::vector<float,...>(void)"
	#                        already defined in ClapHost.cpp.obj
	#   zene.lib(zene.exe) : error LNK2005: "public: __cdecl std::vector<float,...>::~vector<float,...>(void)"
	#                        already defined in ClapHost.cpp.obj
	#   zene.lib(zene.exe) : error LNK2005: "public: float * __cdecl std::vector<float,...>::data(void)"
	#                        already defined in ClapHost.cpp.obj
	#   plugins\clapeffect.dll : fatal error LNK1169: one or more multiply defined symbols found
	# (CI msvc-x64, run 34789449244 job 103810839304, [1373/2443] Linking CXX shared module
	# plugins\clapeffect.dll - the LAST Build failure of that job, and Build is the step that hides
	# every MSVC test.)
	#
	# WHY zene.lib carries them, checked against the source rather than inferred from the error: the
	# export side is CMake's, not an LMMS export macro. src/CMakeLists.txt:255 sets ENABLE_EXPORTS ON
	# on the executable, and on Windows CMake generates that import library's export list by walking
	# the COFF symbol tables of every object that goes into zene.exe and keeping every external symbol
	# with a non-zero section number (CMake Source/bindexplib.cxx, DumpExternalsObjects()). Inline and
	# class-template members are NOT filtered, so every std::vector<float> member any of the
	# executable's own TUs instantiate (src/core/MasteringJob.cpp:270,307, src/core/fft_helpers.cpp,
	# src/core/SampleDecoder.cpp:160, ...) is exported and offered as a definition.
	# cmake/modules/BuildPlugin.cmake:54 links 'zene' into every plug-in module. This module then
	# instantiates the same three members for its own real-time scratch buffers
	# (ClapHost.cpp:225-226, `std::vector<float> silence / outputScratch`), which is the other half of
	# the collision. MSVC folds identical COMDATs from objects, but not a definition offered by
	# the import library, hence LNK2005 rather than silence.
	#
	# Both definitions are the same STL implementation from the same toolchain, so collapsing them
	# changes nothing at runtime, and the flag is scoped to this target so it cannot hide a duplicate
	# anywhere else. Precedent: plugins/NeuralAmp/CMakeLists.txt lines 51-73 (the same collision, the
	# scoped flag) and plugins/Vst3Effect/CMakeLists.txt lines 48-84 (the same collision against
	# zene.lib specifically, and the same audit trail). Each collapsed symbol still appears as LNK4006
	# in the build log, so what the flag hides stays auditable.
	#
	# HONEST LIMIT, the same one the two precedents record: this is a workaround, not the root cause,
	# and it cannot be reproduced on the Linux dev box (no MSVC toolchain). The durable fix is a
	# filtered export list for the executable, or moving the plug-in-facing host API into a DLL;
	# neither is testable without a Windows CI loop, which is why the scoped flag stays.
	TARGET_LINK_OPTIONS(clapeffect PRIVATE /FORCE:MULTIPLE)
ENDIF()
