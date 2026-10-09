/*
 * AutomationClip.cpp - implementation of class AutomationClip which
 *                         holds dynamic values
 *
 * Copyright (c) 2008-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
 * Copyright (c) 2006-2008 Javier Serrano Polo <jasp00/at/users.sourceforge.net>
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

#include <cmath>
#include <new>
#include <vector>

#include "AutomationRamp.h"
#include "AutomationClip.h"

#include "AudioEngine.h"

#include "AutomationNode.h"
#include "AutomationClipView.h"
#include "AutomationTrack.h"
#include "KeyboardShortcuts.h"
#include "LocaleHelper.h"
#include "Note.h"
#include "PatternStore.h"
#include "ProjectJournal.h"
#include "Song.h"


namespace lmms
{

std::atomic<int> AutomationClip::s_quantization{1};
const float AutomationClip::DEFAULT_MIN_VALUE = 0;
const float AutomationClip::DEFAULT_MAX_VALUE = 1;


AutomationClip::AutomationClip( AutomationTrack * _auto_track ) :
	Clip( _auto_track ),
	m_autoTrack( _auto_track ),
	m_objects(),
	m_tension( 1.0 ),
	m_progressionType( ProgressionType::Discrete ),
	m_dragging( false ),
	m_isRecording( false ),
	m_lastRecordedValue( 0 )
{
	changeLength( TimePos( 1, 0 ) );
}


AutomationClip::AutomationClip( const AutomationClip & _clip_to_copy ) :
	Clip(_clip_to_copy),
	m_autoTrack( _clip_to_copy.m_autoTrack ),
	m_objects( _clip_to_copy.m_objects ),
	m_tension( _clip_to_copy.m_tension ),
	m_progressionType(_clip_to_copy.m_progressionType),
	m_relative(_clip_to_copy.isRelative()),
	m_rampSetting(_clip_to_copy.m_rampSetting),
	m_dragging(false),
	m_isRecording(_clip_to_copy.m_isRecording),
	m_lastRecordedValue(0)
{
	// Locks the mutex of the copied AutomationClip to make sure it
	// doesn't change while it's being copied
	QMutexLocker m(&_clip_to_copy.m_clipMutex);

	for( timeMap::const_iterator it = _clip_to_copy.m_timeMap.begin();
				it != _clip_to_copy.m_timeMap.end(); ++it )
	{
		// Copies the automation node (in/out values and in/out tangents)
		m_timeMap[POS(it)] = it.value();
	}
	publishReadSnapshot();
}

bool AutomationClip::addObject( AutomatableModel * _obj, bool _search_dup )
{
	QMutexLocker m(&m_clipMutex);

	if (_search_dup && std::find(m_objects.begin(), m_objects.end(), _obj) != m_objects.end())
	{
		return false;
	}

	// the automation track is unconnected and there is nothing in the track
	if (m_objects.empty() && hasAutomation() == false)
	{
		// then initialize first value
		putValue( TimePos(0), _obj->inverseScaledValue( _obj->value<float>() ), false );
	}

	m_objects.push_back(_obj);

	connect( _obj, SIGNAL(destroyed(lmms::jo_id_t)),
			this, SLOT(objectDestroyed(lmms::jo_id_t)),
						Qt::DirectConnection );

	emit dataChanged();

	return true;
}




void AutomationClip::setProgressionType(
					ProgressionType _new_progression_type )
{
	QMutexLocker m(&m_clipMutex);

	if ( _new_progression_type == ProgressionType::Discrete ||
		_new_progression_type == ProgressionType::Linear ||
		_new_progression_type == ProgressionType::CubicHermite )
	{
		m_progressionType = _new_progression_type;
		publishReadSnapshot();
		emit dataChanged();
	}
}




void AutomationClip::setTension( QString _new_tension )
{
	QMutexLocker m(&m_clipMutex);

	bool ok;
	float nt = LocaleHelper::toFloat(_new_tension, & ok);

	if( ok && nt > -0.01 && nt < 1.01 )
	{
		m_tension = nt;
		publishReadSnapshot();
	}
}




const AutomatableModel * AutomationClip::firstObject() const
{
	AutomatableModel* model;
	if (!m_objects.empty() && (model = m_objects.front()) != nullptr)
	{
		return model;
	}

	static FloatModel fm(0, DEFAULT_MIN_VALUE, DEFAULT_MAX_VALUE, 0.001f);
	return &fm;
}

const AutomationClip::objectVector& AutomationClip::objects() const
{
	return m_objects;
}




TimePos AutomationClip::timeMapLength() const
{
	QMutexLocker m(&m_clipMutex);

	TimePos one_bar = TimePos(1, 0);
	if (m_timeMap.isEmpty()) { return one_bar; }

	timeMap::const_iterator it = m_timeMap.end();
	auto last_tick = static_cast<tick_t>(POS(std::prev(it)));
	// if last_tick is 0 (single item at tick 0)
	// return length as a whole bar to prevent disappearing Clip
	if (last_tick == 0) { return one_bar; }

	return TimePos(last_tick);
}




void AutomationClip::updateLength()
{
	// Technically it only matters if the clip has been resized from the right, but this
	// checks if it has been resized from either direction.
	if (getAutoResize())
	{
		if (m_autoTrack != nullptr && m_autoTrack->trackContainer() == Engine::patternStore())
		{
			// If inside a pattern, the clip is always the length of it.
			changeLength(TimePos::ticksPerBar() * Engine::patternStore()->lengthOfPattern(m_autoTrack->getClipNum(this)));
			return;
		}
		// Using 1 bar as the min length for an un-resized clip. 
		// This does not prevent the user from resizing the clip to be less than a bar later on.
		changeLength(std::max(TimePos::ticksPerBar(), static_cast<tick_t>(timeMapLength())));
		setStartTimeOffset(TimePos(0));
	}
}




/**
 * @brief Puts an automation node on the timeMap with the given value.
 *        The inValue and outValue of the created node will be the same.
 * @param TimePos time to add the node to
 * @param Float inValue and outValue of the node
 * @param Boolean True to quantize the position (defaults to true)
 * @param Boolean True to ignore unquantized surrounding nodes (defaults to true)
 * @return TimePos of the recently added automation node
 */
