#!/usr/bin/env python3
"""Write the four headless-render fixtures the rack proof renders (#599).

    python3 tools/rack-render-fixture.py make /tmp/rackproof

Writes, into the given directory, four one-bar projects that differ only in the
rack on mixer channel 1:

  no-rack.mmp        a TripleOscillator through channel 1, whose own <fxchain>
                     holds one Amplifier at 100% -- today's project shape
  rack-select-a.mmp  the same, plus a <rack selected="0"> whose chain 1 holds an
                     Amplifier at 50%: the rack is on the path and routes the
                     channel's own chain, so this must render like no-rack.mmp
  rack-select-b.mmp  the same rack with selected="1": chain 1 alone, half gain
  rack-parallel.mmp  the same rack with selected="-1": both chains sum, so this
                     must render like rack-select-a + rack-select-b

The source is a synthesiser and not sample playback: LMMS's sample playback is
one of the things that makes a render non-reproducible run to run
(docs/STEM-EXPORT.md section 4.4), and this proof wants a same-build floor it can
trust. The projects are otherwise as small as LMMS accepts: one BB track whose
instrument track is routed to channel 1, one mixer with an explicit send to
master, no UI state. The render command and the comparisons are in docs/RACKS.md.
"""

import os
import sys

BPM = 140

#! One bar of notes: (position, key, volume, length), 192 ticks to the bar.
NOTES = [
    (0, 72, 100, 24),
    (24, 60, 90, 24),
    (48, 67, 80, 48),
    (96, 72, 100, 24),
    (120, 64, 70, 24),
    (144, 65, 100, 48),
]


def notes_xml():
    return "".join(
        '                <note key="%d" vol="%d" pos="%d" pan="0" len="%d"/>\n'
        % (key, vol, pos, length)
        for pos, key, vol, length in NOTES
    )


def amplifier(volume):
    """One <effect> element: the Amplifier's controls are attributes on the
    <AmplifierControls> child its own saveSettings() writes."""
    return (
        '        <effect name="amplifier" on="1" wet="1" autoquit="1">\n'
        '          <AmplifierControls volume="%d" pan="0" left="100" right="100"/>\n'
        '          <key/>\n'
        '        </effect>\n' % volume
    )


def chain_one(volume):
    """The rack's parallel chain: its own <fxchain>, exactly the element the
    channel's chain uses."""
    return (
        '          <chain index="1">\n'
        '            <fxchain numofeffects="1" enabled="1">\n'
        '%s'
        '            </fxchain>\n'
        '          </chain>\n' % amplifier(volume)
    )


