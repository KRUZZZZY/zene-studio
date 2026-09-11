INCLUDE(BuildPlugin)

IF (NOT MSVC)
	# portsmf raises these warnings, which will prevent compilation with -Werror.
	ADD_COMPILE_OPTIONS(-Wno-error=unknown-pragmas)
	IF (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
		# -Wmaybe-uninitialized and -Wrestrict are GCC spellings. clang (including
		# AppleClang) does not know them and prints "unknown warning option" for
		# every file it compiles (CI run 34618027143: 42 lines across the three
		# clang jobs), so only GCC gets them.
		ADD_COMPILE_OPTIONS(-Wno-error=maybe-uninitialized -Wno-error=restrict)
	ENDIF()
ELSE()
	# The MSVC arm of the same suppression. Without it, vendored portsmf is compiled
	# under /WX with no exemptions at all while the non-MSVC arms above get the
	# exemptions their compiler understands — the existing
	# comment above states these warnings "prevent compilation with -Werror", and
	# /WX is that same policy on MSVC. C4068 (unknown #pragma) is the direct analogue
	# of -Wno-error=unknown-pragmas; other MSVC diagnostics get added from a real log
	# rather than guessed at.
	ADD_COMPILE_OPTIONS(/wd4068)
ENDIF()

# For now, portsmf files are included directly in the plugin.
INCLUDE_DIRECTORIES("portsmf/include")
BUILD_PLUGIN(midiimport MidiImport.cpp MidiImport.h
	portsmf/include/allegro.h portsmf/src/allegro.cpp
	portsmf/include/mfmidi.h portsmf/src/mfmidi.cpp
	portsmf/include/strparse.h portsmf/src/strparse.cpp
	# portsmf/src/allegrosmfwr.cpp portsmf/src/allegrowr.cpp
	portsmf/src/allegrosmfrd.cpp portsmf/src/allegrord.cpp
	portsmf/src/algsmfrd_internal.h portsmf/src/algsmfrd_internal.h
	MOCFILES MidiImport.h)
