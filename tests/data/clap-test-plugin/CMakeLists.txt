# The CLAP test plug-in is the subject of ClapHostTest and
# ClapEffectIntegrationTest. It is built from the pinned CLAP headers (MIT) and
# only exists when those headers are available.
add_library(clap-test-gain MODULE clap-test-gain.c)

set_target_properties(clap-test-gain PROPERTIES
	PREFIX ""
	SUFFIX ".clap"
	C_STANDARD 11
	C_STANDARD_REQUIRED ON
)

target_link_libraries(clap-test-gain PRIVATE lmms_clap)

# The INSTRUMENT subject (feature row 79, board task #669): the same kind of
# real CLAP module, but a generator - clap.note-ports with one CLAP-dialect
# input port and clap.audio-ports with an output port and no input port. It is
# what proves the note path end to end: a note goes in, audio comes out.
add_library(clap-test-instrument MODULE clap-test-instrument.c)

set_target_properties(clap-test-instrument PROPERTIES
	PREFIX ""
	SUFFIX ".clap"
	C_STANDARD 11
	C_STANDARD_REQUIRED ON
)

target_link_libraries(clap-test-instrument PRIVATE lmms_clap)

# Plain C plug-ins, no LMMS code, no Qt: keep the warnings strict so the test
# subjects stay clean. -Wall and -Wextra are GNU-style flags and cl.exe rejects
# -Wextra outright ("cl : Command line error D8021 : invalid numeric argument
# '/Wextra'"), so they are guarded the way the rest of this tree guards a
# GNU-style flag - the same shape as tests/CMakeLists.txt:625. MSVC needs no flag
# of its own here: cmake/modules/ErrorFlags.cmake already gives every non-SYSTEM
# target /W2 plus the /external: policy this target inherits.
if(NOT MSVC)
	target_compile_options(clap-test-gain PRIVATE -Wall -Wextra)
	target_compile_options(clap-test-instrument PRIVATE -Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# The ONE-FAULT modules: the test subjects of ClapLoaderErrorTest, the test of
# the host's typed error path.
#
# plugins/ClapEffect/ClapLoader.cpp promises that a failed load returns a CODE
# ("library not found" is not "not a CLAP module" is not "a version this host
# cannot host"), and a code can only be proven against a module that fails in
# exactly that one way. Each fault is structural - the symbol is absent, the
# declared version is old, init() returns false, the factory is null - so each
# one is invisible to the other three by construction; that is what makes a
# wrong code a real defect rather than a fixture artefact.
#
# One source, four modules: tests/data/clap-test-plugin/clap-test-broken.c is
# compiled once per fault with CLAP_TEST_FAULT_<FAULT> defined, and refuses to
# compile without one (the #error at the end of that file) so a module can never
# silently carry zero faults or two.
#
# The same -Wall/-Wextra guard as clap-test-gain above, for the same reason.
# ---------------------------------------------------------------------------
set(LMMS_CLAP_BROKEN_FAULTS no-symbol old-version init-fails no-factory)
foreach(LMMS_CLAP_BROKEN_FAULT IN LISTS LMMS_CLAP_BROKEN_FAULTS)
	string(REPLACE "-" "_" LMMS_CLAP_BROKEN_SUFFIX "${LMMS_CLAP_BROKEN_FAULT}")
	string(TOUPPER "${LMMS_CLAP_BROKEN_SUFFIX}" LMMS_CLAP_BROKEN_SUFFIX)
	set(LMMS_CLAP_BROKEN_TARGET "clap-test-broken-${LMMS_CLAP_BROKEN_FAULT}")

	add_library(${LMMS_CLAP_BROKEN_TARGET} MODULE clap-test-broken.c)
	set_target_properties(${LMMS_CLAP_BROKEN_TARGET} PROPERTIES
		PREFIX ""
		SUFFIX ".clap"
		C_STANDARD 11
		C_STANDARD_REQUIRED ON
	)
	target_link_libraries(${LMMS_CLAP_BROKEN_TARGET} PRIVATE lmms_clap)
	target_compile_definitions(${LMMS_CLAP_BROKEN_TARGET} PRIVATE
		"CLAP_TEST_FAULT_${LMMS_CLAP_BROKEN_SUFFIX}=1")
	if(NOT MSVC)
		target_compile_options(${LMMS_CLAP_BROKEN_TARGET} PRIVATE -Wall -Wextra)
	endif()
endforeach()