TimePos AutomationClip::putValue(
	const TimePos & time,
	const float value,
	const bool quantPos,
	const bool ignoreSurroundingPoints
)
{
	QMutexLocker m(&m_clipMutex);

	cleanObjects();

	TimePos newTime = quantPos ? Note::quantized(time, quantization()) : time;
	newTime = std::max(TimePos(0), newTime);

	// Create a node or replace the existing one on newTime
	m_timeMap[newTime] = AutomationNode(value);

	timeMap::iterator it = m_timeMap.find(newTime);

	// Remove control points that are covered by the new points
	// quantization value. Control Key to override
	if (!ignoreSurroundingPoints)
	{
		// We need to check that to avoid removing nodes from
		// newTime + 1 to newTime (removing the node we are adding)
		if (quantization() > 1)
		{
			// Remove nodes between the quantization points, them not
			// being included
			const bool previouslySuppressed = m_snapshotPublicationSuppressed;
			m_snapshotPublicationSuppressed = true;
			removeNodes(newTime + 1, newTime + quantization() - 1);
			m_snapshotPublicationSuppressed = previouslySuppressed;
		}
	}
	if (it != m_timeMap.begin()) { --it; }
	generateTangents(it, 3);

	updateLength();

	publishReadSnapshot();
	emit dataChanged();

	return newTime;
}




/**
 * @brief Puts an automation node on the timeMap with the given inValue
 *        and outValue.
 * @param TimePos time to add the node to
 * @param Float inValue of the node
 * @param Float outValue of the node
 * @param Boolean True to quantize the position (defaults to true)
 * @param Boolean True to ignore unquantized surrounding nodes (defaults to true)
 * @return TimePos of the recently added automation node
 */
TimePos AutomationClip::putValues(
	const TimePos & time,
	const float inValue,
	const float outValue,
	const bool quantPos,
	const bool ignoreSurroundingPoints
)
{
	QMutexLocker m(&m_clipMutex);

	cleanObjects();

	TimePos newTime = quantPos ? Note::quantized(time, quantization()) : time;
	newTime = std::max(TimePos(0), newTime);

	// Create a node or replace the existing one on newTime
	m_timeMap[newTime] = AutomationNode(inValue, outValue);

	timeMap::iterator it = m_timeMap.find(newTime);

	// Remove control points that are covered by the new points
	// quantization value. Control Key to override
	if (!ignoreSurroundingPoints)
	{
		// We need to check that to avoid removing nodes from
		// newTime + 1 to newTime (removing the node we are adding)
		if (quantization() > 1)
		{
			// Remove nodes between the quantization points, them not
			// being included
			const bool previouslySuppressed = m_snapshotPublicationSuppressed;
			m_snapshotPublicationSuppressed = true;
			removeNodes(newTime + 1, newTime + quantization() - 1);
			m_snapshotPublicationSuppressed = previouslySuppressed;
		}
	}
	if (it != m_timeMap.begin()) { --it; }
	generateTangents(it, 3);

	updateLength();

	publishReadSnapshot();
	emit dataChanged();

	return newTime;
}




void AutomationClip::removeNode(const TimePos & time)
{
	QMutexLocker m(&m_clipMutex);

	cleanObjects();

	m_timeMap.remove( time );
	timeMap::iterator it = m_timeMap.lowerBound(time);
	if( it != m_timeMap.begin() )
	{
		--it;
	}
	generateTangents(it, 3);

	updateLength();

	publishReadSnapshot();
	emit dataChanged();
}




/**
 * @brief Removes all automation nodes between the given ticks
 * @param Int first tick of the range
 * @param Int second tick of the range
 */
