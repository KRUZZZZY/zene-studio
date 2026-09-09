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

# Plain C plug-in, no LMMS code, no Qt: keep the warnings strict so the test
# subject stays clean.
target_compile_options(clap-test-gain PRIVATE -Wall -Wextra)
