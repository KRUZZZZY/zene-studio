--! zene-api 0.1
--
-- generative-bass.lua - Zene Studio Lua API v0 example 2 (spec section 7).
--
-- Seeded-RNG arpeggio in a natural minor scale over 8 bars, one 16th note per
-- step. The seed is fixed so every run produces the same pattern.

local patterns = zene.song():patternStore()

local track = patterns:addInstrumentTrack()
track:setName("Generative Bass")

local clip = patterns:addPattern()
clip:setName("Minor Arp")

clip:addCheckPoint()   -- single undo step for the generated pattern

math.randomseed(20260908)

local root = 36                       -- C2
local minor = { 0, 2, 3, 5, 7, 8, 10 }
local step = zene.ticksPerBar() / 16
local bars = 8
local stepsPerBar = 16
local velocity = 96

for bar = 0, bars - 1 do
	for s = 0, stepsPerBar - 1 do
		local degree = minor[math.random(#minor)]
		local octave = math.random(0, 1) * 12
		local key = root + degree + octave
		local pos = (bar * stepsPerBar + s) * step
		clip:addNoteAt(key, pos, step - 1, velocity)
	end
end

zene.log():info(string.format("generative-bass: %d notes over %d bars",
	clip:noteCount(), bars))