void AutomationClip::removeNodes(const int tick0, const int tick1)
{
	QMutexLocker m(&m_clipMutex);
	if (tick0 == tick1)
	{
		removeNode(TimePos(tick0));
		return;
	}

	auto start = TimePos(std::min(tick0, tick1));
	auto end = TimePos(std::max(tick0, tick1));

	auto it = m_timeMap.lowerBound(start);
	const auto endIt = m_timeMap.upperBound(end);
	if (it == endIt) { return; }
	while (it != endIt) { it = m_timeMap.erase(it); }
	if (it != m_timeMap.begin()) { --it; }
	generateTangents(it, 3);
	const bool previouslySuppressed = m_snapshotPublicationSuppressed;
	m_snapshotPublicationSuppressed = true;
	updateLength();
	m_snapshotPublicationSuppressed = previouslySuppressed;
	publishReadSnapshot();
	emit dataChanged();
}


bool AutomationClip::setNodeInValue(const TimePos& time, float value)
{
	QMutexLocker m(&m_clipMutex);
	auto it = m_timeMap.find(time);
	if (it == m_timeMap.end()) { return false; }
	it.value().m_inValue = value;
	if (it != m_timeMap.begin()) { --it; }
	generateTangents(it, 3);
	publishReadSnapshot();
	emit dataChanged();
	return true;
}


bool AutomationClip::setNodeOutValue(const TimePos& time, float value)
{
	QMutexLocker m(&m_clipMutex);
	auto it = m_timeMap.find(time);
	if (it == m_timeMap.end()) { return false; }
	it.value().m_outValue = value;
	if (it != m_timeMap.begin()) { --it; }
	generateTangents(it, 3);
	publishReadSnapshot();
	emit dataChanged();
	return true;
}


bool AutomationClip::setNodeTangent(const TimePos& time, bool outgoing, float tangent)
{
	QMutexLocker m(&m_clipMutex);
	auto it = m_timeMap.find(time);
	if (it == m_timeMap.end()) { return false; }
	if (outgoing) { it.value().setOutTangent(tangent); }
	else { it.value().setInTangent(tangent); }
	publishReadSnapshot();
	emit dataChanged();
	return true;
}


bool AutomationClip::setNodeTangents(const TimePos& time, float inTangent, float outTangent, bool locked)
{
	QMutexLocker m(&m_clipMutex);
	auto it = m_timeMap.find(time);
	if (it == m_timeMap.end()) { return false; }
	it.value().setInTangent(inTangent);
	it.value().setOutTangent(outTangent);
	it.value().setLockedTangents(locked);
	publishReadSnapshot();
	emit dataChanged();
	return true;
}


bool AutomationClip::setNodeLockedTangents(const TimePos& time, bool locked)
{
	QMutexLocker m(&m_clipMutex);
	auto it = m_timeMap.find(time);
	if (it == m_timeMap.end()) { return false; }
	it.value().setLockedTangents(locked);
	if (!locked) { generateTangents(it, 1); }
	publishReadSnapshot();
	emit dataChanged();
	return true;
}


bool AutomationClip::setNodeCurve(const TimePos& time, AutomationNode::Curve curve, float bend)
{
	QMutexLocker m(&m_clipMutex);
	auto it = m_timeMap.find(time);
	if (it == m_timeMap.end()) { return false; }
	it.value().setCurve(curve, bend);
	publishReadSnapshot();
	emit dataChanged();
	return true;
}


bool AutomationClip::moveNode(const TimePos& from, const TimePos& to, float inValue)
{
	QMutexLocker m(&m_clipMutex);
	auto source = m_timeMap.find(from);
	if (source == m_timeMap.end()) { return false; }
	if (from != to && m_timeMap.contains(to)) { return false; }

	const auto curve = source.value().curve();
	const float bend = source.value().bend();
	cleanObjects();
	const TimePos newTime = std::max(TimePos(0), to);
	m_timeMap.remove(from);
	m_timeMap[newTime] = AutomationNode(inValue);
	auto moved = m_timeMap.find(newTime);
	moved.value().setCurve(curve, bend);
	if (moved != m_timeMap.begin()) { --moved; }
	generateTangents(moved, 3);
	updateLength();
	publishReadSnapshot();
	emit dataChanged();
	return true;
}


bool AutomationClip::rescaleValuesToRange(float oldMin, float oldMax, float newMin, float newMax)
{
	if (oldMin == oldMax || (oldMin == newMin && oldMax == newMax)) { return false; }

	QMutexLocker m(&m_clipMutex);
	if (m_timeMap.isEmpty()) { return false; }

	for (auto it = m_timeMap.begin(); it != m_timeMap.end(); ++it)
	{
		const float clamped = std::clamp(it.value().m_inValue, oldMin, oldMax);
		it.value().m_inValue = (clamped - oldMin) * (newMax - newMin) / (oldMax - oldMin) + newMin;
		it.value().m_outValue = it.value().m_inValue;
	}
	generateTangents();
	publishReadSnapshot();
	emit dataChanged();
	return true;
}




/**
 * @brief Resets the outValues of all automation nodes between the given ticks
 * @param Int first tick of the range
 * @param Int second tick of the range
 */
