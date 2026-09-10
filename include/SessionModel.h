/*
 * SessionModel.h - Session View data layer (clip slots, scenes)
 *
 * Copyright (c) 2026 Zene Studio contributors
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

#ifndef LMMS_SESSION_MODEL_H
#define LMMS_SESSION_MODEL_H

#include <QString>
#include <vector>

#include "lmms_export.h"

class QDomDocument;
class QDomElement;

namespace lmms
{

/*! Session View data layer (SPEC-zene-studio A1, §4.1).
 *
 *  The grid is track columns x scene rows. Every cell is a ClipSlot holding a
 *  reference to a MIDI pattern or an audio clip plus its launch settings and
 *  Follow Action chain; every row is a Scene with an optional tempo and time
 *  signature override.
 *
 *  This is the persisted model only. Launching, Follow Action evaluation and
 *  the separate session clock domain (SPEC A2/A3) belong to the engine and are
 *  deliberately not implemented here. Session clips are a NEW type (SPEC A1),
 *  not PatternClip: the slot stores a reference, never timeline state, and a
 *  track's session and arrangement content stay mutually exclusive.
 *
 *  Serialisation is the versioned <session> block (see SessionModel.cpp for the
 *  schema and the version policy). Everything is feature-gated behind
 *  WANT_SESSION_VIEW (LMMS_HAVE_SESSION_VIEW).
 */
class LMMS_EXPORT SessionModel;

//! Launch mode of a clip slot (SPEC §4.1: Trigger, Gate, Toggle, Repeat).
enum class LaunchMode : int
{
	Trigger = 0, //!< starts on trigger, plays until stopped or its loop ends
	Gate,        //!< plays while the trigger is held
	Toggle,      //!< first trigger starts, second trigger stops
	Repeat       //!< retriggers in a loop while held
};

/*! Launch quantisation, expressed in bars. The numeric value is the number of
 *  bars (0 = none), so `Global` (-1) defers to the session default and the
 *  serialised value stays self-describing. */
enum class LaunchQuantisation : int
{
	Global = -1, //!< per-clip only: use SessionModel's global quantisation
	None = 0,
	Bar = 1,
	TwoBars = 2,
	FourBars = 4
};

//! One entry of a clip's Follow Action chain (SPEC §4.1).
struct LMMS_EXPORT FollowAction
{
	enum class Type : int
	{
		NoAction = 0,
		Stop,
		PlayAgain,
		Previous,
		Next,
		First,
		Last,
		Any,
		Other,
		Jump
	};

	Type type = Type::NoAction;
	//! Chance A/B weight, 0..1. Entries of a chain are picked with these
	//! weights; a single entry with chance 1 is the plain "always" action.
	double chance = 1.0;
	//! Linked timing (the action fires after the clip length) vs Unlinked.
	bool linked = true;
	//! Unlinked follow-action time, in bars (ignored when linked).
	double timeBars = 1.0;
	//! Target scene row for Type::Jump.
	int jumpTo = 0;

	bool operator==( const FollowAction& other ) const
	{
		return type == other.type && chance == other.chance
			&& linked == other.linked && timeBars == other.timeBars
			&& jumpTo == other.jumpTo;
	}
	bool operator!=( const FollowAction& other ) const { return !( *this == other ); }
};

/*! One grid cell: a reference to a MIDI pattern or an audio clip plus its
 *  launch settings. An empty slot is not serialised at all. */
class LMMS_EXPORT ClipSlot
{
public:
	enum class Type : int
	{
		Empty = 0,
		Midi,
		Audio
	};

	Type type() const { return m_type; }
	bool isEmpty() const { return m_type == Type::Empty; }

	//! MIDI clips reference a pattern by its PatternStore id.
	int patternId() const { return m_patternId; }
	//! Audio clips reference a sample file by (possibly relative) path.
	const QString& audioSource() const { return m_audioSource; }

	//! Setting one reference kind clears the other: a slot is either a MIDI
	//! or an audio clip, never both (SPEC A1 mutual exclusivity).
	void setPatternReference( int patternId );
	void setAudioReference( const QString& source );
	void clear();

	const QString& name() const { return m_name; }
	void setName( const QString& name ) { m_name = name; }

	LaunchMode launchMode() const { return m_launchMode; }
	void setLaunchMode( LaunchMode mode ) { m_launchMode = mode; }

	LaunchQuantisation launchQuantisation() const { return m_launchQuantisation; }
	void setLaunchQuantisation( LaunchQuantisation quantisation ) { m_launchQuantisation = quantisation; }

	bool legato() const { return m_legato; }
	void setLegato( bool legato ) { m_legato = legato; }

	//! Loop region in ticks, relative to the clip (SPEC §4.1 per-clip loop).
	int loopStart() const { return m_loopStart; }
	void setLoopStart( int ticks ) { m_loopStart = ticks; }
	int loopLength() const { return m_loopLength; }
	void setLoopLength( int ticks ) { m_loopLength = ticks; }

	float gainDb() const { return m_gainDb; }
	void setGainDb( float gainDb ) { m_gainDb = gainDb; }
	int transpose() const { return m_transpose; }
	void setTranspose( int semitones ) { m_transpose = semitones; }
	int detune() const { return m_detune; }
	void setDetune( int cents ) { m_detune = cents; }
	bool ramMode() const { return m_ramMode; }
	void setRamMode( bool ramMode ) { m_ramMode = ramMode; }

