INCLUDE(BuildPlugin)

SET(NEURALAMP_SOURCES
	NeuralAmpEffect.cpp
	NeuralAmpControls.cpp
	NeuralAmpControlDialog.cpp
	nam/NamModel.cpp
	nam/NamModelLoader.cpp
)

# RTNeural (BSD-3-Clause, header-only) is vendored under rtneural/. This plugin
# uses only two of its vendored submodules: Eigen (MPL-2.0, linear algebra) and
# nlohmann/json (MIT, .nam parsing). See LICENSE-NOTICE.md for the inventory.
# RTNeural's own layer code is not used (it has no .nam loader).
INCLUDE_DIRECTORIES(BEFORE
	"${CMAKE_CURRENT_SOURCE_DIR}"
	"${CMAKE_CURRENT_SOURCE_DIR}/rtneural/modules/Eigen"
	"${CMAKE_CURRENT_SOURCE_DIR}/rtneural/modules/json"
)

BUILD_PLUGIN(neuralamp
	${NEURALAMP_SOURCES}
	MOCFILES
		NeuralAmpEffect.h
		NeuralAmpControls.h
		NeuralAmpControlDialog.h
)

# libm is a Unix convention: on MSVC the math routines live in the CRT, and asking for
# m.lib fails the link outright (CI msvc-x64: "LNK1104: cannot open file 'm.lib'").
# Upstream guards its own 'm' links the same way — plugins/LadspaEffect/{swh,tap}.
TARGET_LINK_LIBRARIES(neuralamp ${CMAKE_DL_LIBS})
IF(NOT MSVC)
	TARGET_LINK_LIBRARIES(neuralamp m)
ENDIF()

# The inference loops are the hot path; make sure they are optimised even if the
# host build type is not Release.
TARGET_COMPILE_OPTIONS(neuralamp PRIVATE $<$<CONFIG:Debug>:-O2>)

# Vendored Eigen (rtneural/modules/Eigen) trips -Wdeprecated-enum-enum-conversion in its
# NEON kernels on aarch64 — the linux-arm64 CI job — and with -DUSE_WERROR that vendored
# diagnostic becomes an error in code this repo does not modify. Keep the warning, drop
# its error status for this target only.
TARGET_COMPILE_OPTIONS(neuralamp PRIVATE
	"$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-Wno-error=deprecated-enum-enum-conversion>")

# Headless verification/benchmark binaries (EXCLUDE_FROM_ALL, see tests/).
ADD_SUBDIRECTORY(tests)

# --- MSVC: multiply-defined STL members against the executable's import library ---------
# CI msvc-x64 (run #22) links plugins\neuralamp.dll against src\lmms.lib — the *executable's*
# import library, which exports STL instantiations because LMMS's own exported classes use
# std::vector<float> in their inline members. This plugin's vendored NAM TUs instantiate the
# same members (??1?$vector@M…, data(), _Tidy, _Getal), and the link fails:
#
#   lmms.lib(lmms.exe) : error LNK2005: std::vector<float>::~vector(...) already defined in
#                        NamModelLoader.cpp.obj
#
# The two definitions are the same STL implementation from the same toolchain, so collapsing
# them changes nothing at runtime; /FORCE:MULTIPLE is scoped to this one target so it cannot
# hide a duplicate in anything else. 94 other plugin DLLs link the same import library without
# this collision, which is why the workaround is scoped rather than global.
#
# HONEST LIMIT: this is a workaround, not the root cause. The dialog is an MSVC export
# interaction that cannot be reproduced on the Linux dev box (the flags for the failing TUs
# are byte-identical to the plugin's other TUs, so it is not a flag mismatch). The clean fix,
# for whoever has a Windows CI loop: keep the STL type off the exported class surface — pimpl
# the buffer (NamModel.h:163 m_prewarmBuffer) or define the plugin class's destructor
# out-of-line in the .cpp so the inline template chain is not exported with it.
IF(MSVC)
	TARGET_LINK_OPTIONS(neuralamp PRIVATE /FORCE:MULTIPLE)
ENDIF()