void AutomationClip::resetNodes(const int tick0, const int tick1)
{
	QMutexLocker m(&m_clipMutex);
	bool changed = false;
	if (tick0 == tick1)
	{
		auto it = m_timeMap.find(TimePos(tick0));
		if (it != m_timeMap.end() && it.value().m_outValue != it.value().m_inValue)
		{
			it.value().m_outValue = it.value().m_inValue;
			changed = true;
		}
		if (changed) { publishReadSnapshot(); }
		return;
	}

	auto start = TimePos(std::min(tick0, tick1));
	auto end = TimePos(std::max(tick0, tick1));

	for (auto it = m_timeMap.lowerBound(start), endIt = m_timeMap.upperBound(end); it != endIt; ++it)
	{
		if (it.value().m_outValue != it.value().m_inValue)
		{
			it.value().m_outValue = it.value().m_inValue;
			changed = true;
		}
	}
	if (changed) { publishReadSnapshot(); }
}




void AutomationClip::resetTangents(const int tick0, const int tick1)
{
	QMutexLocker m(&m_clipMutex);
	bool changed = false;
	if (tick0 == tick1)
	{
		auto it = m_timeMap.find(TimePos(tick0));
		if (it != m_timeMap.end())
		{
			it.value().setLockedTangents(false);
			generateTangents(it, 1);
			changed = true;
		}
		if (changed) { publishReadSnapshot(); }
		return;
	}

	TimePos start = TimePos(std::min(tick0, tick1));
	TimePos end = TimePos(std::max(tick0, tick1));

	for (auto it = m_timeMap.lowerBound(start), endIt = m_timeMap.upperBound(end); it != endIt; ++it)
	{
		it.value().setLockedTangents(false);
		generateTangents(it, 1);
		changed = true;
	}
	if (changed) { publishReadSnapshot(); }
}




void AutomationClip::recordValue(TimePos time, float value)
{
	recordValueAtMapTime(recordingMapTime(time), value);
}


void AutomationClip::recordValueAtMapTime(TimePos mapTime, float value)
{
	QMutexLocker m(&m_clipMutex);
	const bool previouslySuppressed = m_snapshotPublicationSuppressed;
	m_snapshotPublicationSuppressed = true;

	if( value != m_lastRecordedValue )
	{
		putValue(mapTime, value, false, true);
		m_lastRecordedValue = value;
	}
	else if( editableValueAt(mapTime) != value )
	{
		removeNode(mapTime);
	}
	m_snapshotPublicationSuppressed = previouslySuppressed;
	if (!previouslySuppressed) { publishReadSnapshot(); }
}


void AutomationClip::recordQueuedValue(TimePos mapTime, float value, std::size_t producer, std::uint64_t sequence)
{
	QMutexLocker lock(&m_clipMutex);
	if (producer >= m_appliedRecordingSequences.size())
	{
		m_appliedRecordingSequences.resize(producer + 1, 0);
	}
	m_appliedRecordingSequences[producer] = sequence;
	recordValueAtMapTime(mapTime, value);
}


float AutomationClip::editableValueAt(const TimePos& time) const
{
	if (m_timeMap.isEmpty()) { return 0.0f; }
	auto next = m_timeMap.lowerBound(time);
	if (next != m_timeMap.end() && POS(next) == time) { return INVAL(next); }
	if (next == m_timeMap.begin()) { return 0.0f; }
	auto previous = std::prev(next);
	if (next == m_timeMap.end()) { return OUTVAL(previous); }
	const int offset = time - POS(previous);
	if (offset == 0) { return INVAL(previous); }
	const auto curve = previous.value().curve();
	if (curve == AutomationNode::Curve::Bend)
	{
		const double t = std::clamp(static_cast<double>(offset) / std::max(1, POS(next) - POS(previous)), 0.0, 1.0);
		const double bend = previous.value().bend();
		const double shaped = bend >= 0.0 ? std::pow(t, 1.0 + 4.0 * bend)
			: 1.0 - std::pow(1.0 - t, 1.0 - 4.0 * bend);
		return static_cast<float>(OUTVAL(previous) + (INVAL(next) - OUTVAL(previous)) * shaped);
	}
	const auto type = curve == AutomationNode::Curve::Clip ? m_progressionType
		: curve == AutomationNode::Curve::Hold ? ProgressionType::Discrete
		: curve == AutomationNode::Curve::Linear ? ProgressionType::Linear : ProgressionType::CubicHermite;
	if (type == ProgressionType::Discrete) { return OUTVAL(previous); }
	if (type == ProgressionType::Linear)
	{
		const float slope = (INVAL(next) - OUTVAL(previous)) / (POS(next) - POS(previous));
		return OUTVAL(previous) + offset * slope;
	}
	const int numValues = POS(next) - POS(previous);
	const float t = static_cast<float>(offset) / static_cast<float>(numValues);
	const float m1 = OUTTAN(previous) * numValues * m_tension;
	const float m2 = INTAN(next) * numValues * m_tension;
	const auto t2 = t * t, t3 = t2 * t;
	return (2 * t3 - 3 * t2 + 1) * OUTVAL(previous)
		+ (t3 - 2 * t2 + t) * m1 + (-2 * t3 + 3 * t2) * INVAL(next) + (t3 - t2) * m2;
}




