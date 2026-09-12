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
assert(require == nil, "require must not be exposed")
