/*
 * Note.cpp - implementation of class note
 *
 * Copyright (c) 2004-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
 * 
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */


#include <QDomElement>
#include <QStringList>

#include <cmath>
#include <limits>

#include "Note.h"
#include "DetuningHelper.h"
#include "ProjectIds.h"
#include "Song.h"

namespace lmms
{

namespace
{

qint64 readNoteInteger( const QDomElement& element, const char* name )
{
	bool ok = false;
	const qint64 value = element.attribute( QString::fromLatin1( name ) ).toLongLong( &ok );
	return ok ? value : 0;
}


qint64 legacyNoteKey( const QDomElement& element )
{
	const qint64 tone = readNoteInteger( element, "tone" );
	const qint64 octave = readNoteInteger( element, "oct" );
	constexpr qint64 scale = KeysPerOctave;
	constexpr qint64 maximum = std::numeric_limits<qint64>::max();
	constexpr qint64 minimum = std::numeric_limits<qint64>::min();
	if( octave > maximum / scale ) { return maximum; }
	if( octave < minimum / scale ) { return minimum; }
	const qint64 octaveKey = octave * scale;
	if( tone > 0 && octaveKey > maximum - tone ) { return maximum; }
	if( tone < 0 && octaveKey < minimum - tone ) { return minimum; }
	return octaveKey + tone;
}

} // namespace


namespace
{

//! The float an optional document attribute spells, or `fallback` when it is
//! absent, unparseable, or non-finite. QString::toFloat reports failure with 0
//! and admits "nan"/"inf" as VALID numbers, so neither can be told apart from a
//! real value by the conversion alone - the caller has to ask, which is what
//! this does once for both MIDI-depth attributes.
float loadMidiDepthValue( const QDomElement& element, const char* attribute, float fallback )
{
	bool ok = false;
	const float value = element.attribute( QLatin1String( attribute ) ).toFloat( &ok );
	return ( ok && std::isfinite( value ) ) ? value : fallback;
}

//! The saved pre-quantise position and length, when the element carries a pair
//! that can actually be read. Both are optional: an element with no qpos has no
//! timing, which is what -1 says. Both are required together, and both must
//! parse - QString::toInt() reports a failed conversion by yielding 0, which
//! would otherwise read as "the author had this note at tick 0, one tick long"
//! and let note.restore_timing move the note there.
void loadUnquantisedTiming( const QDomElement& element, tick_t& pos, tick_t& length )
{
	pos = -1;
	length = -1;

	bool posOk = false;
	bool lengthOk = false;
	const int storedPos = element.attribute( "qpos" ).toInt( &posOk );
	const int storedLength = element.attribute( "qlen", "1" ).toInt( &lengthOk );
	if( !posOk || !lengthOk || storedPos < 0 || storedLength < 1 ) { return; }

	pos = storedPos;
	length = storedLength;
}


//! Q050-016: one stream axis as the optional attribute text
//! "tick:value,tick:value,..." in the order the points are stored (sorted by
//! tick). Empty when the axis carries no points, so the attribute is not
//! written at all.
QString streamAttributeText( const std::vector<MpeExpressionPoint>& points )
{
	QStringList parts;
	parts.reserve( static_cast<int>( points.size() ) );
	for( const MpeExpressionPoint& point : points )
	{
		parts << QStringLiteral( "%1:%2" ).arg( point.tick ).arg( point.value );
	}
	return parts.join( QLatin1Char( ',' ) );
}


//! The points an optional stream attribute spells. A token the file cannot
//! spell - no colon, text that is not a number, a point past the count cap -
//! is skipped rather than read as a point at zero: QString::toInt() reports
//! failure by yielding 0, so a malformed token would otherwise become a real
//! point at tick 0. Clamping and ordering are the setter's own job, so this
//! stays a pure parser.
std::vector<MpeExpressionPoint> parseStreamAttribute( const QString& text )
{
	std::vector<MpeExpressionPoint> points;
	const QStringList tokens = text.split( QLatin1Char( ',' ), Qt::SkipEmptyParts );
	for( const QString& token : tokens )
	{
		if( points.size() >= static_cast<std::size_t>( MpeExpressionStream::MaxPoints ) ) { break; }
		const int colon = token.indexOf( QLatin1Char( ':' ) );
		if( colon <= 0 ) { continue; }
		bool tickOk = false;
		bool valueOk = false;
		const int tick = token.left( colon ).toInt( &tickOk );
		const int value = token.mid( colon + 1 ).toInt( &valueOk );
		if( !tickOk || !valueOk ) { continue; }
		points.push_back( { static_cast<tick_t>( tick ), value } );
	}
	return points;
}

} // namespace


Note::Note( const TimePos & length, const TimePos & pos,
		int key, volume_t volume, panning_t panning,
						std::shared_ptr<DetuningHelper> detuning ) :
	// The stable id, handed out once, here, at creation (SPEC-stable-ids.md
	// 2.1). It is the number in note-<n> and it never changes while the note
	// lives, so a client that was told note-7 keeps note-7 when a sibling note
	// moves (rearrangeAllNotes re-sorts the list) or is removed.
	// loadSettings() overrides it with the file's value on a project load, and
	// a note element with no id keeps this one - which is what makes legacy
	// assignment deterministic, because MidiClip::loadSettings recreates the
	// notes in document order.
	m_id( ProjectIds::allocate() ),
	m_selected( false ),
	m_oldKey(std::clamp(key, 0, NumKeys - 1)),
	m_oldPos( pos ),
	m_oldLength( length ),
	m_isPlaying( false ),
	m_key(std::clamp(key, 0, NumKeys - 1)),
	m_volume(std::clamp(volume, MinVolume, MaxVolume)),
	m_panning(std::clamp(panning, PanningLeft, PanningRight)),
	m_length( length ),
	m_pos(pos),
	m_detuning(std::move(detuning))
{
}




Note::Note( const Note & note ) :
	SerializingObject( note ),
	// A COPY is a NEW note, so it allocates a FRESH id and does NOT inherit the
	// source's (SPEC-stable-ids.md 2.1: an id names one object). Every copy path
	// goes through here - Note::clone() is what the clipboard, the piano roll's
	// duplicate and MidiClip's copy constructor use - and two notes wearing one
	// id would make the id an ambiguous address.
	m_id( ProjectIds::allocate() ),
	m_selected( note.m_selected ),
	m_oldKey( note.m_oldKey ),
	m_oldPos( note.m_oldPos ),
	m_oldLength( note.m_oldLength ),
	m_isPlaying( note.m_isPlaying ),
	m_key( note.m_key),
	m_volume( note.m_volume ),
	m_panning( note.m_panning ),
	m_length( note.m_length ),
	m_pos( note.m_pos ),
	m_detuning(note.m_detuning),
	m_type(note.m_type),
	m_slide(note.m_slide),
	m_probability(note.m_probability),
	m_velocityJitter(note.m_velocityJitter),
	m_mpeCaptured(note.m_mpeCaptured),
	m_mpePitchCents(note.m_mpePitchCents),
	m_mpePressure(note.m_mpePressure),
	m_mpeTimbre(note.m_mpeTimbre),
	m_mpeStream(note.m_mpeStream),
	m_muted(note.m_muted),
	m_releaseVelocity(note.m_releaseVelocity),
	m_unquantisedPos(note.m_unquantisedPos),
	m_unquantisedLength(note.m_unquantisedLength),
	m_conditions(note.m_conditions)
{
}

Note& Note::operator=(const Note& note)
{
	m_selected = note.m_selected;
	m_oldKey = note.m_oldKey;
	m_oldPos = note.m_oldPos;
	m_oldLength = note.m_oldLength;
	m_isPlaying = note.m_isPlaying;
	m_key = note.m_key;
	m_volume = note.m_volume;
	m_panning = note.m_panning;
	m_length = note.m_length;
	m_pos = note.m_pos;
	m_type = note.m_type;
	m_slide = note.m_slide;
	m_probability = note.m_probability;
	m_velocityJitter = note.m_velocityJitter;
	m_detuning = note.m_detuning;
	m_mpeCaptured = note.m_mpeCaptured;
	m_mpePitchCents = note.m_mpePitchCents;
	m_mpePressure = note.m_mpePressure;
	m_mpeTimbre = note.m_mpeTimbre;
	m_muted = note.m_muted;
	m_releaseVelocity = note.m_releaseVelocity;
	m_unquantisedPos = note.m_unquantisedPos;
	m_unquantisedLength = note.m_unquantisedLength;
	m_conditions = note.m_conditions;
	m_mpeStream = note.m_mpeStream;

	return *this;
}

void Note::setReleaseVelocity( int velocity )
{
	m_releaseVelocity = velocity < 0 ? -1 : std::min( velocity, static_cast<int>( MaxVolume ) );
}

int Note::midiReleaseVelocity( int midiBaseVelocity ) const
{
	return m_releaseVelocity < 0 ? 0 : std::min( MidiMaxVelocity, m_releaseVelocity * midiBaseVelocity / DefaultVolume );
}

void Note::rememberTiming()
{
	if( hasUnquantisedTiming() ) { return; }
	m_unquantisedPos = m_pos.getTicks();
	m_unquantisedLength = m_length.getTicks();
}

bool Note::restoreTiming()
{
	if( !hasUnquantisedTiming() ) { return false; }
	m_pos = TimePos( m_unquantisedPos );
	m_length = TimePos( m_unquantisedLength );
	forgetTiming();
	return true;
}


Note* Note::clone() const
{
	Note* newNote = new Note(*this);
	if (newNote->m_detuning != nullptr)
	{
		newNote->m_detuning = std::make_shared<DetuningHelper>(*newNote->m_detuning);
	}
	return newNote;
}




void Note::setId(int id)
{
	if (id < 0)
	{
		// A file claiming a negative id is not naming a note this build made:
		// keep the number the constructor handed out rather than putting a
		// "-1" on the wire. The load counts this as an assignment.
		return;
	}
	m_id = id;
	// Never let the counter hand this number to anything else, even if the file
	// that carried it had no (or a stale) next-id.
	ProjectIds::observe(id);
}



Note::~Note()
{
}




void Note::setLength( const TimePos & length )
{
	m_length = length;
}




void Note::setPos( const TimePos & pos )
{
	m_pos = pos;
}




void Note::setKey( const int key )
{
	const int k = std::clamp(key, 0, NumKeys - 1);
	m_key = k;
}




void Note::setVolume( volume_t volume )
{
	const volume_t v = std::clamp(volume, MinVolume, MaxVolume);
	m_volume = v;
}


void Note::setProbability( float probability )
{
	// A non-finite value is not an out-of-range one: every comparison against a
	// NaN is false, so std::clamp would hand it straight back unchanged and the
	// NaN would travel into the playback roll (NoteRandom). Fall back to the
	// documented neutral default instead - the note always plays.
	m_probability = std::isfinite( probability ) ? std::clamp( probability, 0.f, 1.f ) : 1.f;
}


void Note::setVelocityJitter( float jitter )
{
	// As above: an unparseable or non-finite value is not velocity jitter, and
	// the documented default is an untouched velocity.
	m_velocityJitter = std::isfinite( jitter ) ? std::clamp( jitter, 0.f, 1.f ) : 0.f;
}




void Note::setPanning( panning_t panning )
{
	const panning_t p = std::clamp(panning, PanningLeft, PanningRight);
	m_panning = p;
}




void Note::setMpeExpression( const MpeNoteExpression& expression )
{
	setMpePitchCents( expression.pitchCents );
	setMpePressure( expression.pressure );
	setMpeTimbre( expression.timbre );
}




void Note::setMpePitchCents( int cents )
{
	m_mpePitchCents = MpeNoteExpression::clampPitchCents( cents );
	m_mpeCaptured = true;
}




void Note::setMpePressure( int pressure )
{
	m_mpePressure = MpeNoteExpression::clamp7Bit( pressure );
	m_mpeCaptured = true;
}




void Note::setMpeTimbre( int timbre )
{
	m_mpeTimbre = MpeNoteExpression::clamp7Bit( timbre );
	m_mpeCaptured = true;
}




void Note::clearMpeExpression()
{
	m_mpeCaptured = false;
	m_mpePitchCents = 0;
	m_mpePressure = 0;
	m_mpeTimbre = 0;
}




MpeExpressionStream& Note::mpeStreamForEdit()
{
	// Copy-on-write: the caller gets the only reference, so in-place edits
	// through it can never touch a snapshot the audio thread is reading (a
	// playing handle holds its own shared reference to the old one). The
	// allocation happens here, on the UI/control thread - never on audio.
	if( m_mpeStream == nullptr )
	{
		m_mpeStream = std::make_shared<MpeExpressionStream>();
	}
	else if( m_mpeStream.use_count() > 1 )
	{
		m_mpeStream = std::make_shared<MpeExpressionStream>( *m_mpeStream );
	}
	return *m_mpeStream;
}




void Note::setMpeStream( const MpeExpressionStream& stream )
{
	if( stream.empty() )
	{
		clearMpeStream();
		return;
	}
	m_mpeStream = std::make_shared<MpeExpressionStream>( stream );
}




TimePos Note::quantized( const TimePos & m, const int qGrid )
{
	if( qGrid <= 0 )
	{
		return m;
	}
	float p = ( (float) m / qGrid );
	if( p - floorf( p ) < 0.5f )
	{
		return static_cast<int>( p ) * qGrid;
	}
	return static_cast<int>( p + 1 ) * qGrid;
}




void Note::quantizeLength( const int qGrid )
{
	if( qGrid <= 0 )
	{
		return;
	}
	setLength( quantized( length(), qGrid ) );
	if( length() == 0 )
	{
		setLength( qGrid );
	}
}




void Note::quantizePos( const int qGrid )
{
	if( qGrid <= 0 )
	{
		return;
	}
	setPos( quantized( pos(), qGrid ) );
}




void Note::saveSettings( QDomDocument & doc, QDomElement & parent )
{
	parent.setAttribute( "key", m_key );
	parent.setAttribute( "vol", m_volume );
	parent.setAttribute( "pan", m_panning );
	parent.setAttribute( "len", m_length );
	parent.setAttribute( "pos", m_pos );
	parent.setAttribute("type", static_cast<int>(m_type));

	// The note's stable id (SPEC-stable-ids.md slice 2, the slice-1 pattern of
	// Track::saveTrack). An ATTRIBUTE on the note's own element, never a child
	// element: Note::loadSettings treats ANY child of this element as detuning
	// info (_this.hasChildNodes() -> createDetuning()), so an id child would
	// make every note grow an automation clip on load, in every project.
	//
	// NOT optional, unlike every other attribute below: it is written
	// unconditionally, "0" included, because a note's id is what makes the note
	// addressable at all - the house rule "an optional attribute is written
	// ONLY when it is not the default" exists so projects that do not use a
	// feature stay byte-identical, and an identity is not a feature that can be
	// switched off. A build without this feature ignores the attribute; this
	// build reads it back in loadSettings().
	parent.setAttribute( "id", m_id );

	// Slide (portamento) flag - only written when set, so notes (and whole
	// projects) saved before slide notes existed serialize byte-identically
	// (SPEC-slide-notes D-1: optional attribute, no DataFile version bump).
	if( m_slide )
	{
		parent.setAttribute( "slide", "1" );
	}

	// MIDI depth: note probability and velocity jitter. Written only when set,
	// exactly like "slide" above, so a note - and a whole project - that does
	// not use them serializes byte-identically to before this existed.
	if( m_probability != 1.f )
	{
		parent.setAttribute( "prob", QString::number( m_probability, 'g', 9 ) );
	}
	if( m_velocityJitter != 0.f )
	{
		parent.setAttribute( "veljit", QString::number( m_velocityJitter, 'g', 9 ) );
	}
	// 0.5.0 L07, each optional in the same way.
	if( m_muted ) { parent.setAttribute( "mute", "1" ); }
	if( m_releaseVelocity >= 0 ) { parent.setAttribute( "relvel", m_releaseVelocity ); }
	if( hasUnquantisedTiming() )
	{
		parent.setAttribute( "qpos", static_cast<int>( m_unquantisedPos ) );
		parent.setAttribute( "qlen", static_cast<int>( m_unquantisedLength ) );
	}
	m_conditions.save( parent );

	// MPE per-note expression (task #601) - likewise optional, so a note with
	// no expression serializes exactly as it did before the feature existed,
	// and an older build reads the project back with the expression absent
	// (it ignores attributes it does not know).
	if( m_mpeCaptured )
	{
		parent.setAttribute( "mpepitch", m_mpePitchCents );
		parent.setAttribute( "mpepressure", m_mpePressure );
		parent.setAttribute( "mpetimbre", m_mpeTimbre );
	}

	// Q050-016: the per-note expression event streams, three optional
	// attributes written only when their axis carries points. A note with no
	// stream - and a whole project without one - serializes exactly as before
	// this feature existed, and an older build ignores the attributes it does
	// not know. Attributes, not child elements: Note::loadSettings treats ANY
	// child of a note element as detuning, so a stream child would make every
	// expressive note grow a detuning curve on load.
	if( m_mpeStream != nullptr )
	{
		if( !m_mpeStream->pitch().empty() )
		{
			parent.setAttribute( "mpepitchpts", streamAttributeText( m_mpeStream->pitch() ) );
		}
		if( !m_mpeStream->pressure().empty() )
		{
			parent.setAttribute( "mpepressurepts", streamAttributeText( m_mpeStream->pressure() ) );
		}
		if( !m_mpeStream->timbre().empty() )
		{
			parent.setAttribute( "mpetimbrepts", streamAttributeText( m_mpeStream->timbre() ) );
		}
	}

	if( m_detuning && m_length )
	{
		m_detuning->saveSettings( doc, parent );
	}
}




void Note::loadSettings( const QDomElement & _this )
{
	const qint64 loadedKey = std::max( legacyNoteKey( _this ), readNoteInteger( _this, "key" ) );
	setKey( static_cast<int>( std::clamp( loadedKey, qint64{ 0 }, qint64{ NumKeys - 1 } ) ) );
	m_volume = _this.attribute( "vol" ).toInt();
	m_panning = _this.attribute( "pan" ).toInt();
	const qint64 position = std::clamp( readNoteInteger( _this, "pos" ), qint64{ 0 },
		static_cast<qint64>( MaxSongLength - 1 ) );
	const qint64 maximumLength = static_cast<qint64>( MaxSongLength ) - position;
	m_length = TimePos( static_cast<tick_t>( std::clamp( readNoteInteger( _this, "len" ), qint64{ 1 }, maximumLength ) ) );
	m_pos = TimePos( static_cast<tick_t>( position ) );
	// Default m_type value is 0, which corresponds to RegularNote
	static_assert(0 == static_cast<int>(Type::Regular));
	m_type = static_cast<Type>(_this.attribute("type", "0").toInt());
	// Absent attribute means a regular note (all projects predating slide notes)
	m_slide = _this.attribute( "slide" ).toInt();

	// The note's stable id (SPEC-stable-ids.md slice 2; the slice-1 pattern of
	// Track::loadTrack). A file that carries one keeps it; a file that does not
	// - every project saved before notes had ids - leaves the number the
	// constructor already handed out, which is deterministic because
	// MidiClip::loadSettings recreates the notes in document order. Either way
	// ProjectIds::loadAssignments() counts it, and project.open reports the
	// count as `ids_assigned`, so a legacy file's one-time upgrade is stated
	// rather than silent.
	//
	// A note read out of a COPY payload - the piano roll's clipboard, a track
	// being cloned - keeps the id the constructor handed out instead (rule R4):
	// the payload's id belongs to the note that was copied, which is still
	// alive, and two live notes answering to one `note-<n>` would make the id
	// an ambiguous address. What tells the two apart is the wrapper the element
	// sits in, not the element itself - a project's notes sit under their clip
	// element, a clipboard's under <note-list> - which is what
	// ProjectIds::isDocumentElement reads. Nothing is counted for a copy: it
	// was never an assignment.
	if( ProjectIds::isDocumentElement( _this ) )
	{
		if( _this.hasAttribute( "id" ) )
		{
			bool ok = false;
			const int stored = _this.attribute( "id" ).toInt( &ok );
			if( ok && stored >= 0 ) { setId( stored ); }
			else { ProjectIds::noteLoadAssignment( id() ); }
		}
		else
		{
			ProjectIds::noteLoadAssignment( id() );
		}
	}

	// Absent attributes mean the neutral MIDI-depth values: the note always
	// plays and its velocity is untouched, which is what every project saved
	// before this existed expects. An attribute the file CANNOT express - text
	// that is not a number at all, or a non-finite spelling such as "nan" - is
	// treated as absent rather than as zero: toFloat() reports failure and
	// yields 0, and std::clamp leaves a NaN a NaN, either of which would make
	// the note permanently silent or carry a NaN into the playback roll.
	// Out-of-range but FINITE text still clamps, as before.
	setProbability( loadMidiDepthValue( _this, "prob", 1.f ) );
	setVelocityJitter( loadMidiDepthValue( _this, "veljit", 0.f ) );
	m_muted = _this.attribute( "mute" ).toInt() != 0;
	setReleaseVelocity( _this.attribute( "relvel", "-1" ).toInt() );
	// The pre-quantise timing note.restore_timing puts back is OPTIONAL, and both
	// halves of it are required. A qpos the file cannot spell - or a qlen it
	// cannot spell - is not a timing to restore: QString::toInt() reports
	// failure by yielding 0, so <note qpos="bad"> used to load as "this note was
	// moved to tick 0" and note.restore_timing faithfully moved it there.
	loadUnquantisedTiming( _this, m_unquantisedPos, m_unquantisedLength );
	m_conditions.load( _this );

	// MPE per-note expression (task #601): an old project has none of these, and
	// then the note carries no expression at all. Presence of any one attribute
	// means expression was captured (even an all-zero capture, which is exactly
	// why the presence of the attributes, not their values, is the flag).
	m_mpeCaptured = _this.hasAttribute( "mpepitch" )
		|| _this.hasAttribute( "mpepressure" )
		|| _this.hasAttribute( "mpetimbre" );
	if( m_mpeCaptured )
	{
		m_mpePitchCents = MpeNoteExpression::clampPitchCents( _this.attribute( "mpepitch" ).toInt() );
		m_mpePressure = MpeNoteExpression::clamp7Bit( _this.attribute( "mpepressure" ).toInt() );
		m_mpeTimbre = MpeNoteExpression::clamp7Bit( _this.attribute( "mpetimbre" ).toInt() );
	}

	// Q050-016: the per-note expression streams, optional like the scalar
	// attributes above. A file with none leaves the note with no stream (the
	// default), and each axis's points are clamped and re-sorted by the setter
	// so a hostile file cannot carry an out-of-range value or unsorted points
	// into playback.
	m_mpeStream.reset();
	if( _this.hasAttribute( "mpepitchpts" ) || _this.hasAttribute( "mpepressurepts" )
		|| _this.hasAttribute( "mpetimbrepts" ) )
	{
		MpeExpressionStream& stream = mpeStreamForEdit();
		if( _this.hasAttribute( "mpepitchpts" ) )
		{
			stream.setPitch( parseStreamAttribute( _this.attribute( "mpepitchpts" ) ) );
		}
		if( _this.hasAttribute( "mpepressurepts" ) )
		{
			stream.setPressure( parseStreamAttribute( _this.attribute( "mpepressurepts" ) ) );
		}
		if( _this.hasAttribute( "mpetimbrepts" ) )
		{
			stream.setTimbre( parseStreamAttribute( _this.attribute( "mpetimbrepts" ) ) );
		}
		// An attribute that parsed to no points (empty text, or all tokens
		// malformed) leaves the stream empty; drop the whole object so
		// hasMpeStream() is false and a re-save writes no attribute.
		if( m_mpeStream->empty() ) { m_mpeStream.reset(); }
	}

	if( _this.hasChildNodes() )
	{
		createDetuning();
		m_detuning->loadSettings( _this );
	}
}


AutomationClip* Note::parameterCurve(ParameterType paramType)
{
	switch (paramType)
	{
	// TODO: In the future, more per-note parameters which can be automated are planned. Perhaps velocity, panning, and/or some other plugin-specific parameter.
	case ParameterType::Detuning:
		if (detuning() != nullptr)
		{
			return detuning()->automationClip();
		}
	default:
		return nullptr;
	}
}


void Note::createDetuning()
{
	if( m_detuning == nullptr )
	{
		m_detuning = std::make_shared<DetuningHelper>();
		(void) m_detuning->automationClip();
		m_detuning->setRange( -MaxDetuning, MaxDetuning, 0.5f );
		m_detuning->automationClip()->setProgressionType( AutomationClip::ProgressionType::Linear );
	}
}




bool Note::hasDetuningInfo() const
{
	return m_detuning && m_detuning->hasAutomation();
}



bool Note::withinRange(int tickStart, int tickEnd) const
{
	return pos().getTicks() >= tickStart && pos().getTicks() <= tickEnd
		&& length().getTicks() != 0;
}




/*! \brief Get the start/end/bottom/top positions of notes in a vector
 *
 *  Returns no value if there are no notes
 */
std::optional<NoteBounds> boundsForNotes(const NoteVector& notes)
{
	if (notes.empty()) { return std::nullopt; }

	TimePos start = notes.front()->pos();
	TimePos end = start;
	int lower = notes.front()->key();
	int upper = lower;

	for (const Note* note: notes)
	{
		// TODO should we assume that NoteVector is always sorted correctly,
		// so first() always has the lowest time position?
		start = std::min(start, note->pos());
		end = std::max(end, note->endPos());
		lower = std::min(lower, note->key());
		upper = std::max(upper, note->key());
	}

	return NoteBounds{start, end, lower, upper};
}


} // namespace lmms