/**
 * @brief Set the position of the point that is being dragged.
 *        Calling this function will also automatically set m_dragging to true.
 *        When applyDragValue() is called, m_dragging is set back to false.
 * @param TimePos of the node being dragged
 * @param Float with the value to assign to the point being dragged
 * @param Boolean. True to snip x position
 * @param Boolean. True to ignore unquantized surrounding nodes
 * @return TimePos with current time of the dragged value
 */
TimePos AutomationClip::setDragValue(
	const TimePos & time,
	const float value,
	const bool quantPos,
	const bool controlKey
)
{
	QMutexLocker m(&m_clipMutex);
	const bool previouslySuppressed = m_snapshotPublicationSuppressed;
	m_snapshotPublicationSuppressed = true;

	if (m_dragging == false)
	{
		TimePos newTime = quantPos ? Note::quantized(time, quantization()) : time;

		// We will keep the same outValue only if it's different from the
		// inValue
		m_dragKeepOutValue = false;

		// We will set the tangents back to what they were if the node had
		// its tangents locked
		m_dragLockedTan = false;

		// Check if we already have a node on the position we are dragging
		// and if we do, store the outValue so the discrete jump can be kept
		// and information about the tangents
		timeMap::iterator it = m_timeMap.find(newTime);
		if (it != m_timeMap.end())
		{
			// If we don't have a discrete jump, the outValue will be the
			// same as the inValue
			if (OFFSET(it) != 0)
			{
				m_dragKeepOutValue = true;
				m_dragOutValue = OUTVAL(it);
			}
			// For the tangents, we will only keep them if the tangents were
			// locked
			if (LOCKEDTAN(it))
			{
				m_dragLockedTan = true;
				m_dragInTan = INTAN(it);
				m_dragOutTan = OUTTAN(it);
			}
		}

		this->removeNode(newTime);
		m_oldTimeMap = m_timeMap;
		m_dragging = true;
	}

	//Restore to the state before it the point were being dragged
	m_timeMap = m_oldTimeMap;

	generateTangents();

	TimePos returnedPos;

	if (m_dragKeepOutValue)
	{
		returnedPos = this->putValues(time, value, m_dragOutValue, quantPos, controlKey);
	}
	else
	{
		returnedPos = this->putValue(time, value, quantPos, controlKey);
	}

	// Set the tangents on the newly created node if they were locked
	// before dragging
	if (m_dragLockedTan)
	{
		timeMap::iterator it = m_timeMap.find(returnedPos);
		if (it != m_timeMap.end())
		{
			it.value().setInTangent(m_dragInTan);
			it.value().setOutTangent(m_dragOutTan);
			it.value().setLockedTangents(true);
		}
	}
	m_snapshotPublicationSuppressed = previouslySuppressed;
	publishReadSnapshot();

	return returnedPos;
}




/**
 * @brief After the point is dragged, this function is called to apply the change.
 */
void AutomationClip::applyDragValue()
{
	QMutexLocker m(&m_clipMutex);

	m_dragging = false;
}




/*! Sample-accurate automation (feature-list row 9,
 *  docs/SAMPLE-ACCURATE-AUTOMATION.md): the clip's own opt-in, and the ramp
 *  builder the audio thread calls once per block.
 *
 *  WHY ONE KNOT PER TICK BOUNDARY IS THE WHOLE CURVE. A clip's nodes live on
 *  integer ticks, and the stored value is linear in ticks between two of them,
 *  so inside one tick the curve is a straight line. An audio block covers a
 *  handful of ticks and the tick grid does not line up with the block grid: a
 *  block starts somewhere inside a tick and ends somewhere inside another. So a
 *  ramp carrying the curve's value at the block's first sample, at every tick
 *  boundary inside the block and at the block's end reproduces the curve at
 *  every frame in between.
 *
 *  EVERY BOUNDARY GETS TWO KNOTS - the frame just BEFORE it and the frame ON it
 *  - because the clip's default progression type is Discrete, where the value
 *  HOLDS for a whole tick and then jumps at the next node. Without the knot at
 *  `frame - 1` the ramp would ramp into the jump and smear it over the last
 *  tick. For Linear the two knots are one frame apart and the line between them
 *  is the curve to within one frame's slope.
 *
 *  It is an APPROXIMATION for CubicHermite, whose stored shape inside one tick
 *  is a cubic: the row's own limitation line says so, and the surface reports
 *  the progression type rather than implying an exactness it does not have.
 */
