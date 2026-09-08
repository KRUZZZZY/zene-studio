--! lmms-api 0.1
--
-- midi-router.lua - LMMS Lua API v0 example 3 (spec section 7).
--
-- v0 has no event callbacks, so MIDI-in is a polled buffer: every note-on
-- still in the buffer is mirrored a fifth up onto a second track, and the
-- transposed note is also sent to MIDI-out. Run it whenever you want the
-- buffered events routed (menu action / shortcut in v0).

local patterns = lmms.song():patternStore()

local source = patterns:addInstrumentTrack()
source:setName("MIDI In")

local destination = patterns:addInstrumentTrack()
destination:setName("MIDI Transposed")

local clip = patterns:addPattern()
clip:setName("Transposed")
clip:addCheckPoint()   -- single undo step for everything routed below

local transpose = 7                    -- a fifth up
local step = lmms.ticksPerBar() / 16
local routed = 0

while lmms.midiIn():hasEvent() do
	local event = lmms.midiIn():next()
	if event:isValid() and event:isNoteOn() then
		local key = event:key() + transpose
		clip:addNoteAt(key, routed * step, step / 2, event:velocity())
		lmms.midiOut():noteOn(event:channel(), key, event:velocity())
		lmms.midiOut():noteOff(event:channel(), key)
		routed = routed + 1
	end
end

lmms.log():info(string.format("midi-router: routed %d note(s) to '%s'",
	routed, destination:name()))
