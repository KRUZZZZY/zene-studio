#!/usr/bin/env python3
"""Merge train 3C, merge 3 (post-alpha/mpe): the two product-code conflicts in
include/Note.h and src/core/Note.cpp, resolved as verified unions.

Both sides add DISJOINT things at the same anchor:
  Note.h        ours  = MIDI-depth probability()/velocityJitter() public API +
                        the two private members (post-alpha/midi-depth)
                theirs = MPE per-note expression API + the four private members
                        (task #601)
  Note.cpp      the copy constructor's init list, saveSettings()'s optional
                attributes, and loadSettings()'s reads - same story, three
                non-overlapping insertions.

Nothing is dropped and nothing is authored: every line below is one of the two
sides' own text, in each side's own words.
"""
W = "/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-pa-foreign"


def sub_once(path, old, new, label):
    p = f"{W}/{path}"
    s = open(p, encoding="utf-8").read()
    n = s.count(old)
    assert n == 1, f"{label}: expected 1 occurrence, found {n}"
    open(p, "w", encoding="utf-8").write(s.replace(old, new))
    print(f"  resolved {label}")


# ---------------- include/Note.h ----------------
NOTE_H_PUBLIC_OLD = """<<<<<<< HEAD
\t/*! The chance, in [0, 1], that this note is played at all in a take.
\t *  1 - the default, and what every project saved before note probability
\t *  existed loads as - means \"always\". Serialized as the optional \"prob\"
\t *  attribute, written only when it is not 1. */
\tfloat probability() const { return m_probability; }
\tvoid setProbability( float probability );

\t/*! Multiplicative velocity jitter, in [0, 1]. 0 - the default - leaves
\t *  the note's velocity exactly alone; a value j multiplies it by a factor
\t *  in [1-j, 1+j] drawn from the seeded roll. Serialized as the optional
\t *  \"veljit\" attribute, written only when it is not 0. */
\tfloat velocityJitter() const { return m_velocityJitter; }
\tvoid setVelocityJitter( float jitter );
=======
\t/*! MPE per-note expression (task #601): the pitch bend / channel pressure /
\t *  CC74 an MPE controller sent on *this note's own* MIDI channel.
\t *
\t *  Not the slide note above (that is a portamento the piano roll draws and
\t *  the note glides along); not Note::detuning() (a per-note pitch
\t *  automation curve); and not the channel-wide bend in
\t *  MidiEvent::pitchBend(), which bends every note of the channel. Storing
\t *  it here is what makes it per-note.
\t *
\t *  Serialized as the optional \"mpepitch\"/\"mpepressure\"/\"mpetimbre\"
\t *  attributes, written only when expression was captured: notes (and whole
\t *  projects) saved with no expression serialize byte-identically to before,
\t *  and a build without this feature simply ignores the attributes. */
\tbool hasMpeExpression() const { return m_mpeCaptured; }
\tMpeNoteExpression mpeExpression() const { return { m_mpePitchCents, m_mpePressure, m_mpeTimbre }; }
\t//! Bend offset in 1/100 semitone, clamped to +-MpeNoteExpression::MaxPitchCents
\tint mpePitchCents() const { return m_mpePitchCents; }
\t//! Channel pressure 0..127
\tint mpePressure() const { return m_mpePressure; }
\t//! CC74 (timbre) 0..127
\tint mpeTimbre() const { return m_mpeTimbre; }

\t//! The piano roll's (future) editing entry point: sets all three axes and
\t//! marks the note as carrying expression. Individual setters only touch
\t//! their own axis, and any of them marks the note as carrying expression.
\tvoid setMpeExpression( const MpeNoteExpression& expression );
\tvoid setMpePitchCents( int cents );
\tvoid setMpePressure( int pressure );
\tvoid setMpeTimbre( int timbre );
\t//! Drops the expression entirely (and the serialized attributes with it).
\tvoid clearMpeExpression();
>>>>>>> post-alpha/mpe
"""

