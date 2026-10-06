# Mixer metering: ballistics, the Selected filter, live input meters and the phase view

**Lane:** `050/feat-meters` (worktree `zene-feat-meters`), based on `0.5.0/ui-train` @ `6d1a1143d`.
**Backlog rows:** MX-128, MX-005, MX-134, MX-137 (`plans/BACKLOG-050-FEATURES.md` in the program root).
**Tests:** `tests/src/gui/MixerMeteringTest.cpp` (the real application, offscreen) and the socket checks
added to `tests/control-mixing-routing.py` (`ControlMixingRouting`). What none of it does is in
`docs/KNOWN-LIMITATIONS.md`, section "0.5.0 L08 metering".

## 1. MX-128 — meter ballistics

Before this lane the meter's *scale* was a view option but its *ballistics* were constants: the bars
fell by 1.25 per 60 Hz tick in two places (`MixerView::updateFaders` and
`mixerviewoptions::feedMeter`) and the peak line held for a fixed 1500 ms inside `Fader::setPeak`.

Now both are view options beside the others, set by `window.mixer_view`, kept in the config file
(`ui/mixerview`) and offered in the mixer's **Mixer View** menu (**Meter Fall-Off**, **Peak Hold**):

| option | values | default |
|---|---|---|
| `meter_falloff` | `fast` (1.25 per tick, about 116 dB/s), `medium` (about 26 dB/s), `slow` (about 12 dB/s, a peak programme meter's 20 dB in 1.7 s) | `fast` - the inherited constant |
| `peak_hold` | `off` (the line is the level), `1.5s`, `3s`, `6s` | `1.5s` - the inherited hold |

The defaults are the inherited numbers, so a mixer nobody configures meters exactly as before.
`mixerviewoptions::meterFallOff()` feeds both fall-off sites; `applyToStrip` hands each strip's
`Fader` its hold through the new `Fader::setPeakHoldMs`. All of it is GUI-thread work.

Test-first record (`MixerMeteringTest`): before the change the test did not compile (no
`meterFallOff`, no `Fader::peakHoldMs`; build EXIT=2); after it, 12/12 pass (ctest EXIT=0) - the
defaults are the inherited ones, the options are answered, refused by a bad name and saved, the
strip's meter falls by exactly the chosen factor in the peak and the pre-fader modes, and the hold
line holds for each time and follows the level when off.

## 2. MX-005 — show only some channels ("Selected")

The mixer's filters were by type (all, instruments, audio, returns, used). A sixth, **selected**, shows
the master and the channels picked for it:

- `window.mixer_view` takes `selected_channels` (ch-<n> ids) and answers them in mixer order; a channel
  that does not exist is refused `not_found`, the master `invalid_args` (it always shows), and a refused
  call changes nothing;
- the mode bar's **Show** box lists **Selected**, and its **Pick...** button (declaring
  `controlCommand` `window.mixer_view`) opens the channel checklist and switches the filter to it;
- **Ctrl+click** on a strip adds its channel to the set or takes it out (through the same command), and a
  selected strip wears an accent bar along its foot.

The set is kept by each channel's stable id (`MixerChannel::id()`), so it follows a channel when another
is moved or deleted, and `MixerView::clear()` (what `Song::clearProject` calls) forgets it, because a new
project's ids start again.

Test-first record (`MixerMeteringTest`): red - the test did not compile (no
`mixerviewoptions::isSelected`; build EXIT=2); green - 18/18 (ctest EXIT=0). `ControlMixingRouting`
checks the set-and-answer and the master's refusal over the socket.

