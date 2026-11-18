--! zene-api 0.1
--
-- hello.lua - Zene Studio Lua API v0 smoke test (spec section 9, phase G1).
--
-- Every call below goes through the LuaLog bridge; the captured output is
-- what the G1 gate observes in a headless build.

zene.log():info("Hello from Lua " .. _VERSION)
zene.log():info("Zene Studio Lua API " .. zene.version())

-- print() is routed through LuaLog as well, so it is captured too.
print("hello.lua finished")

-- Basic sandbox sanity: the dangerous stdlib must not be reachable.
assert(os == nil, "os must not be exposed")
assert(io == nil, "io must not be exposed")
assert(package == nil and dofile == nil and loadfile == nil, "file loaders must not be exposed")
-- `require` exists for script files, but only loads sibling modules (Q050-051).
assert(type(require) == "function", "sandboxed require expected")