def project(rack_selected):
    rack = ""
    if rack_selected is not None:
        rack = (
            '        <rack version="1" selected="%d">\n'
            '%s'
            '        </rack>\n' % (rack_selected, chain_one(50))
        )
    return (
        '<?xml version="1.0"?>\n'
        '<!DOCTYPE lmms-project>\n'
        '<lmms-project type="song" version="1.0" creator="Zene Studio" creatorversion="0.1.0">\n'
        '  <head timesig_denominator="4" bpm="%d" masterpitch="0" mastervol="100" timesig_numerator="4"/>\n'
        '  <song>\n'
        '    <trackcontainer visible="1" width="580" height="159" type="song" x="425" y="16" maximized="0" minimized="0">\n'
        '      <track type="1" muted="0" name="Beat/Bassline 0" solo="0">\n'
        '        <bbtrack>\n'
        '          <trackcontainer visible="1" width="580" height="249" type="bbtrackcontainer" x="426" y="182" maximized="0" minimized="0">\n'
        '            <track type="0" muted="0" name="Tone" solo="0">\n'
        '              <instrumenttrack pitch="0" vol="29" mixch="1" pan="0" basenote="57" usemasterpitch="1" pitchrange="1">\n'
        '                <instrument name="tripleoscillator">\n'
        '                  <tripleoscillator finel0="0" vol1="100" finel1="0" coarse0="0" finer0="0" vol2="0" pan0="0" finer1="0" coarse1="0" finel2="0" pan1="0" coarse2="0" finer2="0" pan2="0" wavetype0="0" wavetype1="0" wavetype2="0" phoffset0="0" phoffset1="0" phoffset2="0" modalgo1="2" modalgo2="2" modalgo3="2" stphdetun0="0" stphdetun1="0" stphdetun2="0" userwavefile0="" userwavefile1="" userwavefile2="" vol0="100"/>\n'
        '                </instrument>\n'
        '                <eldata fwet="0" ftype="0" fres="0.5" fcut="14000">\n'
        '                  <elvol lspd="0.1" ctlenvamt="0" lpdel="0" pdel="0" amt="1" hold="0" syncmode="0" userwavefile="" latt="0" sustain="1" lamt="0" lshp="0" lspd_denominator="4" lspd_numerator="4" x100="0" rel="0.328" dec="0.597" att="0"/>\n'
        '                  <elcut lspd="0.1" ctlenvamt="0" lpdel="0" pdel="0" amt="0" hold="0" syncmode="0" userwavefile="" latt="0" sustain="1" lamt="0" lshp="0" lspd_denominator="4" lspd_numerator="4" x100="0" rel="0.1" dec="0.499" att="0"/>\n'
        '                  <elres lspd="0.1" ctlenvamt="0" lpdel="0" pdel="0" amt="0" hold="0.499" syncmode="0" userwavefile="" latt="0" sustain="0.501" lamt="0" lshp="0" lspd_denominator="4" lspd_numerator="4" x100="0" rel="0.1" dec="0.499" att="0"/>\n'
        '                </eldata>\n'
        '                <chordcreator chordrange="1" chord="0" chord-enabled="0"/>\n'
        '                <arpeggiator arpdir="0" arpgate="100" arptime_denominator="4" syncmode="0" arp-enabled="0" arprange="1" arptime_numerator="4" arpmode="0" arp="0" arptime="100"/>\n'
        '                <midiport inputchannel="0" fixedinputvelocity="-1" outputcontroller="0" outputchannel="1" fixedoutputvelocity="-1" readable="0" fixedoutputnote="-1" outputprogram="1" writable="0" basevelocity="127" inputcontroller="0"/>\n'
        '                <fxchain numofeffects="0" enabled="0"/>\n'
        '              </instrumenttrack>\n'
        '              <pattern type="1" muted="0" steps="16" name="Tone" pos="0" len="192">\n'
        '%s'
        '              </pattern>\n'
        '            </track>\n'
        '          </trackcontainer>\n'
        '        </bbtrack>\n'
        '        <bbtco usestyle="1" muted="0" name="Beat/Baseline 0" pos="0" len="192" color="4282417407"/>\n'
        '      </track>\n'
        '    </trackcontainer>\n'
        '    <mixer visible="1" width="647" height="332" x="9" y="441" maximized="0" minimized="0">\n'
        '      <mixerchannel num="0" muted="0" volume="1" name="Master" soloed="0">\n'
        '        <fxchain numofeffects="0" enabled="0"/>\n'
        '      </mixerchannel>\n'
        '      <mixerchannel num="1" muted="0" volume="1" name="Rack" soloed="0">\n'
        '        <send channel="0" amount="1"/>\n'
        '        <fxchain numofeffects="1" enabled="1">\n'
        '%s'
        '        </fxchain>\n'
        '%s'
        '      </mixerchannel>\n'
        '    </mixer>\n'
        '    <controllers/>\n'
        '  </song>\n'
        '</lmms-project>\n' % (BPM, notes_xml(), amplifier(100), rack)
    )


def make(directory):
    os.makedirs(directory, exist_ok=True)
    written = []
    for name, selected in (("no-rack", None), ("rack-select-a", 0),
                           ("rack-select-b", 1), ("rack-parallel", -1)):
        path = os.path.join(directory, "%s.mmp" % name)
        with open(path, "w") as handle:
            handle.write(project(selected))
        written.append(path)
    for path in written:
        print("wrote", path)
    return written


def main(argv):
    if len(argv) != 2 or argv[0] != "make":
        print(__doc__)
        return 2
    make(os.path.abspath(argv[1]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