// R1.2: the knot cap holds for the densest block the engine renders (AutomationRamp.h).
static_assert( 2 * ( ( DEFAULT_BUFFER_SIZE * 999 * 48 ) / ( 22050 * 60 ) + 1 ) + 2 <= AutomationRamp::MaxKnots,
	"a 256-frame block at 999 BPM and 22.05 kHz must fit the automation ramp" );

float *AutomationClip::valuesAfter( const TimePos & _time ) const
{
	QMutexLocker m(&m_clipMutex);

	timeMap::const_iterator v = m_timeMap.lowerBound(_time);
	auto const nv = std::next(v);

	if (v == m_timeMap.end() || nv == m_timeMap.end())
	{
		return nullptr;
	}

	int numValues = POS(nv) - POS(v);
	auto ret = new float[numValues];

	for( int i = 0; i < numValues; i++ )
	{
		ret[i] = editableValueAt(POS(v) + i);
	}

	return ret;
}




// flipY / flipX, and the 0.5.0 range edits (shapes, transform, thinning): AutomationClipTransforms.cpp.




void AutomationClip::saveSettings( QDomDocument & _doc, QDomElement & _this )
{
	QMutexLocker m(&m_clipMutex);

	_this.setAttribute( "pos", startPosition() );
	_this.setAttribute( "len", length() );
	_this.setAttribute( "name", name() );
	_this.setAttribute( "prog", QString::number( static_cast<int>(progressionType()) ) );
	_this.setAttribute( "tens", QString::number( getTension() ) );
	if (isRelative()) { _this.setAttribute("relative", 1); }  // ED-199, only when set
	_this.setAttribute( "mute", QString::number( isMuted() ) );
	_this.setAttribute("off", startTimeOffset());
	_this.setAttribute("autoresize", QString::number(getAutoResize()));
	// Sample-accurate automation (feature row 9, R1.2): written ONLY when it was set
	// explicitly - "1" exactly as before, "0" for an explicit off - so a clip that never
	// asked serialises exactly what it always did. loadSettings resets it to the default
	// on absence, which is what lets a journal checkpoint taken before the first edit
	// take it back.
	if( m_rampSetting != RampSetting::Default )
	{
		_this.setAttribute( "sample_accurate", QString::number( m_rampSetting == RampSetting::On ? 1 : 0 ) );
	}

	if (const auto& c = color())
	{
		_this.setAttribute("color", c->name());
	}

	for( timeMap::const_iterator it = m_timeMap.begin();
						it != m_timeMap.end(); ++it )
	{
		QDomElement element = _doc.createElement( "time" );
		element.setAttribute("pos", POS(it));
		element.setAttribute("value", INVAL(it));
		element.setAttribute("outValue", OUTVAL(it));
		element.setAttribute("inTan", INTAN(it));
		element.setAttribute("outTan", OUTTAN(it));
		element.setAttribute("lockedTan", static_cast<int>(LOCKEDTAN(it)));
		it.value().saveCurve(element);
		_this.appendChild( element );
	}

	for (const auto& object : m_objects)
	{
		if (object)
		{
			QDomElement element = _doc.createElement( "object" );
			element.setAttribute("id", ProjectJournal::idToWrite(object->id()));  // WF-033: exact in an undo snapshot
			_this.appendChild(element);
		}
	}
}




void AutomationClip::loadSettings( const QDomElement & _this )
{
	QMutexLocker m(&m_clipMutex);

	// Legacy compatibility: Previously tangents were not stored in
	// the project file. So if any node doesn't have tangent information
	// we will generate the tangents
	bool shouldGenerateTangents = false;

	clear();

	movePosition( _this.attribute( "pos" ).toInt() );
	setRelative(_this.attribute("relative", "0").toInt() != 0);
	setName( _this.attribute( "name" ) );
	setProgressionType( static_cast<ProgressionType>( _this.attribute(
							"prog" ).toInt() ) );
	setTension( _this.attribute( "tens" ) );
	setMuted(_this.attribute( "mute", QString::number( false ) ).toInt() );
	setAutoResize(_this.attribute("autoresize", "1").toInt());
	setStartTimeOffset(_this.attribute("off").toInt());
	// Sample-accurate automation (feature row 9). RESET on absence, not left as
	// it was: the flag is written only when it is on, so a checkpoint's saved
	// state (which omits it) has to be able to turn it back off. Without this
	// line control.undo could not take the first automation.ramp_set back.
	setRampSetting( !_this.hasAttribute( "sample_accurate" ) ? RampSetting::Default
		: _this.attribute( "sample_accurate" ).toInt() != 0 ? RampSetting::On : RampSetting::Off );

	for( QDomNode node = _this.firstChild(); !node.isNull();
						node = node.nextSibling() )
	{
		QDomElement element = node.toElement();
		if( element.isNull()  )
		{
			continue;
		}
		if( element.tagName() == "time" )
		{
			int timeMapPos = element.attribute("pos").toInt();
			float timeMapInValue = LocaleHelper::toFloat(element.attribute("value"));
			float timeMapOutValue = LocaleHelper::toFloat(element.attribute("outValue"));

			m_timeMap[timeMapPos] = AutomationNode(timeMapInValue, timeMapOutValue);
			m_timeMap[timeMapPos].loadCurve(element);

			// Load tangents if there is information about it (it's enough to check for either inTan or outTan)
			if (element.hasAttribute("inTan"))
			{
				float inTan = LocaleHelper::toFloat(element.attribute("inTan"));
				float outTan = LocaleHelper::toFloat(element.attribute("outTan"));
				bool lockedTan = static_cast<bool>(element.attribute("lockedTan", "0").toInt());

				m_timeMap[timeMapPos].setInTangent(inTan);
				m_timeMap[timeMapPos].setOutTangent(outTan);
				m_timeMap[timeMapPos].setLockedTangents(lockedTan);
			}
			else
			{
				shouldGenerateTangents = true;
			}
		}
		else if( element.tagName() == "object" )
		{
			m_idsToResolve.push_back(element.attribute("id").toInt());
		}
	}
	
	if (_this.hasAttribute("color"))
	{
		setColor(QColor{_this.attribute("color")});
	}

	int len = _this.attribute( "len" ).toInt();
	if( len <= 0 )
	{
		// TODO: Handle with an upgrade method
		updateLength();
	}
	else
	{
		changeLength( len );
	}

	if (shouldGenerateTangents) { generateTangents(); }
	publishReadSnapshot();
}




