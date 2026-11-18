# Clip crossfades

Crossfades apply to audio clips on the same track. They are stored as the outgoing clip's fade-out and the incoming
clip's fade-in, and one edit is one undo step.

## Default: clamp to clip edges

An arranger crossfade and clip.crossfade use edge fades by default. The command reports mode "clamp" when no mode is
supplied. A contained pair does not create the old V-shaped offset fade on the container. The edited fade sides are
reset to zero offsets, so a crossfade without Alt cannot create offset fades.

The command accepts mode "clamp" explicitly. Its out_length and in_length arguments set the two edge ramp lengths;
if either is omitted, that ramp uses the overlap length. Each ramp must fit its own clip.

## Optional container gesture

Hold Alt while dropping a clip onto another clip on the same track to select a crossfade for that gesture. When one
clip lies wholly inside the other, the containing clip fades down at the inner clip's start and returns at its end.
For a plain overlap, Alt uses the ordinary edge crossfade.

The inner clip's authored fade-in and fade-out lengths set the two container ramps. Both must be positive and their
sum must fit inside the inner clip. When the lengths leave a gap, the container remains silent between the ramps.
The arranger skips an invalid container gesture and reports the reason. The same behavior is available through
clip.crossfade with mode "container"; invalid clips or ramps return invalid_args. An optional out_length or
in_length overrides the shared entry or exit ramp selected by the command's out/in order.

The command requires the two clips to overlap and to share a track. It writes both clip states under one Track
checkpoint, so one undo restores the edit.

## Project compatibility

Existing fadeinoffset and fadeoutoffset values continue to load and render as before. The new fadeoffsetgap="1"
attribute appears only on a container fade whose authored ramps leave a silent gap; files without that attribute
retain the earlier adjacent-ramp repair rule. The audio render path applies the gap with a bounded comparison and
no allocation.

Coverage: ClipOffsetFadeTest checks the default render, the Alt/container ramps, command validation and undo,
project round trips, old offset-pair behavior, and allocation-free rendering. ClipOffsetFadePairTest checks edge
clamping and the refusal for a tiny container-mode clip.
