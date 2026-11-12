--! zene-api 0.1
--
-- controller-surface.lua - a control-surface script (Customise > Controller Scripts > Start Script...,
-- or controller.script_start). It is not a one-shot script: Zene keeps it loaded and calls on_midi for
-- every event the bound MIDI input sends, on the interface thread, never on the audio thread.
--
-- What this example does, on any controller (no model is assumed):
--   CC 7 on any channel     -> the master fader (mixer.set_volume), and the value echoed back as feedback
--   note 94 (Mackie "Play") -> transport.play,  and the Play LED lit (note 94, velocity 127)
--   note 93 (Mackie "Stop") -> transport.stop,  and the Play LED turned off
-- Feedback goes to the MIDI output the script was started with.

local master = nil

function on_start()
	-- The master channel's id, from the mixer's own state (ids are stable, not positions).
	local mixer = zene.command("mixer.get_state")
	for _, channel in ipairs(mixer and mixer.channels or {}) do
		if channel.is_master then master = channel.id end
	end
	surface.log("controller-surface ready")
end

function on_midi(event)
	if event.type == "cc" and event.controller == 7 then
		local reply, message = zene.command("mixer.set_volume", { channel = master, volume = event.value / 127 })
		if reply then
			surface.send_cc(event.channel, 7, event.value)
		else
			surface.log("fader refused: " .. tostring(message))
		end
	elseif event.type == "note_on" and event.velocity > 0 and event.key == 94 then
		if zene.command("transport.play") then surface.send_note(event.channel, 94, 127) end
	elseif event.type == "note_on" and event.velocity > 0 and event.key == 93 then
		if zene.command("transport.stop") then surface.send_note(event.channel, 94, 0) end
	end
end