QString AutomationClip::name() const
{
	QMutexLocker m(&m_clipMutex);

	if( !Clip::name().isEmpty() )
	{
		return Clip::name();
	}
	if (!m_objects.empty() && m_objects.front() != nullptr)
	{
		return m_objects.front()->fullDisplayName();
	}
	return tr( "Drag a control while pressing <%1>" ).arg(UI_COPY_KEY);
}




gui::ClipView * AutomationClip::createView( gui::TrackView * _tv )
{
	QMutexLocker m(&m_clipMutex);

	return new gui::AutomationClipView( this, _tv );
}





bool AutomationClip::isAutomated( const AutomatableModel * _m )
{
	auto l = combineAllTracks();
	for (const auto track : l)
	{
		if (track->type() == Track::Type::Automation || track->type() == Track::Type::HiddenAutomation)
		{
			for (const auto& clip : track->getClips())
			{
				const auto a = dynamic_cast<const AutomationClip*>(clip);
				if( a && a->hasAutomation() )
				{
					for (const auto& object : a->m_objects)
					{
						if (object == _m)
						{
							return true;
						}
					}
				}
			}
		}
	}
	return false;
}


/**
 * @brief returns a list of all the automation clips that are connected to a specific model
 * @param _m the model we want to look for
 */
std::vector<AutomationClip *> AutomationClip::clipsForModel(const AutomatableModel* _m)
{
	std::vector<AutomationClip *> clips;
	auto l = combineAllTracks();

	// go through all tracks...
	for (const auto track : l)
	{
		// we want only automation tracks...
		if (track->type() == Track::Type::Automation || track->type() == Track::Type::HiddenAutomation )
		{
			// go through all the clips...
			for (const auto& trackClip : track->getClips())
			{
				auto a = dynamic_cast<AutomationClip*>(trackClip);
				// check that the clip has automation
				if( a && a->hasAutomation() )
				{
					// now check is the clip is connected to the model we want by going through all the connections
					// of the clip
					bool has_object = false;
					for (const auto& object : a->m_objects)
					{
						if (object == _m)
						{
							has_object = true;
						}
					}
					// if the clips is connected to the model, add it to the list
					if (has_object) { clips.push_back(a); }
				}
			}
		}
	}
	return clips;
}



AutomationClip * AutomationClip::globalAutomationClip(
							AutomatableModel * _m )
{
	AutomationTrack * t = Engine::getSong()->globalAutomationTrack();
	for (const auto& clip : t->getClips())
	{
		auto a = dynamic_cast<AutomationClip*>(clip);
		if( a )
		{
			for (const auto& object : a->m_objects)
			{
				if (object == _m) { return a; }
			}
		}
	}

	auto a = new AutomationClip(t);
	a->addObject( _m, false );
	return a;
}




void AutomationClip::resolveAllIDs()
{
	auto l = combineAllTracks();
	for (const auto& track : l)
	{
		if (track->type() == Track::Type::Automation || track->type() == Track::Type::HiddenAutomation)
		{
			for (const auto& clip : track->getClips())
			{
				auto a = dynamic_cast<AutomationClip*>(clip);
				if( a )
				{
					for (const auto& id : a->m_idsToResolve)
					{
						JournallingObject* o = Engine::projectJournal()->journallingObject(id);
						if( o && dynamic_cast<AutomatableModel *>( o ) )
						{
							a->addObject( dynamic_cast<AutomatableModel *>( o ), false );
						}
						else
						{
							// FIXME: Remove this block once the automation system gets fixed
							// This is a temporary fix for https://github.com/LMMS/lmms/issues/3781
							o = Engine::projectJournal()->journallingObject(ProjectJournal::idFromSave(id));
							if( o && dynamic_cast<AutomatableModel *>( o ) )
							{
								a->addObject( dynamic_cast<AutomatableModel *>( o ), false );
							}
							else
							{
								// FIXME: Remove this block once the automation system gets fixed
								// This is a temporary fix for https://github.com/LMMS/lmms/issues/4781
								o = Engine::projectJournal()->journallingObject(ProjectJournal::idToSave(id));
								if( o && dynamic_cast<AutomatableModel *>( o ) )
								{
									a->addObject( dynamic_cast<AutomatableModel *>( o ), false );
								}
							}
						}
					}
					a->m_idsToResolve.clear();
					a->dataChanged();
				}
			}
		}
	}
}