NOTE_H_PUBLIC_NEW = """\t/*! The chance, in [0, 1], that this note is played at all in a take.
\t *  1 - the default, and what every project saved before note probability
\t *  existed loads as - means \"always\". Serialized as the optional \"prob\"
\t *  attribute, written only when it is not 1. */
\tfloat probability() const { return m_probability; }
\tvoid setProbability( float probability );

\t/*! Multiplicative velocity jitter, in [0, 1]. 0 - the default - leaves
\t *  the note's velocity exactly alone; a value j multiplies it by a factor
\t *  in [1-j, 1+j] drawn from the seeded roll. Serialized as the optional
\t *  \"veljit\" attribute, written only when it is not 0. */
\tfloat velocityJitter() const { return m_velocityJitter; }
\tvoid setVelocityJitter( float jitter );

\t/*! MPE per-note expression (task #601): the pitch bend / channel pressure /
\t *  CC74 an MPE controller sent on *this note's own* MIDI channel.
\t *
\t *  Not the slide note above (that is a portamento the piano roll draws and
\t *  the note glides along); not Note::detuning() (a per-note pitch
\t *  automation curve); and not the channel-wide bend in
\t *  MidiEvent::pitchBend(), which bends every note of the channel. Storing
\t *  it here is what makes it per-note.
\t *
\t *  Serialized as the optional \"mpepitch\"/\"mpepressure\"/\"mpetimbre\"
\t *  attributes, written only when expression was captured: notes (and whole
\t *  projects) saved with no expression serialize byte-identically to before,
\t *  and a build without this feature simply ignores the attributes. */
\tbool hasMpeExpression() const { return m_mpeCaptured; }
\tMpeNoteExpression mpeExpression() const { return { m_mpePitchCents, m_mpePressure, m_mpeTimbre }; }
\t//! Bend offset in 1/100 semitone, clamped to +-MpeNoteExpression::MaxPitchCents
\tint mpePitchCents() const { return m_mpePitchCents; }
\t//! Channel pressure 0..127
\tint mpePressure() const { return m_mpePressure; }
\t//! CC74 (timbre) 0..127
\tint mpeTimbre() const { return m_mpeTimbre; }

\t//! The piano roll's (future) editing entry point: sets all three axes and
\t//! marks the note as carrying expression. Individual setters only touch
\t//! their own axis, and any of them marks the note as carrying expression.
\tvoid setMpeExpression( const MpeNoteExpression& expression );
\tvoid setMpePitchCents( int cents );
\tvoid setMpePressure( int pressure );
\tvoid setMpeTimbre( int timbre );
\t//! Drops the expression entirely (and the serialized attributes with it).
\tvoid clearMpeExpression();
"""

NOTE_H_PRIVATE_OLD = """<<<<<<< HEAD
\t// MIDI depth. Both default to the behaviour of every project saved before
\t// these existed: always play, velocity untouched.
\tfloat m_probability = 1.f;
\tfloat m_velocityJitter = 0.f;
=======
\t// MPE per-note expression (task #601). m_mpeCaptured distinguishes \"no
\t// expression\" (nothing is written to the project file) from a captured
\t// expression that happens to be all zeros.
\tbool m_mpeCaptured = false;
\tint m_mpePitchCents = 0;
\tint m_mpePressure = 0;
\tint m_mpeTimbre = 0;
>>>>>>> post-alpha/mpe
"""

NOTE_H_PRIVATE_NEW = """\t// MIDI depth. Both default to the behaviour of every project saved before
\t// these existed: always play, velocity untouched.
\tfloat m_probability = 1.f;
\tfloat m_velocityJitter = 0.f;

\t// MPE per-note expression (task #601). m_mpeCaptured distinguishes \"no
\t// expression\" (nothing is written to the project file) from a captured
\t// expression that happens to be all zeros.
\tbool m_mpeCaptured = false;
\tint m_mpePitchCents = 0;
\tint m_mpePressure = 0;
\tint m_mpeTimbre = 0;
"""

# ---------------- src/core/Note.cpp ----------------
CPP_CTOR_OLD = """<<<<<<< HEAD
\tm_probability(note.m_probability),
\tm_velocityJitter(note.m_velocityJitter)
=======
\tm_mpeCaptured(note.m_mpeCaptured),
\tm_mpePitchCents(note.m_mpePitchCents),
\tm_mpePressure(note.m_mpePressure),
\tm_mpeTimbre(note.m_mpeTimbre)
>>>>>>> post-alpha/mpe
"""

CPP_CTOR_NEW = """\tm_probability(note.m_probability),
\tm_velocityJitter(note.m_velocityJitter),
\tm_mpeCaptured(note.m_mpeCaptured),
\tm_mpePitchCents(note.m_mpePitchCents),
\tm_mpePressure(note.m_mpePressure),
\tm_mpeTimbre(note.m_mpeTimbre)
"""

