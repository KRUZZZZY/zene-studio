#!/usr/bin/env python3
"""Build a project whose song track list carries one vst3instrument track.

The subject is `data/projects/shorties/Crunk(Demo).mmp`: a real project with a
song-level instrument track (the "Bass" track, the only instrumenttrack with
mixch="2"). Its <instrument> block is replaced by the VST3 instrument element
the product's own project writer produces (see Vst3Instrument::saveSettings and
InstrumentTrack::saveState), with the <key> the loader re-instantiates from.

Usage: craft-vst3-project.py <source.mmp> <bundle.vst3> <out.mmp>
"""
import re
import sys

src_path, bundle, out_path = sys.argv[1], sys.argv[2], sys.argv[3]
text = open(src_path, encoding="utf-8").read()

vst3_block = (
    '          <instrument name="vst3instrument">\n'
    '            <vst3instrument>\n'
    '              <key>\n'
    f'                <attribute name="file" value="{bundle}"/>\n'
    '                <attribute name="class" value="Zene VST3 Test Instrument"/>\n'
    '              </key>\n'
    '            </vst3instrument>\n'
    '          </instrument>'
)

# the Bass track's instrument block: the one instrumenttrack with mixch="2"
start = text.index('mixch="2"')
inst_start = text.index('          <instrument name=', start)
inst_end = text.index('</instrument>', inst_start) + len('</instrument>')
out = text[:inst_start] + vst3_block + text[inst_end:]

open(out_path, "w", encoding="utf-8").write(out)
print(f"wrote {out_path}: replaced the <instrument> block at offset {inst_start}")
print("instrument blocks now:", re.findall(r'<instrument name="([^"]+)"', out))