void AutomationClip::clear()
{
	QMutexLocker m(&m_clipMutex);

	m_timeMap.clear();
	publishReadSnapshot();

	emit dataChanged();
}




void AutomationClip::objectDestroyed( jo_id_t _id )
{
	QMutexLocker m(&m_clipMutex);

	// TODO: distict between temporary removal (e.g. LADSPA controls
	// when switching samplerate) and real deletions because in the latter
	// case we had to remove ourselves if we're the global automation
	// clip of the destroyed object
	m_idsToResolve.push_back(_id);

	for (auto objIt = m_objects.begin(); objIt != m_objects.end(); objIt++)
	{
		Q_ASSERT( !(*objIt).isNull() );
		if( (*objIt)->id() == _id )
		{
			//Assign to objIt so that this loop work even break; is removed.
			objIt = m_objects.erase( objIt );
			break;
		}
	}

	emit dataChanged();
}




void AutomationClip::cleanObjects()
{
	QMutexLocker m(&m_clipMutex);

	for( objectVector::iterator it = m_objects.begin(); it != m_objects.end(); )
	{
		if( *it )
		{
			++it;
		}
		else
		{
			it = m_objects.erase( it );
		}
	}
}




void AutomationClip::generateTangents()
{
	generateTangents(m_timeMap.begin(), m_timeMap.size());
}




// We have two tangents, one for the left side of the node and one for the right side
// of the node (in case we have discrete value jumps in the middle of a curve).
// If the inValue and outValue of a node are the same, consequently the inTangent and
// outTangent values of the node will be the same too.
void AutomationClip::generateTangents(timeMap::iterator it, int numToGenerate)
{
	QMutexLocker m(&m_clipMutex);

	for (int i = 0; i < numToGenerate && it != m_timeMap.end(); ++i, ++it)
	{
		// Skip the node if it has locked tangents (were manually edited)
		if (LOCKEDTAN(it))
		{
			continue;
		}

		auto const nit = std::next(it);

		if (nit == m_timeMap.end())
		{
			// Previously, the last value's tangent was always set to 0. That logic was kept for both tangents
			// of the last node
			it.value().setInTangent(0);
			it.value().setOutTangent(0);
		}
		else if (it == m_timeMap.begin())
		{
			// On the first node there's no curve behind it, so we will only calculate the outTangent
			// and inTangent will be set to 0.
			float tangent = (INVAL(nit) - OUTVAL(it)) / (POS(nit) - POS(it));
			it.value().setInTangent(0);
			it.value().setOutTangent(tangent);
		}
		else
		{
			// When we are in a node that is in the middle of two other nodes, we need to check if we
			// have a discrete jump at this node. If we do not, then we can calculate the tangents normally.
			// If we do have a discrete jump, then we have to calculate the tangents differently for each side
			// of the curve.
			// TODO: This behavior means that a very small difference between the inValue and outValue can
			// result in a big change in the curve. In the future, allowing the user to manually adjust
			// the tangents would be better.

			auto const pit = std::prev(it);

			if (OFFSET(it) == 0)
			{
				float inTangent = (INVAL(nit) - OUTVAL(pit)) / (POS(nit) - POS(pit));
				it.value().setInTangent(inTangent);
				// inTangent == outTangent in this case
				it.value().setOutTangent(inTangent);
			}
			else
			{
				// Calculate the left side of the curve
				float inTangent = (INVAL(it) - OUTVAL(pit)) / (POS(it) - POS(pit));
				// Calculate the right side of the curve
				float outTangent = (INVAL(nit) - OUTVAL(it)) / (POS(nit) - POS(it));
				it.value().setInTangent(inTangent);
				it.value().setOutTangent(outTangent);
			}
		}
	}
}

std::vector<Track*> AutomationClip::combineAllTracks()
{
	std::vector<Track*> combinedTrackList;

	auto& songTracks = Engine::getSong()->tracks();
	auto& patternStoreTracks = Engine::patternStore()->tracks();

	combinedTrackList.insert(combinedTrackList.end(), songTracks.begin(), songTracks.end());
	combinedTrackList.insert(combinedTrackList.end(), patternStoreTracks.begin(), patternStoreTracks.end());
	combinedTrackList.push_back(Engine::getSong()->globalAutomationTrack());

	return combinedTrackList;
}

} // namespace lmms