CPP_SAVE_OLD = """<<<<<<< HEAD
\t// MIDI depth: note probability and velocity jitter. Written only when set,
\t// exactly like \"slide\" above, so a note - and a whole project - that does
\t// not use them serializes byte-identically to before this existed.
\tif( m_probability != 1.f )
\t{
\t\tparent.setAttribute( \"prob\", QString::number( m_probability, 'g', 9 ) );
\t}
\tif( m_velocityJitter != 0.f )
\t{
\t\tparent.setAttribute( \"veljit\", QString::number( m_velocityJitter, 'g', 9 ) );
=======
\t// MPE per-note expression (task #601) - likewise optional, so a note with
\t// no expression serializes exactly as it did before the feature existed,
\t// and an older build reads the project back with the expression absent
\t// (it ignores attributes it does not know).
\tif( m_mpeCaptured )
\t{
\t\tparent.setAttribute( \"mpepitch\", m_mpePitchCents );
\t\tparent.setAttribute( \"mpepressure\", m_mpePressure );
\t\tparent.setAttribute( \"mpetimbre\", m_mpeTimbre );
>>>>>>> post-alpha/mpe
"""

CPP_SAVE_NEW = """\t// MIDI depth: note probability and velocity jitter. Written only when set,
\t// exactly like \"slide\" above, so a note - and a whole project - that does
\t// not use them serializes byte-identically to before this existed.
\tif( m_probability != 1.f )
\t{
\t\tparent.setAttribute( \"prob\", QString::number( m_probability, 'g', 9 ) );
\t}
\tif( m_velocityJitter != 0.f )
\t{
\t\tparent.setAttribute( \"veljit\", QString::number( m_velocityJitter, 'g', 9 ) );
\t}

\t// MPE per-note expression (task #601) - likewise optional, so a note with
\t// no expression serializes exactly as it did before the feature existed,
\t// and an older build reads the project back with the expression absent
\t// (it ignores attributes it does not know).
\tif( m_mpeCaptured )
\t{
\t\tparent.setAttribute( \"mpepitch\", m_mpePitchCents );
\t\tparent.setAttribute( \"mpepressure\", m_mpePressure );
\t\tparent.setAttribute( \"mpetimbre\", m_mpeTimbre );
"""

CPP_LOAD_OLD = """<<<<<<< HEAD
\t// Absent attributes mean the neutral MIDI-depth values: the note always
\t// plays and its velocity is untouched, which is what every project saved
\t// before this existed expects.
\tm_probability = std::clamp( _this.attribute( \"prob\", \"1\" ).toFloat(), 0.f, 1.f );
\tm_velocityJitter = std::clamp( _this.attribute( \"veljit\", \"0\" ).toFloat(), 0.f, 1.f );
=======
\t// MPE per-note expression (task #601): an old project has none of these, and
\t// then the note carries no expression at all. Presence of any one attribute
\t// means expression was captured (even an all-zero capture, which is exactly
\t// why the presence of the attributes, not their values, is the flag).
\tm_mpeCaptured = _this.hasAttribute( \"mpepitch\" )
\t\t|| _this.hasAttribute( \"mpepressure\" )
\t\t|| _this.hasAttribute( \"mpetimbre\" );
\tif( m_mpeCaptured )
\t{
\t\tm_mpePitchCents = MpeNoteExpression::clampPitchCents( _this.attribute( \"mpepitch\" ).toInt() );
\t\tm_mpePressure = MpeNoteExpression::clamp7Bit( _this.attribute( \"mpepressure\" ).toInt() );
\t\tm_mpeTimbre = MpeNoteExpression::clamp7Bit( _this.attribute( \"mpetimbre\" ).toInt() );
\t}
>>>>>>> post-alpha/mpe
"""

