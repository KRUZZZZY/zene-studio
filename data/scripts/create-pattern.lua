--! lmms-api 0.1
--
-- create-pattern.lua - LMMS Lua API v0 example 1 (spec section 7).
--
-- Creates an instrument track and a pattern, then fills it with a 16-step
-- hi-hat line carrying velocity accents on every beat. The whole script is a
-- single undo step: the journal checkpoint is taken while the pattern is still
-- empty, so one undo removes every note the script added (spec OQ-2 default).

local patterns = lmms.song():patternStore()

local track = patterns:addInstrumentTrack()
track:setName("Hi-Hat")

local clip = patterns:addPattern()
clip:setName("Hi-Hat 16")

-- Snapshot the empty clip before mutating it: this is the undo point.
clip:addCheckPoint()

local step = lmms.ticksPerBar() / 16   -- one 16th note, in ticks
local accent = 112
local ghost = 72

for i = 0, 15 do
	local velocity = ghost
	if i % 4 == 0 then
		velocity = accent              -- accent on every beat
	end
	clip:addNoteAt(42, i * step, step / 2, velocity)
end

lmms.log():info(string.format("create-pattern: %d notes in '%s' on '%s'",
	clip:noteCount(), clip:name(), track:name()))