	const std::vector<FollowAction>& followActions() const { return m_followActions; }
	void setFollowActions( std::vector<FollowAction> chain ) { m_followActions = std::move( chain ); }
	void addFollowAction( const FollowAction& action ) { m_followActions.push_back( action ); }

	//! Writes the slot's attributes onto a <clip> element.
	void saveState( QDomDocument& doc, QDomElement& element ) const;
	void restoreState( const QDomElement& element );

private:
	Type m_type = Type::Empty;
	int m_patternId = -1;
	QString m_audioSource;
	QString m_name;
	LaunchMode m_launchMode = LaunchMode::Trigger;
	LaunchQuantisation m_launchQuantisation = LaunchQuantisation::Global;
	bool m_legato = false;
	int m_loopStart = 0;
	int m_loopLength = 0;
	float m_gainDb = 0.0f;
	int m_transpose = 0;
	int m_detune = 0;
	bool m_ramMode = false;
	std::vector<FollowAction> m_followActions;
};

//! One grid row: an optional tempo / time-signature override (SPEC §4.1).
class LMMS_EXPORT Scene
{
public:
	const QString& name() const { return m_name; }
	void setName( const QString& name ) { m_name = name; }

	bool tempoEnabled() const { return m_tempoEnabled; }
	void setTempoEnabled( bool enabled ) { m_tempoEnabled = enabled; }
	double tempo() const { return m_tempo; }
	void setTempo( double tempo ) { m_tempo = tempo; }

	bool timeSigEnabled() const { return m_timeSigEnabled; }
	void setTimeSigEnabled( bool enabled ) { m_timeSigEnabled = enabled; }
	int timeSigNumerator() const { return m_timeSigNumerator; }
	void setTimeSigNumerator( int numerator ) { m_timeSigNumerator = numerator; }
	int timeSigDenominator() const { return m_timeSigDenominator; }
	void setTimeSigDenominator( int denominator ) { m_timeSigDenominator = denominator; }

	//! True when the scene carries anything beyond its (empty) defaults.
	bool isModified() const
	{
		return !m_name.isEmpty() || m_tempoEnabled || m_timeSigEnabled;
	}

	void saveState( QDomDocument& doc, QDomElement& element ) const;
	void restoreState( const QDomElement& element );

private:
	QString m_name;
	bool m_tempoEnabled = false;
	double m_tempo = 120.0;
	bool m_timeSigEnabled = false;
	int m_timeSigNumerator = 4;
	int m_timeSigDenominator = 4;
};

class LMMS_EXPORT SessionModel
{
public:
	//! Version of the <session> block this build writes. See the version
	//! policy in SessionModel.cpp.
	static constexpr int CurrentVersion = 1;
	//! Default global launch quantisation (Live's default: one bar).
	static constexpr LaunchQuantisation DefaultLaunchQuantisation = LaunchQuantisation::Bar;

	SessionModel() = default;

	// ---- grid ---------------------------------------------------------

	int trackCount() const { return m_trackCount; }
	int sceneCount() const { return m_sceneCount; }

	//! Resize the grid. Existing slots keep their content and grid position;
	//! cells that appear are empty.
	void setTrackCount( int tracks );
	void setSceneCount( int scenes );

	//! Slot at (track, scene). Out-of-range access returns a shared empty
	//! slot so callers cannot corrupt the grid; index with valid coordinates.
	const ClipSlot& slot( int track, int scene ) const;
	ClipSlot& slot( int track, int scene );

	const Scene& scene( int scene ) const;
	Scene& scene( int scene );

	// ---- session-wide state -------------------------------------------

	LaunchQuantisation globalLaunchQuantisation() const { return m_globalLaunchQuantisation; }
	void setGlobalLaunchQuantisation( LaunchQuantisation quantisation ) { m_globalLaunchQuantisation = quantisation; }

	//! True when the model holds nothing worth persisting: no grid, no clip,
	//! no scene override and the default global quantisation.
	bool isEmpty() const;

	//! Whether saveState() should emit a <session> block. A project that never
	//! used the session view (no block on load, nothing added since) re-saves
	//! without one, byte-identically.
	bool shouldPersist() const { return m_hadSessionBlock || !isEmpty(); }

	//! True when the loaded project contained a <session> element.
	bool hadSessionBlock() const { return m_hadSessionBlock; }

	void clear();

	// ---- persistence ---------------------------------------------------

	//! Appends <session version="1" ...> to `parent` and returns it. The
	//! caller decides whether to call this (see shouldPersist()).
	QDomElement saveState( QDomDocument& doc, QDomElement& parent ) const;

	//! Loads a <session> element. Returns false when the block was not
	//! understood (unknown/future version); such a block is preserved verbatim
	//! so a re-save through this build does not drop it.
	bool restoreState( const QDomElement& element );

	//! Raw XML of a <session> block written by a newer build; empty otherwise.
	const QString& preservedUnknownXml() const { return m_preservedUnknownXml; }

private:
	void resize( int tracks, int scenes );

	int m_trackCount = 0;
	int m_sceneCount = 0;
	std::vector<ClipSlot> m_slots; //!< track * sceneCount + scene
	std::vector<Scene> m_scenes;
	LaunchQuantisation m_globalLaunchQuantisation = DefaultLaunchQuantisation;
	bool m_hadSessionBlock = false;
	QString m_preservedUnknownXml;
};

} // namespace lmms

#endif // LMMS_SESSION_MODEL_H