CPP_LOAD_NEW = """\t// Absent attributes mean the neutral MIDI-depth values: the note always
\t// plays and its velocity is untouched, which is what every project saved
\t// before this existed expects.
\tm_probability = std::clamp( _this.attribute( \"prob\", \"1\" ).toFloat(), 0.f, 1.f );
\tm_velocityJitter = std::clamp( _this.attribute( \"veljit\", \"0\" ).toFloat(), 0.f, 1.f );

\t// MPE per-note expression (task #601): an old project has none of these, and
\t// then the note carries no expression at all. Presence of any one attribute
\t// means expression was captured (even an all-zero capture, which is exactly
\t// why the presence of the attributes, not their values, is the flag).
\tm_mpeCaptured = _this.hasAttribute( \"mpepitch\" )
\t\t|| _this.hasAttribute( \"mpepressure\" )
\t\t|| _this.hasAttribute( \"mpetimbre\" );
\tif( m_mpeCaptured )
\t{
\t\tm_mpePitchCents = MpeNoteExpression::clampPitchCents( _this.attribute( \"mpepitch\" ).toInt() );
\t\tm_mpePressure = MpeNoteExpression::clamp7Bit( _this.attribute( \"mpepressure\" ).toInt() );
\t\tm_mpeTimbre = MpeNoteExpression::clamp7Bit( _this.attribute( \"mpetimbre\" ).toInt() );
\t}
"""

sub_once("include/Note.h", NOTE_H_PUBLIC_OLD, NOTE_H_PUBLIC_NEW, "Note.h public API")
sub_once("include/Note.h", NOTE_H_PRIVATE_OLD, NOTE_H_PRIVATE_NEW, "Note.h private members")
sub_once("src/core/Note.cpp", CPP_CTOR_OLD, CPP_CTOR_NEW, "Note.cpp copy ctor init list")
sub_once("src/core/Note.cpp", CPP_SAVE_OLD, CPP_SAVE_NEW, "Note.cpp saveSettings")
sub_once("src/core/Note.cpp", CPP_LOAD_OLD, CPP_LOAD_NEW, "Note.cpp loadSettings")

# ---------------- assertions ----------------
h = open(f"{W}/include/Note.h", encoding="utf-8").read()
c = open(f"{W}/src/core/Note.cpp", encoding="utf-8").read()
checks = [
    ('#include "MpeExpression.h"' in h, "Note.h keeps the MpeExpression.h include"),
    (h.count("float probability() const") == 1 and h.count("float velocityJitter() const") == 1,
     "Note.h: both MIDI-depth accessors, once each"),
    (h.count("bool hasMpeExpression() const") == 1 and h.count("void clearMpeExpression();") == 1,
     "Note.h: MPE readback + clear, once each"),
    (h.count("float m_probability = 1.f;") == 1 and h.count("float m_velocityJitter = 0.f;") == 1,
     "Note.h: both MIDI-depth members"),
    (h.count("bool m_mpeCaptured = false;") == 1 and h.count("int m_mpeTimbre = 0;") == 1,
     "Note.h: all four MPE members"),
    (c.count("m_probability(note.m_probability)") == 1 and c.count("m_mpeTimbre(note.m_mpeTimbre)") == 1,
     "Note.cpp: copy ctor carries both sides' fields"),
    (c.count("m_mpeCaptured = note.m_mpeCaptured;") == 1, "Note.cpp: operator= carries the MPE fields (auto-merged)"),
    (c.count('parent.setAttribute( "prob"') == 1 and c.count('parent.setAttribute( "mpetimbre"') == 1,
     "Note.cpp: saveSettings writes both sides' optional attributes"),
    (c.count("m_probability = std::clamp(") == 1 and c.count("m_mpePitchCents = MpeNoteExpression::clampPitchCents(") == 1,
     "Note.cpp: loadSettings reads both sides"),
]
bad = [m for ok, m in checks if not ok]
for ok, m in checks:
    print(("PASS  " if ok else "FAIL  ") + m)
for p in ("include/Note.h", "src/core/Note.cpp"):
    s = open(f"{W}/{p}", encoding="utf-8").read()
    assert "<<<<<<<" not in s and ">>>>>>>" not in s and "\n=======\n" not in s, f"{p}: marker"
    print(f"PASS  {p}: no conflict marker")
# brace balance of the two files must be unchanged from a marker-free expectation:
assert c.count("{") == c.count("}"), f"Note.cpp braces unbalanced: {c.count('{')} vs {c.count('}')}"
assert h.count("{") == h.count("}"), f"Note.h braces unbalanced"
print("PASS  brace counts balanced in both files")
assert not bad, f"FAILED: {bad}"
print("ALL ASSERTIONS PASSED")
