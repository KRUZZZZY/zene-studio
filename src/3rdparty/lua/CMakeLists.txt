# Lua 5.4.8 (MIT) -- vendored scripting runtime, see COPYRIGHT and README.lmms
#
# The standalone interpreter and bytecode compiler (lua.c, luac.c) are
# intentionally excluded: LMMS embeds the library only. lua.c also pulls in
# readline via LUA_USE_LINUX, which we do not want in the build.
set(LUA_SRC
	lapi.c
	lauxlib.c
	lbaselib.c
	lcode.c
	lcorolib.c
	lctype.c
	ldblib.c
	ldebug.c
	ldo.c
	ldump.c
	lfunc.c
	lgc.c
	linit.c
	liolib.c
	llex.c
	lmathlib.c
	lmem.c
	loadlib.c
	lobject.c
	lopcodes.c
	loslib.c
	lparser.c
	lstate.c
	lstring.c
	lstrlib.c
	ltable.c
	ltablib.c
	ltm.c
	lundump.c
	lutf8lib.c
	lvm.c
	lzio.c
)

add_library(lua STATIC ${LUA_SRC})
target_include_directories(lua PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(lua PROPERTIES
	POSITION_INDEPENDENT_CODE ON
	# Lua is plain C89-compatible C; keep the project-wide C++20 settings out
	# of it.
	C_STANDARD 99
	C_STANDARD_REQUIRED OFF
)
