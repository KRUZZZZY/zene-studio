/*
 * Mixer.cpp - effect mixer for LMMS
 *
 * Copyright (c) 2008-2011 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include "Mixer.h"
#include "RecorderTaps.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QDebug>
#include <QDomElement>
#include <QSet>
#include <QScopedValueRollback>

#include "AudioBusHandle.h"
#include "AudioEngine.h"
#include "AudioEngineWorkerThread.h"
#include "AudioEngineWorkerThread.h"
#include "AutomatableModel.h"
#include "AutomationClip.h"
#include "DspLoadProbe.h"
#include "Mixer.h"
#include "Song.h"
#include "VcaGroup.h"

#include "InstrumentOutputBuses.h"
#include "InstrumentTrack.h"
#include "LocaleHelper.h"
#include "MixHelpers.h"
#include "MixerListen.h"
#include "MixerGroups.h"
#include "MixerSendPan.h"
#include "MixerStrip.h"
#include "StripMeters.h"
#include "PatternStore.h"
#include "ProjectIds.h"
#include "SampleTrack.h"
#include "TrackContainer.h" // For TrackContainer::TrackList typedef
#include "TrackFolder.h"

namespace lmms
{

namespace
{

//! What a project-supplied channel index turned out to be.
enum class ProjectIndex
{
	Ok,          //!< a channel index this mixer can address
	NotANumber,  //!< the attribute carries no number at all
	OutOfRange   //!< a number the project file does not back up
};

//! True when the text is a decimal integer literal - a number that did not
//! fit the 64-bit conversion, rather than a non-number.
bool looksLikeDecimalInteger(const QString& text)
{
	// QString::toLongLong() trims the whitespace around the number before it
	// converts it (" 42 " is 42), so this classification has to trim the same
	// way: a padded number that did not fit 64 bits is still a number, and it
	// must be reported as one instead of being dropped as a non-number.
	const QString trimmed = text.trimmed();
	int i = 0;
	if (!trimmed.isEmpty() && (trimmed.at(0) == QLatin1Char('-') || trimmed.at(0) == QLatin1Char('+'))) { i = 1; }
	if (i >= trimmed.size()) { return false; }
	for (; i < trimmed.size(); ++i)
	{
		const QChar c = trimmed.at(i);
		if (c < QLatin1Char('0') || c > QLatin1Char('9')) { return false; }
	}
	return true;
}

//! The number of distinct channel indices the project file describes. The
//! application writes one <mixerchannel> element per channel with a distinct
//! index, so a saved project keeps its full range. Counting unique indices
//! prevents duplicate elements from inflating the allocation bound.
int countDescribedChannels( const QDomElement & _this )
{
	QSet<qlonglong> indices;
	for( QDomNode node = _this.firstChild(); ! node.isNull(); node = node.nextSibling() )
	{
		const QDomElement channel = node.toElement();
		if( channel.nodeName() != QString( "mixerchannel" ) ) { continue; }
		bool ok = false;
		const qlonglong index = channel.attribute( "num" ).toLongLong( &ok );
		if( ok && index >= 0 ) { indices.insert( index ); }
	}
	return indices.size();
}

//! True when the index is one the project file backs up: 0..describedChannels.
//! The bound is the FILE's own distinct channel indices, not a fixed number, so a
//! project the application created and saved loads however many channels it
//! has - while an index the file does not describe (which is what a hostile
//! file asks for) is refused: honouring it would let a 60-byte file choose
//! how much memory the load takes.
bool isSupportedProjectMixerIndex(int index, int describedChannels)
{
	return index >= 0 && index <= describedChannels;
}

//! Read a project-supplied channel index. The value is converted as a WIDE
//! integer first: QString::toInt() fails on a number that does not fit in an
//! int and returns 0 for it, and 0 is the master - a conversion failure must
//! never become a channel. An index the file does not back up is refused.
ProjectIndex parseProjectChannelIndex(const QString& text, int describedChannels, int& index)
{
	bool ok = false;
	const qlonglong value = text.toLongLong(&ok);
	if (!ok)
	{
		// The number did not even fit in 64 bits. It is still a number, so
		// it is refused AND reported, not dropped as a non-number.
		index = 0;
		return looksLikeDecimalInteger(text) ? ProjectIndex::OutOfRange : ProjectIndex::NotANumber;
	}
	if (value < 0 || value > describedChannels)
	{
		index = 0;
		return ProjectIndex::OutOfRange;
	}
	index = static_cast<int>(value);
	return ProjectIndex::Ok;
}

//! True when @a element carries @a name as a float attribute whose value is not finite.
//! loadSettings() would otherwise feed it straight into the model (NaN, +/-inf), so the
//! loader skips that one setting and keeps the default it already reset to.
bool hasNonFiniteFloatAttribute(const QDomElement& element, const QString& name)
{
	if (!element.hasAttribute(name)) { return false; }
	// The converted value is checked regardless of the conversion flag: an overflowing
	// literal ("1e999") converts to infinity with the flag cleared.
	const float value = LocaleHelper::toFloat(element.attribute(name));
	return !std::isfinite(value);
}

//! True when any point of @a clip's automation carries a non-finite float.
bool hasNonFiniteAutomationValue(const QDomElement& clip)
{
	for (QDomElement point = clip.firstChildElement(QStringLiteral("time")); !point.isNull();
		 point = point.nextSiblingElement(QStringLiteral("time")))
	{
		if (hasNonFiniteFloatAttribute(point, QStringLiteral("value"))
			|| hasNonFiniteFloatAttribute(point, QStringLiteral("outValue"))
			|| hasNonFiniteFloatAttribute(point, QStringLiteral("inTan"))
			|| hasNonFiniteFloatAttribute(point, QStringLiteral("outTan"))
			|| hasNonFiniteFloatAttribute(point, QStringLiteral("bend")))
		{
			return true;
		}
	}
	return false;
}

//! The child element AutomatableModel::loadSettings() would read @a name's value from.
QDomElement modelValueElement(const QDomElement& element, const QString& name)
{
	QDomElement mismatchedNamedElement;
	for (QDomElement child = element.firstChildElement(); !child.isNull(); child = child.nextSiblingElement())
	{
		const bool matches = (!child.hasAttribute(QStringLiteral("nodename")) && child.tagName() == name)
			|| child.attribute(QStringLiteral("nodename")) == name;
		if (matches)
		{
			return child;
		}
		// AutomatableModel::loadSettings() starts with namedItem(name). If that
		// element's nodename mismatches and there is no matching sibling, it still
		// loads the original element, so validate it too.
		if (mismatchedNamedElement.isNull() && child.tagName() == name) { mismatchedNamedElement = child; }
	}
	return mismatchedNamedElement;
}

//! True when the model @a name of @a element would load a non-finite value, from its
//! attribute form, its child-element form or its automation clip.
bool hasNonFiniteModelValue(const QDomElement& element, const QString& name)
{
	const QDomElement automation = element.firstChildElement(AutomationClip::classNodeName())
		.firstChildElement(name);
	if (!automation.isNull()) { return hasNonFiniteAutomationValue(automation); }

	const QDomElement valueElement = modelValueElement(element, name);
	return valueElement.isNull()
		? hasNonFiniteFloatAttribute(element, name)
		: hasNonFiniteFloatAttribute(valueElement, QStringLiteral("value"));
}

//! Load a persisted mixer model unless the file would give it a non-finite value.
void loadFiniteMixerSetting(FloatModel& model, const QDomElement& element, const QString& name)
{
	if (!hasNonFiniteModelValue(element, name)) { model.loadSettings(element, name); }
}

} // namespace


bool MixerRoute::delivers(bool compensate) const
{
	return enabled() && (m_from->m_hasInput || m_from->m_stillRunning || compensate
		|| (feedback() && !feedbackCommittedQuiet()));
}

MixerRoute::MixerRoute( MixerChannel * from, MixerChannel * to, float amount,
			bool preFader ) :
	m_from( from ),
	m_to( to ),
	m_amount(amount, 0, 1, 0.001f, nullptr,
			tr("Amount to send from channel %1 to channel %2").arg(m_from->index()).arg(m_to->index())),
	m_pan(0.f, -1.f, 1.f, 0.01f, nullptr,
			tr("Pan of the send from channel %1 to channel %2").arg(m_from->index()).arg(m_to->index())),
	m_preFader( preFader )
{
	//qDebug( "created: %d to %d", m_from->m_channelIndex, m_to->m_channelIndex );
	// create send amount model
	// PDC (#605): preallocate the per-edge delay history on the control thread.
	m_compensation.init(Engine::audioEngine()->framesPerPeriod());
}


void MixerRoute::updateName()
{
	m_amount.setDisplayName(
			tr("Amount to send from channel %1 to channel %2").arg(m_from->index()).arg(m_to->index()));
	m_pan.setDisplayName(
			tr("Pan of the send from channel %1 to channel %2").arg(m_from->index()).arg(m_to->index()));
}


// ---- the cycle-permitted submode (board card #709) -------------------------

void MixerRoute::setFeedback( bool on )
{
	if( m_feedback == on ) { return; }
	m_feedback = on;
	if( !on )
	{
		m_feedbackIntermediate.clear();
		m_feedbackCommitted.clear();
		return;
	}
	// Control thread only (the same rule as the constructor's delay-line
	// init): preallocate both snapshots so the audio path only ever copies.
	const f_cnt_t frames = Engine::audioEngine()->framesPerPeriod();
	m_feedbackIntermediate.assign( frames, SampleFrame{} );
	m_feedbackCommitted.assign( frames, SampleFrame{} );
	m_feedbackQuiet = true;
	m_feedbackCommittedQuiet = true;
}


void MixerRoute::writeFeedback( const SampleFrame* in, f_cnt_t frames )
{
	if( m_feedbackIntermediate.size() < frames )
	{
		return; // cannot happen: sized with framesPerPeriod like m_buffer
	}
	std::copy( in, in + frames, m_feedbackIntermediate.begin() );
	bool quiet = true;
	for( f_cnt_t f = 0; f < frames && quiet; ++f )
	{
		quiet = in[f][0] == 0.0f && in[f][1] == 0.0f;
	}
	m_feedbackQuiet = quiet;
}


void MixerRoute::clearFeedback()
{
	for( auto& frame : m_feedbackIntermediate ) { frame = SampleFrame{}; }
	m_feedbackQuiet = true;
}


void MixerRoute::commitFeedback()
{
	if( m_feedbackCommitted.size() != m_feedbackIntermediate.size() )
	{
		m_feedbackCommitted = m_feedbackIntermediate;
	}
	else
	{
		std::copy( m_feedbackIntermediate.begin(), m_feedbackIntermediate.end(),
			m_feedbackCommitted.begin() );
	}
	m_feedbackCommittedQuiet = m_feedbackQuiet;
}


MixerSidechainRoute::MixerSidechainRoute( MixerChannel * from, MixerChannel * to,
			float amount, SidechainTapPoint mode, bool deferred ) :
	m_from( from ),
	m_to( to ),
	m_amount(amount, 0, 1, 0.001f, nullptr,
			tr("Sidechain amount from channel %1 to channel %2").arg(m_from->index()).arg(m_to->index())),
	m_mode( mode ),
	// one private intermediate buffer per (sender, receiver) pair: the sender
	// worker writes only this buffer, the receiver worker sums it post-hoc.
	m_intermediate( Engine::audioEngine()->framesPerPeriod(), 2 ),
	m_deferred( deferred ),
	// deferred routes deliver the previous period's tap; the snapshot is
	// pre-allocated here so the audio path only ever copies into it.
	m_committed( Engine::audioEngine()->framesPerPeriod(), 2 )
{
	m_intermediate.silenceAllChannels();
	m_committed.silenceAllChannels();
	// PDC (#605): preallocate the tap delay history on the control thread.
	m_compensation.init(Engine::audioEngine()->framesPerPeriod());
}


void MixerSidechainRoute::compensatedTap(bool deferred, const float** left,
		const float** right, f_cnt_t frames)
{
	AudioBuffer& tap = deferred ? m_committed : m_intermediate;
	if (m_compensation.delayFrames() > 0)
	{
		m_compensation.processPlanar(tap.buffer(0).data(), tap.buffer(1).data(), frames);
	}
	*left = tap.buffer(0).data();
	*right = tap.buffer(1).data();
}


void MixerSidechainRoute::clearIntermediate()
{
	m_intermediate.silenceAllChannels();
}


void MixerSidechainRoute::commitIntermediate()
{
	for( ch_cnt_t ch = 0; ch < 2; ++ch )
	{
		auto src = m_intermediate.buffer( ch );
		auto dst = m_committed.buffer( ch );
		std::copy( src.begin(), src.end(), dst.begin() );
	}
	m_committed.updateSilenceFlags( 0b11 );
}


void MixerSidechainRoute::updateName()
{
	m_amount.setDisplayName(
			tr("Sidechain amount from channel %1 to channel %2").arg(m_from->index()).arg(m_to->index()));
}



MixerChannel::MixerChannel( int idx, Model * _parent ) :
	m_fxChain( nullptr ),
	m_rack( &m_fxChain ),
	m_stillRunning( false ),
	m_peakLeft( 0.0f ),
	m_peakRight( 0.0f ),
	m_buffer( new SampleFrame[Engine::audioEngine()->framesPerPeriod()] ),
	m_bus( &m_buffer, 1, Engine::audioEngine()->framesPerPeriod() ),
	m_hasInput( false ),
	m_muteBeforeSolo( false ),
	m_muteModel( false, _parent ),
	m_soloModel( false, _parent ),
	m_volumeModel(1.f, 0.f, 2.f, 0.001f, _parent),
	m_panModel(0.f, -1.f, 1.f, 0.001f, _parent),
	m_name(),
	m_queued( false ),
	// D1 (mixer concurrency audit): initialise the mute latch explicitly. It
	// used to be left indeterminate until the first masterMix().
	m_muted( false ),
	m_sidechainBuffer( Engine::audioEngine()->framesPerPeriod(), 2 ),
	m_postFaderBuffer( Engine::audioEngine()->framesPerPeriod(), 2 ),
	m_sidechainSends(),
	m_sidechainReceives(),
	m_isBus( false ),
	m_dependenciesMet(0),
	m_channelIndex(idx),
	// The stable id, handed out once, here, at creation (SPEC-stable-ids.md
	// 2.1, slice 2). It is the number in ch-<n> and it never changes while the
	// channel lives, so a client that was told ch-7 keeps ch-7 when a sibling
	// channel is added, removed or moved - unlike the channel's index in the
	// mixer, which is what ch-<n> used to be derived from. Mixer::loadSettings
	// overrides it with the file's value on a project load, and a file element
	// with no id keeps this one, which is what makes legacy assignment
	// deterministic (the load walks the <mixerchannel> elements in document
	// order, and each one's `num` allocates the channels up to it). The
	// counter is the project-scoped ProjectIds, shared with the track, clip,
	// note and effect ids.
	m_id( ProjectIds::allocate() )
{
	m_bus.silenceAllChannels();
	m_sidechainBuffer.silenceAllChannels();
	m_postFaderBuffer.silenceAllChannels();
	m_surround.init( Engine::audioEngine()->framesPerPeriod() );  // 0.5.0 F7: the side buffer, once

	// cluster new-p10-249: one input partial per render thread (plus one for a
	// caller outside a render job), sized here on the control thread so the
	// worker-fed accumulation never allocates on the audio path. A render thread
	// writes only its own slot; Mixer::reduceInputPartials() reduces the slots
	// at the period barrier.
	m_inputPartials.resize( static_cast<std::size_t>(Engine::audioEngine()->renderThreadCount()) + 1 );
	for( MixerChannel::InputPartial & partial : m_inputPartials )
	{
		partial.buffer.assign( Engine::audioEngine()->framesPerPeriod(), SampleFrame{} );
		partial.quiet.set();
	}
}




MixerChannel::~MixerChannel()
{
	// m_loudnessOwner (Q050-108) goes with the channel: a channel is deleted while no period is processed.
	delete[] m_buffer;
}

/*! Replace the id with \a id, and raise the project counter above it so the
 *  number can never be handed out again (SPEC-stable-ids.md rule R3).
 *
 *  A negative value is ignored: it is not a number this surface can address,
 *  and the constructor's id is always a valid answer. Mixer::loadSettings is
 *  the only caller - the file's `id` attribute, when it parses to a
 *  non-negative int.
 */
void MixerChannel::setId( int id )
{
	if( id < 0 )
	{
		return;
	}
	m_id = id;
	// Never let the counter hand this number to anything else, even if the
	// file that carried it had no (or a stale) next-id.
	ProjectIds::observe( id );
}


inline void MixerChannel::processed()
{
	// D1 (mixer concurrency audit): the latch is read by workers, so it is
	// loaded atomically. Relaxed is enough: the value published for this
	// period reaches a worker through the job queue's release/acquire pair
	// (see m_muted in include/Mixer.h).
	for( const MixerRoute * receiverRoute : m_sends )
	{
		// board card #709: a cycle-permitted (feedback) send does NOT gate its
		// receiver - waiting on it inside the loop it closes would deadlock
		// the dependency count, the same reason a deferred sidechain send is
		// skipped below. Its audio arrives as the receiver's own
		// previous-period snapshot pull instead (spec 5.2 generalized).
		if( receiverRoute->feedback() )
		{
			continue;
		}
		if( receiverRoute->receiver()->m_muted.load(std::memory_order_relaxed) == false )
		{
			receiverRoute->receiver()->incrementDeps();
		}
	}
	// Phase D: a sidechain send is also a scheduling edge. The receiver must
	// run after the sender has written the sender's private intermediate
	// buffer, otherwise the post-hoc sum would read a partial or stale tap.
	// Deferred routes are the exception: they close a cycle through a regular
	// send, so they must not gate (spec 5.2) and are skipped here.
	for( const MixerSidechainRoute * receiverRoute : m_sidechainSends )
	{
		if( receiverRoute->deferred() )
		{
			continue;
		}
		if( receiverRoute->receiver()->m_muted.load(std::memory_order_relaxed) == false )
		{
			receiverRoute->receiver()->incrementDeps();
		}
	}
}

int MixerChannel::gatingSidechainReceives() const
{
	int count = 0;
	for( const MixerSidechainRoute * route : m_sidechainReceives )
	{
		if( ! route->deferred() )
		{
			++count;
		}
	}
	return count;
}

std::size_t MixerChannel::gatingReceives() const
{
	std::size_t count = 0;
	for( const MixerRoute * route : m_receives )
	{
		// board card #709: only sends that can actually gate count - a
		// loop-closing (feedback) send never increments this channel either
		// (processed() skips it), so counting it here would leave the channel
		// waiting for a delivery that never comes.
		if( ! route->feedback() )
		{
			++count;
		}
	}
	return count;
}

void MixerChannel::incrementDeps()
{
	const auto i = m_dependenciesMet++ + 1;
	if( i >= gatingReceives() + gatingSidechainReceives() && ! m_queued )
	{
		m_queued = true;
		AudioEngineWorkerThread::addJob( this );
	}
}

void MixerChannel::unmuteForSolo()
{
	// Two visited sets, one per walk direction. A channel reached by the
	// sender walk is not yet expanded by the receiver walk (and the reverse),
	// so one shared set would hide it from the walk that must still follow it.
	std::set<MixerChannel*> senderVisited;
	std::set<MixerChannel*> receiverVisited;
	m_muteModel.setValue(false);

	// if channel is not master, unmute also every channel it sends to/receives from
	if (!isMaster())
	{
		for (const MixerRoute* sendsRoute : m_sends)
		{
			sendsRoute->receiver()->unmuteSenderForSolo(senderVisited, receiverVisited);
		}

		for (const MixerRoute* receiverRoute : m_receives)
		{
			receiverRoute->sender()->unmuteReceiverForSolo(senderVisited, receiverVisited);
		}
	}
}

void MixerChannel::unmuteSenderForSolo()
{
	std::set<MixerChannel*> senderVisited;
	std::set<MixerChannel*> receiverVisited;
	unmuteSenderForSolo(senderVisited, receiverVisited);
}

void MixerChannel::unmuteSenderForSolo(std::set<MixerChannel*>& senderVisited,
	std::set<MixerChannel*>& receiverVisited)
{
	if (!senderVisited.insert(this).second) { return; }
	m_muteModel.setValue(false);

	// if channel is not master, unmute every channel it sends to
	if (!isMaster())
	{
		for (const MixerRoute* sendsRoute : m_sends)
		{
			sendsRoute->receiver()->unmuteSenderForSolo(senderVisited, receiverVisited);
		}
	}
}


void MixerChannel::unmuteReceiverForSolo()
{
	std::set<MixerChannel*> senderVisited;
	std::set<MixerChannel*> receiverVisited;
	unmuteReceiverForSolo(senderVisited, receiverVisited);
}

void MixerChannel::unmuteReceiverForSolo(std::set<MixerChannel*>& senderVisited,
	std::set<MixerChannel*>& receiverVisited)
{
	if (!receiverVisited.insert(this).second) { return; }
	m_muteModel.setValue(false);

	// if channel is not master, unmute every channel it receives from, and of those, unmute the channels they send to
	if (!isMaster())
	{
		for (const MixerRoute* receiverRoute : m_receives)
		{
			receiverRoute->sender()->unmuteReceiverForSolo(senderVisited, receiverVisited);
		}

		for (const MixerRoute* sendsRoute : m_sends)
		{
			sendsRoute->receiver()->unmuteSenderForSolo(senderVisited, receiverVisited);
		}
	}
}



void MixerChannel::sumSidechainInputs(const f_cnt_t fpp)
{
	if( m_sidechainReceives.empty() )
	{
		return;
	}

	// Post-hoc sum of the per-sender intermediates (spec 5.4 strategy (a)).
	// Each intermediate is consumed (cleared) here, so a muted or otherwise
	// silent sender contributes silence instead of a stale tap from the
	// previous period. No allocation, no locking: all buffers are
	// pre-allocated when the route is created on the control thread.
	float* const dst0 = m_sidechainBuffer.buffer(0).data();
	float* const dst1 = m_sidechainBuffer.buffer(1).data();
	for( f_cnt_t f = 0; f < fpp; ++f )
	{
		dst0[f] = 0.0f;
		dst1[f] = 0.0f;
	}

	for( MixerSidechainRoute * route : m_sidechainReceives )
	{
		// Deferred routes do not gate their receiver, so the current
		// intermediate may not have been written yet this period: read the
		// snapshot committed in prepareMasterMix() instead (one period late).
		const bool postNoGain = route->mode() == SidechainTapPoint::PostFaderNoGain;
		// Luna cluster 5156: an automated amount carries sample-exact data,
		// exactly like a regular send; read it per frame instead of collapsing
		// the curve to one scalar for the whole period.
		// review f1a74cc54 (cluster 2647): this receiver's job is the only reader of the route's amount,
		// so the sole-reader form takes no mutex. A linked amount still takes the linked model's mutex.
		ValueBuffer* const amountBuf = postNoGain ? nullptr : route->amount()->valueBuffer();
		const float amount = postNoGain ? 1.0f : route->amount()->value();
		// PDC (#605): delay the tap to this channel's alignment point. The
		// route owns the tap buffer, so the delay is applied in place.
		const float* s0 = nullptr;
		const float* s1 = nullptr;
		route->compensatedTap(route->deferred(), &s0, &s1, fpp);
		if( amountBuf )
		{
			for( f_cnt_t f = 0; f < fpp; ++f )
			{
				const float a = amountBuf->value( f );
				dst0[f] += s0[f] * a;
				dst1[f] += s1[f] * a;
			}
		}
		else if( amount == 1.0f )
		{
			for( f_cnt_t f = 0; f < fpp; ++f )
			{
				dst0[f] += s0[f];
				dst1[f] += s1[f];
			}
		}
		else
		{
			for( f_cnt_t f = 0; f < fpp; ++f )
			{
				dst0[f] += s0[f] * amount;
				dst1[f] += s1[f] * amount;
			}
		}
		if( ! route->deferred() )
		{
			// deferred routes are consumed by the commit instead
			route->clearIntermediate();
		}
	}
	m_sidechainBuffer.updateSilenceFlags(0b11);
}




void MixerChannel::updatePostFaderBuffer(const float volume, const f_cnt_t fpp)
{
	// D1: the channel volume multiply happens here, after the send loop, and
	// produces a separate snapshot. m_buffer keeps carrying the post-FX,
	// pre-fader signal so that pre-fader sends and taps stay independent of
	// the fader. The per-sample expression is identical to the one the legacy
	// post-fader receive path applies, so post-fader output is unchanged.
	float* const dst0 = m_postFaderBuffer.buffer(0).data();
	float* const dst1 = m_postFaderBuffer.buffer(1).data();
	ValueBuffer * volBuf = m_volumeModel.valueBuffer();
	if( volBuf )
	{
		const float* const values = volBuf->values();
		for( f_cnt_t f = 0; f < fpp; ++f )
		{
			dst0[f] = m_buffer[f][0] * values[f];
			dst1[f] = m_buffer[f][1] * values[f];
		}
	}
	else
	{
		for( f_cnt_t f = 0; f < fpp; ++f )
		{
			dst0[f] = m_buffer[f][0] * volume;
			dst1[f] = m_buffer[f][1] * volume;
		}
	}
	m_postFaderBuffer.updateSilenceFlags(0b11);
}




void MixerChannel::writeSidechainTaps(const SidechainTapPoint point,
					const f_cnt_t fpp)
{
	if( m_sidechainSends.empty() )
	{
		return;
	}

	for( MixerSidechainRoute * route : m_sidechainSends )
	{
		if( route->mode() != point )
		{
			continue;
		}
		AudioBuffer& dst = route->intermediate();
		float* const d0 = dst.buffer(0).data();
		float* const d1 = dst.buffer(1).data();
		if( point == SidechainTapPoint::PostFader
			|| point == SidechainTapPoint::PostFaderNoGain )
		{
			const float* const p0 = m_postFaderBuffer.buffer(0).data();
			const float* const p1 = m_postFaderBuffer.buffer(1).data();
			for( f_cnt_t f = 0; f < fpp; ++f )
			{
				d0[f] = p0[f];
				d1[f] = p1[f];
			}
		}
		else
		{
			// pre-fx and pre-fader both tap the post-FX, pre-volume buffer;
			// pre-fx is written before the FX chain runs, pre-fader after.
			for( f_cnt_t f = 0; f < fpp; ++f )
			{
				d0[f] = m_buffer[f][0];
				d1[f] = m_buffer[f][1];
			}
		}
		dst.assumeNonSilent(0);
		dst.assumeNonSilent(1);
	}
}




void MixerChannel::doProcessing()
{
	const f_cnt_t fpp = Engine::audioEngine()->framesPerPeriod();

	if( m_muted.load(std::memory_order_relaxed) == false )
	{
		for( MixerRoute * senderRoute : m_receives )
		{
			MixerChannel * sender = senderRoute->sender();
			FloatModel * sendModel = senderRoute->amount();
			if( ! sendModel ) qFatal( "Error: no send model found from %d to %d", senderRoute->senderIndex(), m_channelIndex );

			// PDC (#605): a compensated route must deliver its delayed block
			// even while the sender is momentarily silent, because the delay
			// line can still hold the tail of the signal.
			const bool compensate = senderRoute->compensationFrames() > 0;
			// board card #709: a cycle-permitted (feedback) send delivers this
			// sender's PREVIOUS period snapshot (committed by prepareMasterMix
			// while no worker ran - the receiver worker and the sender worker
			// never touch the same buffer), and the one-period delay is the
			// loop's inherent, uncompensatable offset. Pull it while the
			// committed snapshot may still hold signal, so a decaying loop is
			// not truncated - the compensate rule, for the snapshot's own
			// quiet flag.
			const bool feedback = senderRoute->feedback();
			if( senderRoute->delivers( compensate ) )
			{
				// figure out if we're getting sample-exact input
				ValueBuffer * sendBuf = sendModel->valueBuffer();
				// 0.5.0 MX-082: a sender whose post-fader inserts baked its fader is read at unity.
				ValueBuffer * volBuf = postinserts::faderBuffer( *sender, senderRoute );

				// Delay the sender's block to this channel's alignment point.
				// A zero delay returns the sender's buffer unchanged. A
				// feedback send never uses the delay line at all: its source
				// is the committed snapshot (compensation is suspended for
				// the loop, #709).
				const SampleFrame* ch_buf = feedback
					? senderRoute->feedbackCommitted()
					: senderRoute->compensatedBuffer(postinserts::sendSource(*sender, *senderRoute), fpp);

				if( sendIsPanned( *senderRoute ) )
				{
					// 0.5.0 L09 (MX-048): a send with its own pan (MixerSendPan.cpp),
					// sample-exact since 0.5.0 W4 (a moving pan is picked even when it
					// ends the block at centre). A centred send - every send of every
					// older project - takes the branches below unchanged.
					addPannedSend( m_buffer, ch_buf, *senderRoute, *sender, fpp );
				}
				else if( senderRoute->preFader() )
				{
					// Phase D pre-fader send: the sender's post-FX, pre-volume
					// signal scaled only by the send amount. The sender fader
					// does not affect a bus's input (spec 5.5).
					if( ! sendBuf )
					{
						const float v = sendModel->value();
						MixHelpers::addMultiplied( m_buffer, ch_buf, v, fpp );
					}
					else
					{
						MixHelpers::addMultipliedByBuffer( m_buffer, ch_buf, 1.0f, sendBuf, fpp );
					}
				}
				// use sample-exact mixing if sample-exact values are available
				else if( ! volBuf && ! sendBuf ) // neither volume nor send has sample-exact data...
				{
					const float v = postinserts::faderValue( *sender, senderRoute ) * sendModel->value();
					MixHelpers::addMultiplied( m_buffer, ch_buf, v, fpp );
				}
				else if( volBuf && sendBuf ) // both volume and send have sample-exact data
				{
					MixHelpers::addMultipliedByBuffers( m_buffer, ch_buf, volBuf, sendBuf, fpp );
				}
				else if( volBuf ) // volume has sample-exact data but send does not
				{
					const float v = sendModel->value();
					MixHelpers::addMultipliedByBuffer( m_buffer, ch_buf, v, volBuf, fpp );
				}
				else // vice versa
				{
					const float v = postinserts::faderValue( *sender, senderRoute );
					MixHelpers::addMultipliedByBuffer( m_buffer, ch_buf, v, sendBuf, fpp );
				}
				// 0.5.0 F7 (MX-060): a surround sender's other speakers, carried or folded (a no-op for stereo).
				surround::addSideChannels( *senderRoute, fpp );
				if( ! compensate && ! feedback )
				{
					m_bus.quietChannels() &= sender->m_bus.quietChannels(); // mix silence status
				}
				else
				{
					// A delayed block's silence is not described by the
					// sender's current flags, so keep this channel awake: the
					// delay line may be handing us the tail of a signal the
					// sender has already stopped reporting, and a sleeping
					// effect downstream would discard it as "no input" if the
					// flags still said quiet (1 = quiet, so clear this bus's
					// pair). Inverting this - skipping the merge without
					// clearing - silences the channel (#605 audit C2). A
					// feedback snapshot is the same story (#709): it is one
					// period old, so the sender's current flags describe a
					// different block.
					m_bus.quietChannels().reset(0);
					m_bus.quietChannels().reset(1);
				}
				m_hasInput = true;
			}
			else if( compensate && ! senderRoute->enabled() )
			{
				// 0.5.0 L08 (review W3-batchA #3): a switched-off send's delay line keeps time, so switching it
				// back on does not replay what it held when it went off - as a muted receiver's lines do.
				senderRoute->advanceSilence( fpp );
			}
		}

		// 0.5.0 L08: the strip's trim, before the inserts (MixerStrip.cpp).
		mixerstrip::applyInput( *this, fpp );

		// Phase D: pre-FX sidechain tap, taken from the raw channel buffer
		// before the FX chain runs (spec 4.3).
		writeSidechainTaps(SidechainTapPoint::PreFx, fpp);
		recordertaps::channelAt(*this, SidechainTapPoint::PreFx, fpp);  // 0.5.0 WF-086: a record route tapping here

		// Phase D: fold the per-sender sidechain intermediates of this period
		// into m_sidechainBuffer before our own FX chain reads it.
		sumSidechainInputs(fpp);

		// 0.5.0 W3 (WF-104): time the devices and the strip stages below, to the end of
		// this block (two clock reads and one relaxed store; DspLoadProbe.h).
		const DspLoadProbe dspLoad( m_dspLoad, fpp, Engine::audioEngine()->baseSampleRate() );

		// #599: a channel with a rack (two or more chains) renders this period
		// through the rack's RoutingGraph; with no rack configured the
		// channel's own chain is the whole signal path, exactly as before.
		if( m_rack.canProcessThroughRack( m_bus ) )
		{
			m_stillRunning = m_sidechainReceives.empty()
				? m_rack.processAudioBuffer( m_bus )
				: m_rack.processAudioBuffer( m_bus, &m_sidechainBuffer );
		}
		else
		{
			m_stillRunning = m_sidechainReceives.empty()
				? m_fxChain.processAudioBuffer(m_bus)
				: m_fxChain.processAudioBuffer(m_bus, &m_sidechainBuffer);
		}

		// 0.5.0 F1 (MX-061): the hardware insert, after the devices and before the fader (a no-op while off).
		hardwareio::processInsert( *this, fpp );

		// VCA / mix group (#622). The group gain is applied here, as one extra
		// factor on the post-FX buffer, and nowhere else: m_buffer is what the
		// fader snapshot, the peak meter, the post-/pre-fader sidechain taps
		// and every receiving channel all read, so a single multiply scales the
		// whole of the member's output while the member's own volume model is
		// never written. That is what makes "move the VCA back" restore the
		// members bit-exactly.
		//
		// The guard is not only an optimisation: with gain == 1.0f (no group, or
		// a group at unity) a channel takes the pre-#622 arithmetic path
		// unchanged, so an ungrouped project renders byte-identically.
		const float vcaGain = m_vcaGain.load(std::memory_order_relaxed);
		if (vcaGain != 1.0f)
		{
			for (f_cnt_t f = 0; f < fpp; ++f)
			{
				m_buffer[f][0] *= vcaGain;
				m_buffer[f][1] *= vcaGain;
			}
			// Deliberately no quiet-flag update: the bus flags are an
			// optimisation the DSP trusts, and claiming silence we do not have
			// loses audio, while claiming audio we do not have only wastes
			// work. A silent group is silent either way.
		}

		// Channel pan (m_panModel, a balance law). Applied HERE for the reason the
		// VCA gain is: m_buffer is what the fader snapshot, the meter, the taps
		// and every receiver read, so one multiply pans the channel's whole
		// output, and pre-fader sends carry it too (pan is part of the channel's
		// insert stage, not of the fader). Centred and unautomated - every
		// channel in every project written before this - skips the stage, so the
		// arithmetic is the pre-pan arithmetic, bit for bit. No allocation, no
		// lock: the model's value and its sample-exact buffer are the same reads
		// the volume fader makes on this thread.
		// 0.5.0 L08: polarity and mono, then the pan in its mode (balance falls through to the stage below).
		mixerstrip::applyOutput( *this, fpp );
		const bool panned = mixerstrip::applyPanMode( *this, fpp );
		const ValueBuffer* panBuf = m_panModel.valueBuffer();
		const float pan = m_panModel.value();
		if (!panned && (panBuf != nullptr || pan != 0.0f))
		{
			for (f_cnt_t f = 0; f < fpp; ++f)
			{
				const float p = panBuf != nullptr ? panBuf->values()[f] : pan;
				m_buffer[f][0] *= std::min(1.0f, 1.0f - p);
				m_buffer[f][1] *= std::min(1.0f, 1.0f + p);
			}
		}

		// 0.5.0 F7 (MX-060): the side channels' trim, VCA gain and balance (a no-op for stereo).
		surround::processChannel( *this, fpp );

		// D1: the volume multiply happens after the send loop, producing the
		// post-fader snapshot used by post-fader sidechain taps.
		// board card #709: publish this period's block to every outgoing
		// cycle-permitted send. The receiver reads only the copy
		// prepareMasterMix() commits before the NEXT period starts, so the
		// two workers never touch this buffer (the sidechain-intermediate
		// rule, applied to feedback).
		for( MixerRoute * senderRoute : m_sends )
		{
			if( senderRoute->feedback() )
			{
				senderRoute->writeFeedback( m_buffer, fpp );
			}
		}

		const float v = m_volumeModel.value();
		updatePostFaderBuffer(v, fpp);

		// Phase D: post-FX sidechain taps (pre-fader = post-FX/pre-volume,
		// post-fader = post-volume).
		writeSidechainTaps(SidechainTapPoint::PreFader, fpp);
		writeSidechainTaps(SidechainTapPoint::PostFader, fpp);
		recordertaps::channelAt(*this, SidechainTapPoint::PreFader, fpp);  // 0.5.0 WF-086
		recordertaps::channelAt(*this, SidechainTapPoint::PostFader, fpp);
		writeSidechainTaps(SidechainTapPoint::PostFaderNoGain, fpp);

		// 0.5.0 MX-082: the post-fader inserts (skipped while empty: the meters then apply v as before).
		const float meterGain = m_postInserts.process( *this, v, fpp );

		SampleFrame peakSamples = getAbsPeakValues(m_buffer, fpp);
		m_peakLeft = std::max(m_peakLeft, peakSamples[0] * meterGain);
		m_peakRight = std::max(m_peakRight, peakSamples[1] * meterGain);
		m_speechPeakLeft.set(peakSamples[0] * meterGain);
		m_speechPeakRight.set(peakSamples[1] * meterGain);
		mixerstrip::updateMeters( *this, m_buffer, meterGain, fpp );  // 0.5.0 L08
	}
	// A muted channel never reaches this function: Mixer::masterMix handles
	// muted channels directly (dependency bookkeeping plus the incoming delay
	// lines' silence advance, #605 audit C7). The guard above stays as the
	// invariant; the silence branch that used to live here was unreachable.

	// increment dependency counter of all receivers
	processed();
}

#ifdef LMMS_TESTING
namespace
{

//! Test seam (cluster new-p14-29): the mixerChannel() lookups that named no channel.
int refusedLookups = 0;

} // namespace

MixerChannel* Mixer::refusedLookupForTesting()
{
	++refusedLookups;
	return nullptr;
}

int Mixer::takeRefusedLookupsForTesting()
{
	const int count = refusedLookups;
	refusedLookups = 0;
	return count;
}
#endif

Mixer::Mixer() :
	Model( nullptr ),
	JournallingObject(),
	m_mixerChannels(),
	m_lastSoloed(-1)
{
	// create master channel
	createChannel();
}



Mixer::~Mixer()
{
	// VCA groups (#622) reference channel indices; drop them first.
	for (VcaGroup* group : m_vcaGroups)
	{
		group->detach();
		delete group;
	}
	m_vcaGroups.clear();
	while (!m_mixerSidechainRoutes.empty())
	{
		deleteSidechainSend(m_mixerSidechainRoutes.front());
	}
	while (!m_mixerRoutes.empty())
	{
		deleteChannelSend(m_mixerRoutes.front());
	}
	while( m_mixerChannels.size() )
	{
		MixerChannel * f = m_mixerChannels[m_mixerChannels.size() - 1];
		m_mixerChannels.pop_back();
		delete f;
	}
}



int Mixer::createChannel()
{
	// D3 (mixer concurrency audit): growing m_mixerChannels reallocates the
	// container the render thread iterates (masterMix, mixToChannel), and the
	// latency scratch follows it (resizeLatencyScratch -> updateLatencyCompensation,
	// resolveLatency). Hold the same change mutex every other topology writer in
	// this file holds, so the period boundary is the only place the containers
	// can change. The mutex is recursive, so the nested requestChangeInModel()
	// inside clearChannel() -> createChannelSend() -> createRoute() is fine.
	Engine::audioEngine()->requestChangeInModel();

	const int index = m_mixerChannels.size();
	// create new channel
	m_mixerChannels.push_back( new MixerChannel( index, this ) );
	// 0.5.0 L08 (review L05-L09-L08-1 #1): the engine applies a solo itself - it used to be applied
	// only through MixerView's connection, so a headless or socket-only instance never muted anything.
	connect(&m_mixerChannels.back()->m_soloModel, &BoolModel::dataChanged, this, &Mixer::toggledSolo, Qt::DirectConnection);
	// PDC (#605): the latency scratch follows the channel count.
	resizeLatencyScratch( m_mixerChannels.size() );

	// reset channel state
	clearChannel( index );

	// if there is a soloed channel, mute the new track
	if (m_lastSoloed != -1 && m_mixerChannels[m_lastSoloed]->m_soloModel.value())
	{
		m_mixerChannels[index]->m_muteBeforeSolo = m_mixerChannels[index]->m_muteModel.value();
		m_mixerChannels[index]->m_muteModel.setValue(true);
	}

	Engine::audioEngine()->doneChangeInModel();

	return index;
}

void Mixer::activateSolo()
{
	for (auto i = std::size_t{1}; i < m_mixerChannels.size(); ++i)
	{
		m_mixerChannels[i]->m_muteBeforeSolo = m_mixerChannels[i]->m_muteModel.value();
		// 0.5.0 L08 (MX-031): a solo-safe channel keeps playing through another's solo.
		if (!m_mixerChannels[i]->m_soloSafeModel.value()) { m_mixerChannels[i]->m_muteModel.setValue( true ); }
	}
}

void Mixer::deactivateSolo()
{
	for (auto i = std::size_t{1}; i < m_mixerChannels.size(); ++i)
	{
		m_mixerChannels[i]->m_muteModel.setValue( m_mixerChannels[i]->m_muteBeforeSolo );
	}
}

void Mixer::toggledSolo()
{
	int soloedChan = -1;
	bool resetSolo = m_lastSoloed != -1;
	// Luna cluster 1523: a channel solo now owns the mute state.
	m_soloingGroup = nullptr;
	// 0.5.0 L08 (MX-029): additive solo keeps the earlier solos (MixerStrip.cpp).
	if (mixerstrip::additiveSolo()) { applyAdditiveSolo(); return; }
	//untoggle if lastsoloed is entered
	if (resetSolo)
	{
		m_mixerChannels[m_lastSoloed]->m_soloModel.setValue( false );
	}
	//determine the soloed channel
	for (auto i = std::size_t{0}; i < m_mixerChannels.size(); ++i)
	{
		if (m_mixerChannels[i]->m_soloModel.value() == true)
			soloedChan = i;
	}
	// if no channel is soloed, unmute everything, else mute everything
	if (soloedChan != -1)
	{
		if (resetSolo)
		{
			deactivateSolo();
			activateSolo();
		} else {
			activateSolo();
		}
		// unmute the soloed chan and every channel it sends to/receives from
		// #622: soloing a member soloes its whole VCA group, so the group is
		// audible as a unit (the member -> group half of the solo link).
		VcaGroup* group = vcaGroupForChannel( static_cast<mix_ch_t>(soloedChan) );
		if (group != nullptr)
		{
			for (mix_ch_t member : group->members())
			{
				if (member < m_mixerChannels.size())
				{
					m_mixerChannels[member]->unmuteForSolo();
				}
			}
		}
		else
		{
			m_mixerChannels[soloedChan]->unmuteForSolo();
		}
	} else {
		deactivateSolo();
	}
	m_lastSoloed = soloedChan;
}



// ---- VCA / mix-and-edit groups (#622) --------------------------------------

VcaGroup* Mixer::createVcaGroup(const QString& name, int id)
{
	if (id < 0)
	{
		// Lowest unused id, so ids stay small and a round trip through the
		// project file re-creates the same ones.
		id = 0;
		while (vcaGroup(id) != nullptr)
		{
			++id;
		}
	}
	if (vcaGroup(id) != nullptr)
	{
		return nullptr;
	}

	auto group = new VcaGroup(this, id, name.isEmpty() ? tr("VCA %1").arg(id) : name);
	m_vcaGroups.push_back(group);
	return group;
}



VcaGroup* Mixer::vcaGroup(int id) const
{
	for (VcaGroup* group : m_vcaGroups)
	{
		if (group->id() == id)
		{
			return group;
		}
	}
	return nullptr;
}



VcaGroup* Mixer::vcaGroupForChannel(mix_ch_t channel) const
{
	for (VcaGroup* group : m_vcaGroups)
	{
		if (group->contains(channel))
		{
			return group;
		}
	}
	return nullptr;
}



bool Mixer::deleteVcaGroup(int id)
{
	for (auto it = m_vcaGroups.begin(); it != m_vcaGroups.end(); ++it)
	{
		VcaGroup* group = *it;
		if (group->id() != id)
		{
			continue;
		}
		m_vcaGroups.erase(it);
		group->detach();
		delete group;
		// The members are no longer grouped, so they play at unity again.
		refreshGroups();
		return true;
	}
	return false;
}



void Mixer::clearVcaGroups()
{
	for (VcaGroup* group : m_vcaGroups)
	{
		group->detach();
		delete group;
	}
	m_vcaGroups.clear();
	refreshGroups();
}



void Mixer::refreshGroups()
{
	// A channel that is in no group, or that has just left one, publishes
	// unity - which is also what makes an old project (no groups at all) load
	// into exactly the pre-#622 playback state. Each channel's gain is decided
	// first and stored ONCE: resetting every channel to unity and then
	// re-applying let the audio thread read 1.0 for a grouped channel between
	// the two stores (a VCA at -20 dB playing a block at 0 dB). The later group
	// wins, as it did when groups were applied in order.
	// (A member id is a mix_ch_t, so a channel past its range is in no group.)
	// 0.5.0 L09 (MX-070): a channel in several groups takes the product of their gains, and each
	// group's gain is scaled by the groups it sits in (vcaGroupGain, MixerGroups.cpp).
	for (std::size_t ch = 0; ch < m_mixerChannels.size(); ++ch)
	{
		float gain = 1.0f;
		for (const VcaGroup* group : m_vcaGroups)
		{
			if (ch <= std::numeric_limits<mix_ch_t>::max() && group->contains(static_cast<mix_ch_t>(ch))) { gain *= vcaGroupGain(*this, *group); }
		}
		m_mixerChannels[ch]->setVcaGain(gain);
	}
}



void Mixer::applyGroupSolo(VcaGroup* group, bool soloed)
{
	if (group == nullptr)
	{
		return;
	}
	if (!soloed)
	{
		// Luna cluster 1523: clearing a solo must run BEFORE the link check
		// below. A group that was soloed while its link was on has muted every
		// non-member; if the link is then turned off (or the group suspended),
		// the early return used to skip deactivateSolo() and those mutes were
		// never restored. Only the group that muted may restore, so clearing a
		// group that never soloed cannot unmute a channel solo in progress.
		if (m_soloingGroup != group) { return; }
		m_soloingGroup = nullptr;
		deactivateSolo();
		m_lastSoloed = -1;
		return;
	}
	// 0.5.0 L09 (MX-071): a group whose solo link is off - or that is suspended - does not solo its members.
	if (!group->linksSolo() || group->isSuspended())
	{
		return;
	}

	// Exclusive, exactly like the channel solo: soloing one group clears any
	// other group's flag (its handler deactivates first), so the pre-solo mute
	// state that deactivateSolo() restores is never overwritten before it is
	// read back.
	for (VcaGroup* other : m_vcaGroups)
	{
		if (other != group && other->soloModel()->value())
		{
			other->soloModel()->setValue(false);
		}
	}

	activateSolo();
	for (mix_ch_t member : group->members())
	{
		if (member < m_mixerChannels.size())
		{
			m_mixerChannels[member]->unmuteForSolo();
		}
	}
	// No channel's own solo flag drove this, so toggledSolo() has nothing to
	// untoggle later.
	m_lastSoloed = -1;
	// Luna cluster 1523: this group owns the mute state until its flag clears.
	m_soloingGroup = group;
}



void Mixer::deleteChannel( int index )
{
	// channel deletion is performed between mixer rounds
	Engine::audioEngine()->requestChangeInModel();
	mixerlisten::clear();  // 0.5.0 L09: the indices move, so no listen flag survives

	// VCA groups (#622): drop the deleted channel and shift the members whose
	// index moves down, the same bookkeeping the tracks below get. Done before
	// the erase so the group members still describe the pre-deletion layout.
	for (VcaGroup* group : m_vcaGroups)
	{
		group->channelDeleted( static_cast<mix_ch_t>(index) );
	}

	// go through every instrument and adjust for the channel index change
	TrackContainer::TrackList tracks;

	auto& songTracks = Engine::getSong()->tracks();
	auto& patternStoreTracks = Engine::patternStore()->tracks();
	tracks.insert(tracks.end(), songTracks.begin(), songTracks.end());
	tracks.insert(tracks.end(), patternStoreTracks.begin(), patternStoreTracks.end());

	// A routing folder's OWN mixer channel is an index into the same vector the
	// track walk below renumbers, so the folders are told FIRST: one whose channel
	// was the deleted one gives its children back to their own channels before the
	// walk can send them to master, and the folders above it follow the index down
	// instead of keeping one that now names somebody else's strip.
	for( Track* t : tracks )
	{
		if( t->type() != Track::Type::Folder ) { continue; }
		dynamic_cast<TrackFolder*>( t )->channelDeleted( index );
	}

	for( Track* t : tracks )
	{
		if( t->type() == Track::Type::Instrument )
		{
			auto inst = dynamic_cast<InstrumentTrack*>(t);
			int val = inst->mixerChannelModel()->value(0);
			if( val == index )
			{
				// we are deleting this track's channel send
				// send to master
				inst->mixerChannelModel()->setValue(0);
			}
			else if( val > index )
			{
				// subtract 1 to make up for the missing channel
				inst->mixerChannelModel()->setValue(val-1);
			}
		}
		else if( t->type() == Track::Type::Sample )
		{
			auto strk = dynamic_cast<SampleTrack*>(t);
			int val = strk->mixerChannelModel()->value(0);
			if( val == index )
			{
				// we are deleting this track's channel send
				// send to master
				strk->mixerChannelModel()->setValue(0);
			}
			else if( val > index )
			{
				// subtract 1 to make up for the missing channel
				strk->mixerChannelModel()->setValue(val-1);
			}
		}
	}

	MixerChannel * ch = m_mixerChannels[index];

	// delete all of this channel's sends and receives
	while (!ch->m_sends.empty())
	{
		deleteChannelSend(ch->m_sends.front());
	}
	while (!ch->m_receives.empty())
	{
		deleteChannelSend(ch->m_receives.front());
	}
	// Phase D: sidechain sends/receives are routes too
	while (!ch->m_sidechainSends.empty())
	{
		deleteSidechainSend(ch->m_sidechainSends.front());
	}
	while (!ch->m_sidechainReceives.empty())
	{
		deleteSidechainSend(ch->m_sidechainReceives.front());
	}

	// if m_lastSoloed was our index, the solo goes with the channel: restore the mutes it hid
	// (activateSolo saved each channel's pre-solo state) before the channel is erased, or nothing
	// brings them back once m_lastSoloed no longer names a soloed channel.
	const bool soloRemoved = m_lastSoloed == index;
	if (soloRemoved)
	{
		deactivateSolo();
		m_lastSoloed = -1;
	}
	// if m_lastSoloed is > delete index, it will move left
	else if (m_lastSoloed > index) { --m_lastSoloed; }

	// actually delete the channel
	m_mixerChannels.erase(m_mixerChannels.begin() + index);
	delete ch;
	resizeLatencyScratch( m_mixerChannels.size() );
	// #622: the surviving members' indices changed; republish the gains.
	refreshGroups();

	for (auto i = static_cast<std::size_t>(index); i < m_mixerChannels.size(); ++i)
	{
		validateChannelName( i, i + 1 );

		// set correct channel index
		m_mixerChannels[i]->setIndex(i);

		// now check all routes and update names of the send models
		for( MixerRoute * r : m_mixerChannels[i]->m_sends )
		{
			r->updateName();
		}
		for( MixerRoute * r : m_mixerChannels[i]->m_receives )
		{
			r->updateName();
		}
		for( MixerSidechainRoute * r : m_mixerChannels[i]->m_sidechainSends )
		{
			r->updateName();
		}
		for( MixerSidechainRoute * r : m_mixerChannels[i]->m_sidechainReceives )
		{
			r->updateName();
		}
	}

	// Additive solo keeps the other soloed channels on: re-derive the mutes from them on the new layout
	// (after the renumbering, which applyAdditiveSolo reads through index()).
	if (soloRemoved && mixerstrip::additiveSolo()) { applyAdditiveSolo(); }

	Engine::audioEngine()->doneChangeInModel();
	if (!m_clearing) { emit channelDeleted(); }
}



void Mixer::moveChannelLeft( int index )
{
	// can't move master or first channel
	if (index <= 1 || static_cast<std::size_t>(index) >= m_mixerChannels.size())
	{
		return;
	}

	// D4 (mixer concurrency audit): the swap below and the renumbering that
	// follows it change state the render thread reads under its period mutex -
	// masterMix's latch loop walks m_mixerChannels, and updateLatencyCompensation
	// reaches the same order and each channel's index through
	// MixerRoute::senderIndex(). Take the change mutex for the whole operation,
	// as deleteChannel() does, so a reorder can only happen between periods.
	Engine::audioEngine()->requestChangeInModel();

	// channels to swap
	int a = index - 1, b = index;

	// check if m_lastSoloed is one of our swaps
	if (m_lastSoloed == a) { m_lastSoloed = b; }
	else if (m_lastSoloed == b) { m_lastSoloed = a; }

	// go through every instrument and adjust for the channel index change
	const TrackContainer::TrackList& songTrackList = Engine::getSong()->tracks();
	const TrackContainer::TrackList& patternTrackList = Engine::patternStore()->tracks();

	for (const auto& trackList : {songTrackList, patternTrackList})
	{
		for (const auto& track : trackList)
		{
			if (track->type() == Track::Type::Instrument)
			{
				auto inst = (InstrumentTrack*)track;
				int val = inst->mixerChannelModel()->value(0);
				if( val == a )
				{
					inst->mixerChannelModel()->setValue(b);
				}
				else if( val == b )
				{
					inst->mixerChannelModel()->setValue(a);
				}
			}
			else if (track->type() == Track::Type::Sample)
			{
				auto strk = (SampleTrack*)track;
				int val = strk->mixerChannelModel()->value(0);
				if( val == a )
				{
					strk->mixerChannelModel()->setValue(b);
				}
				else if( val == b )
				{
					strk->mixerChannelModel()->setValue(a);
				}
			}
		}
	}

	// Swap positions in array
	qSwap(m_mixerChannels[index], m_mixerChannels[index - 1]);

	// Update m_channelIndex of both channels
	m_mixerChannels[index]->setIndex(index);
	m_mixerChannels[index - 1]->setIndex(index - 1);

	// Send model labels include both endpoint indices, so refresh routes touching either moved channel.
	for (MixerRoute* route : m_mixerRoutes)
	{
		if (route->senderIndex() == static_cast<mix_ch_t>(a) || route->senderIndex() == static_cast<mix_ch_t>(b)
			|| route->receiverIndex() == static_cast<mix_ch_t>(a) || route->receiverIndex() == static_cast<mix_ch_t>(b))
		{
			route->updateName();
		}
	}
	for (MixerSidechainRoute* route : m_mixerSidechainRoutes)
	{
		if (route->senderIndex() == static_cast<mix_ch_t>(a) || route->senderIndex() == static_cast<mix_ch_t>(b)
			|| route->receiverIndex() == static_cast<mix_ch_t>(a) || route->receiverIndex() == static_cast<mix_ch_t>(b))
		{
			route->updateName();
		}
	}

	// #622: group membership follows the channels, not the slots.  Done inside
	// the same change (D4) as the swap it follows, so the render thread never
	// sees a channel order the groups do not describe.
	for (VcaGroup* group : m_vcaGroups)
	{
		group->channelsSwapped( static_cast<mix_ch_t>(a), static_cast<mix_ch_t>(b) );
	}
	refreshGroups();

	// 0.5.0 L09: listen flags are kept per index too, so they follow their channels (deleteChannel
	// clears them for the same reason) - otherwise the neighbour moved into the slot is heard.
	const bool listenedA = mixerlisten::listening( a );
	mixerlisten::setListening( a, mixerlisten::listening( b ) );
	mixerlisten::setListening( b, listenedA );

	Engine::audioEngine()->doneChangeInModel();
}



void Mixer::moveChannelRight( int index )
{
	moveChannelLeft( index + 1 );
}



MixerRoute * Mixer::createChannelSend( mix_ch_t fromChannel, mix_ch_t toChannel,
								float amount, bool preFader )
{
//	qDebug( "requested: %d to %d", fromChannel, toChannel );
	// find the existing connection
	MixerChannel * from = m_mixerChannels[fromChannel];
	MixerChannel * to = m_mixerChannels[toChannel];

	// Phase D: sends to a parallel bus are pre-fader by default, so the
	// sending channel's fader does not affect the bus input (spec 5.5).
	const bool routePreFader = preFader || to->isBus();

	for (const auto& send : from->m_sends)
	{
		if (send->receiver() == to)
		{
			// Luna cluster 5157: preFader is a plain field the render thread
			// reads, so an update takes the same change-mutex boundary
			// createRoute() does.
			Engine::audioEngine()->requestChangeInModel();
			// simply adjust the amount
			send->amount()->setValue(amount);
			send->setPreFader(routePreFader);
			Engine::audioEngine()->doneChangeInModel();
			return send;
		}
	}

	// connection does not exist. create a new one
	return createRoute( from, to, amount, routePreFader );
}


MixerRoute * Mixer::createRoute( MixerChannel * from, MixerChannel * to, float amount,
					bool preFader )
{
	if( from == to )
	{
		return nullptr;
	}
	Engine::audioEngine()->requestChangeInModel();
	auto route = new MixerRoute(from, to, amount, preFader);

	// board card #709: inside the cycle-permitted submode a route that CLOSES
	// a loop is flagged at creation - non-gating (processed() skips it),
	// snapshot-delivered (doProcessing pulls the committed copy) and
	// uncompensated (the latency pass fixes it at 0 frames). The check runs
	// against the graph WITHOUT this edge, so it answers exactly the question
	// the surface asked (resolveRoutingEnds). With the submode off this branch
	// cannot run, so the default path is byte-for-byte the old one;
	// loadSettings flags its own sends from their persisted attribute. The
	// master guard keeps the "master cannot send" rule outside the submode.
	if( shouldAutoFlagFeedbackRoute(from, to) )
	{
		route->setFeedback( true );
	}

	// add us to from's sends
	from->m_sends.push_back(route);

	// add us to to's receives
	to->m_receives.push_back(route);

	// add us to mixer's list
	Engine::mixer()->m_mixerRoutes.push_back(route);
	Engine::audioEngine()->doneChangeInModel();

	return route;
}


bool Mixer::shouldAutoFlagFeedbackRoute(MixerChannel* from, MixerChannel* to)
{
	return !m_loadingSettings && m_feedbackMode && from != m_mixerChannels[0]
		&& checkInfiniteLoop(from, to);
}


int Mixer::createBusChannel()
{
	const int index = createChannel();

	// D3 (mixer concurrency audit): setIsBus() is read by the render thread
	// (mixToChannel) and by updateLatencyCompensation, so it takes the same
	// change mutex. createChannel() has already released it, hence the
	// re-acquire here; both windows end at a period boundary.
	Engine::audioEngine()->requestChangeInModel();

	m_mixerChannels[index]->setIsBus(true);
	m_mixerChannels[index]->m_name = tr("Bus %1").arg(index);
	m_mixerChannels[index]->m_volumeModel.setDisplayName(
			m_mixerChannels[index]->m_name + ">" + tr("Volume"));
	m_mixerChannels[index]->m_panModel.setDisplayName(
			m_mixerChannels[index]->m_name + ">" + tr("Pan"));
	m_mixerChannels[index]->m_muteModel.setDisplayName(
			m_mixerChannels[index]->m_name + ">" + tr("Mute"));
	m_mixerChannels[index]->m_soloModel.setDisplayName(
			m_mixerChannels[index]->m_name + ">" + tr("Solo"));

	Engine::audioEngine()->doneChangeInModel();

	return index;
}


bool Mixer::isBusChannel(int channel) const
{
	return channel >= 0
		&& static_cast<std::size_t>(channel) < m_mixerChannels.size()
		&& m_mixerChannels[channel]->isBus();
}


MixerSidechainRoute * Mixer::createSidechainSend( mix_ch_t fromChannel,
			mix_ch_t toChannel, float amount, SidechainTapPoint mode )
{
	if( fromChannel == toChannel )
	{
		return nullptr;
	}
	MixerChannel * from = m_mixerChannels[fromChannel];
	MixerChannel * to = m_mixerChannels[toChannel];

	// update an existing route in place
	for( MixerSidechainRoute * route : from->m_sidechainSends )
	{
		if( route->receiver() == to )
		{
			// Luna cluster 5157: the tap mode is a plain field the render
			// thread reads, so an update takes the same change-mutex
			// boundary the creation path below does.
			Engine::audioEngine()->requestChangeInModel();
			route->amount()->setValue(amount);
			route->setMode(mode);
			Engine::audioEngine()->doneChangeInModel();
			return route;
		}
	}

	// can't send master to anything (legacy rule, kept for sidechain sends)
	if( from == m_mixerChannels[0] )
	{
		return nullptr;
	}

	// A cycle made of sidechain sends alone has no regular send to anchor
	// the ordering, so it is refused outright.
	if( checkSidechainCycle(from, to) )
	{
		return nullptr;
	}

	// A cycle through at least one regular send cannot be ordered either,
	// but a sidechain send is observation-only (spec 5.2: it never creates a
	// circular wait), so the route is accepted as deferred: it does not gate
	// its receiver and the receiver reads the previous period's committed
	// tap. Refusing it would silently drop the send (see
	// MixerRoutingBackwardCompatTest::phaseDProjectRoundTripsThroughSaveLoad).
	const bool deferred = checkInfiniteLoop(from, to);

	Engine::audioEngine()->requestChangeInModel();
	auto route = new MixerSidechainRoute(from, to, amount, mode, deferred);
	from->m_sidechainSends.push_back(route);
	to->m_sidechainReceives.push_back(route);
	m_mixerSidechainRoutes.push_back(route);
	Engine::audioEngine()->doneChangeInModel();

	return route;
}


MixerSidechainRoute * Mixer::channelSidechainSend( mix_ch_t fromChannel,
			mix_ch_t toChannel )
{
	if( fromChannel == toChannel )
	{
		return nullptr;
	}
	MixerChannel * from = m_mixerChannels[fromChannel];
	MixerChannel * to = m_mixerChannels[toChannel];
	for( MixerSidechainRoute * route : from->m_sidechainSends )
	{
		if( route->receiver() == to )
		{
			return route;
		}
	}
	return nullptr;
}


void Mixer::deleteSidechainSend( mix_ch_t fromChannel, mix_ch_t toChannel )
{
	MixerChannel * from = m_mixerChannels[fromChannel];
	MixerChannel * to = m_mixerChannels[toChannel];
	for( const auto& send : from->m_sidechainSends )
	{
		if( send->receiver() == to )
		{
			deleteSidechainSend(send);
			break;
		}
	}
}


void Mixer::deleteSidechainSend( MixerSidechainRoute * route )
{
	Engine::audioEngine()->requestChangeInModel();

	auto removeFromRouteVector = [route](MixerSidechainRouteVector& routeVec)
	{
		auto it = std::find(routeVec.begin(), routeVec.end(), route);
		if (it != routeVec.end()) { routeVec.erase(it); }
	};

	removeFromRouteVector(route->sender()->m_sidechainSends);
	removeFromRouteVector(route->receiver()->m_sidechainReceives);
	removeFromRouteVector(m_mixerSidechainRoutes);

	delete route;
	Engine::audioEngine()->doneChangeInModel();
}


// delete the connection made by createChannelSend
void Mixer::deleteChannelSend( mix_ch_t fromChannel, mix_ch_t toChannel )
{
	// delete the send
	MixerChannel * from = m_mixerChannels[fromChannel];
	MixerChannel * to	 = m_mixerChannels[toChannel];

	// find and delete the send entry
	for (const auto& send : from->m_sends)
	{
		if (send->receiver() == to)
		{
			deleteChannelSend(send);
			break;
		}
	}
}


void Mixer::deleteChannelSend( MixerRoute * route )
{
	Engine::audioEngine()->requestChangeInModel();

	auto removeFromMixerRoute = [route](MixerRouteVector& routeVec)
	{
		auto it = std::find(routeVec.begin(), routeVec.end(), route);
		if (it != routeVec.end()) { routeVec.erase(it); }
	};

	// remove us from from's sends
	removeFromMixerRoute(route->sender()->m_sends);

	// remove us from to's receives
	removeFromMixerRoute(route->receiver()->m_receives);

	// remove us from mixer's list
	removeFromMixerRoute(Engine::mixer()->m_mixerRoutes);

	delete route;
	Engine::audioEngine()->doneChangeInModel();
}


bool Mixer::isInfiniteLoop( mix_ch_t sendFrom, mix_ch_t sendTo )
{
	if( sendFrom == sendTo ) return true;
	MixerChannel * from = m_mixerChannels[sendFrom];
	MixerChannel * to = m_mixerChannels[sendTo];
	bool b = checkInfiniteLoop( from, to );
	return b;
}


/*! Enter or leave the cycle-permitted submode (board card #709). Entering
 *  creates nothing and changes no audio - it only permits the next
 *  loop-closing send, which createRoute then FLAGS (that flag, not this one,
 *  is what the workers read). Leaving is what restores normal PDC: every
 *  flagged send is deleted here, so the next period's latency pass recomputes
 *  a graph without them and the cycle rule is back in force. Control thread
 *  only. Returns how many loop-closing sends leaving removed (0 entering).
 */
int Mixer::setFeedbackMode( bool enabled )
{
	if( m_feedbackMode == enabled )
	{
		return 0;
	}
	m_feedbackMode = enabled;
	if( enabled )
	{
		return 0;
	}
	// Collect first: deleteChannelSend mutates both channels' lists.
	std::vector<MixerRoute*> doomed;
	for( MixerRoute * route : m_mixerRoutes )
	{
		if( route->feedback() )
		{
			doomed.push_back( route );
		}
	}
	for( MixerRoute * route : doomed )
	{
		deleteChannelSend( route );
	}
	return static_cast<int>( doomed.size() );
}


bool Mixer::checkInfiniteLoop( MixerChannel * from, MixerChannel * to )
{
	// Control thread only (createRoute / createSidechainSend / the GUI's
	// routing arrows), so the one visited-set allocation below is never on a
	// process/render/play path.
	std::vector<MixerChannel*> visited;
	return checkInfiniteLoop( from, to, visited );
}


bool Mixer::checkInfiniteLoop( MixerChannel * from, MixerChannel * to,
	std::vector<MixerChannel*>& visited )
{
	// can't send master to anything
	if( from == m_mixerChannels[0] )
	{
		return true;
	}

	// can't send channel to itself
	if( from == to )
	{
		return true;
	}

	// Already expanded on this walk: the feedback submode permits real
	// cycles, so an existing A->B->A would otherwise be followed forever and
	// blow the stack. Everything reachable from here was decided the first
	// time, so cutting the revisit cannot change the answer.
	if (std::find(visited.begin(), visited.end(), to) != visited.end())
	{
		return false;
	}
	visited.push_back(to);

	// follow sendTo's outputs recursively looking for something that sends
	// to sendFrom. Phase D: gating sidechain edges are traversed as well,
	// because a non-deferred sidechain send is a scheduling dependency and a
	// cycle through one would deadlock the dependency counter. Deferred
	// routes never gate, so they cannot deadlock and are skipped.
	for (const auto& send : to->m_sends)
	{
		// A feedback route is cycle-permitted by construction (createRoute
		// flags the edge that closes one), so it never closes a NEW cycle.
		if (send->feedback())
		{
			continue;
		}
		if (checkInfiniteLoop(from, send->receiver(), visited))
		{
			return true;
		}
	}
	for (const auto& send : to->m_sidechainSends)
	{
		if (send->deferred())
		{
			continue;
		}
		if (checkInfiniteLoop(from, send->receiver(), visited))
		{
			return true;
		}
	}

	return false;
}


bool Mixer::checkSidechainCycle( MixerChannel * from, MixerChannel * to )
{
	// would adding from->to close a cycle made of sidechain sends alone?
	// There is no regular send to anchor the ordering, so such a route is
	// refused instead of deferred (see createSidechainSend).
	if( from == to )
	{
		return true;
	}
	for( const auto& send : to->m_sidechainSends )
	{
		if( checkSidechainCycle(from, send->receiver()) )
		{
			return true;
		}
	}
	return false;
}


// how much does fromChannel send its output to the input of toChannel?
FloatModel * Mixer::channelSendModel( mix_ch_t fromChannel, mix_ch_t toChannel )
{
	if( fromChannel == toChannel )
	{
		return nullptr;
	}
	const MixerChannel * from = m_mixerChannels[fromChannel];
	const MixerChannel * to = m_mixerChannels[toChannel];

	for( MixerRoute * route : from->m_sends )
	{
		if( route->receiver() == to )
		{
			return route->amount();
		}
	}

	return nullptr;
}



void Mixer::mixToChannel(const AudioBus& bus, mix_ch_t channel)
{
	auto mixerChannel = m_mixerChannels[channel];
	// Phase D: a parallel bus never receives instrument output directly
	// (spec 5.5) - it is fed exclusively by pre-fader sends.
	if (mixerChannel->isBus())
	{
		return;
	}
	if (mixerChannel->m_muteModel.value() != false)
	{
		return;
	}

	// cluster new-p10-249: render threads never touch the shared channel bus
	// directly. They fold their block into their OWN partial, which no other
	// thread touches; Mixer::reduceInputPartials() sums the partials at the
	// period barrier (top of masterMix). A caller that is not on a render job
	// (the control side, the tests) accumulates straight into the channel bus,
	// exactly as before, so its data is visible immediately.
	const int slot = AudioEngineWorkerThread::currentRenderSlot();
	if (slot >= 0 && static_cast<std::size_t>(slot) < mixerChannel->m_inputPartials.size() - 1)
	{
		MixerChannel::InputPartial & partial = mixerChannel->m_inputPartials[static_cast<std::size_t>(slot)];
		const int frames = std::min( static_cast<int>(bus.frames()), static_cast<int>(partial.buffer.size()) );
		MixHelpers::add(partial.buffer.data(), bus.bus()[0], frames);
		partial.quiet &= bus.quietChannels(); // mix silence status
		partial.frames = std::max(partial.frames, frames);
		partial.live = true;
	}
	else
	{
		MixHelpers::add(mixerChannel->m_bus.bus()[0], bus.bus()[0], bus.frames());
		mixerChannel->m_bus.quietChannels() &= bus.quietChannels(); // mix silence status
		mixerChannel->m_hasInput = true;
	}
}




//! The period barrier: the render workers have joined (STAGE 1 and STAGE 2 are
//! done), so every render thread's input partial is complete and private. This
//! is the single owner of the worker-fed accumulation: it sums the live partials
//! into the channel buses before any channel is processed.
void Mixer::reduceInputPartials()
{
	for( MixerChannel * channel : m_mixerChannels )
	{
		for( MixerChannel::InputPartial & partial : channel->m_inputPartials )
		{
			if( ! partial.live )
			{
				continue;
			}
			const int frames = partial.frames;
			MixHelpers::add(channel->m_bus.bus()[0], partial.buffer.data(), frames);
			channel->m_bus.quietChannels() &= partial.quiet; // mix silence status
			channel->m_hasInput = true;

			// The partial is spent: clear it for the next period.
			partial.live = false;
			partial.frames = 0;
			partial.quiet.set();
			std::fill(partial.buffer.begin(), partial.buffer.begin() + frames, SampleFrame{});
		}
	}
}


void Mixer::prepareMasterMix()
{
	// PDC (#605): publish this period's compensation delays before any worker
	// runs. Allocation-free and lock-free; see updateLatencyCompensation().
	updateLatencyCompensation();

	m_mixerChannels[0]->m_bus.silenceAllChannels();

	// Phase D: publish the deferred sidechain taps. A deferred route closes a
	// cycle through a regular send, so it must not gate its receiver (spec
	// 5.2); the receiver instead reads the previous period's snapshot. This
	// runs on the audio render path (AudioEngine::renderStageNoteSetup)
	// before the period's workers start and only copies between pre-allocated
	// buffers (no allocation, no locking).
	for( MixerSidechainRoute * route : m_mixerSidechainRoutes )
	{
		if( route->deferred() )
		{
			route->commitIntermediate();
		}
	}

	// board card #709: publish each cycle-permitted send's previous-period
	// snapshot - the same quiescent moment, the same rule: no worker runs, and
	// only copies between the route's own pre-allocated buffers. The receiver
	// worker reads only this committed copy, so it never races the sender
	// worker writing the intermediate.
	for( MixerRoute * route : m_mixerRoutes )
	{
		if( route->feedback() )
		{
			route->commitFeedback();
		}
	}
}



void Mixer::resizeLatencyScratch(std::size_t channels)
{
	if (m_latencyInputScratch.size() >= channels)
	{
		return;
	}
	m_latencyInputScratch.resize(channels, 0);
	m_latencyOutputScratch.resize(channels, 0);
	m_latencyVisitScratch.resize(channels, 0);
	m_directSourceLatencyScratch.resize(channels, 0);
}



int Mixer::channelInputLatency(mix_ch_t channel) const
{
	return channel < m_mixerChannels.size()
		? m_mixerChannels[channel]->inputLatencyFrames()
		: 0;
}



namespace
{

//! MX-157: whether a hosted instrument's bus routed to @a index counts there: a bus channel takes no
//! instrument output (mixToChannel refuses it), the same rule as for a track's handle.
bool takesRoutedBus(const std::vector<MixerChannel*>& channels, int index)
{
	const auto at = static_cast<std::size_t>(index);
	return index >= 0 && at < channels.size() && !channels[at]->isBus();
}

//! MX-157: a hosted instrument's routed output buses are direct sources of their channels, and as late
//! as the instrument (InstrumentOutputBuses::forEachLatentRoute; allocation-free).
void addRoutedBusLatency(const std::vector<MixerChannel*>& channels, std::vector<int>& sourceLatency)
{
	InstrumentOutputBuses::forEachLatentRoute([&channels, &sourceLatency](int index, int latency) {
		if (!takesRoutedBus(channels, index)) { return; }
		auto& frames = sourceLatency[static_cast<std::size_t>(index)];
		frames = std::max(frames, latency);
	});
}

//! MX-157: @a clamped, or whether a routed bus needs more delay than a delay line holds (the handles'
//! clamp rule).
bool routedBusClamped(const std::vector<MixerChannel*>& channels, const std::vector<int>& alignment, int cap,
	bool clamped)
{
	InstrumentOutputBuses::forEachLatentRoute([&](int index, int latency) {
		clamped = clamped || (takesRoutedBus(channels, index) && alignment[static_cast<std::size_t>(index)] - latency > cap);
	});
	return clamped;
}

} // namespace



int Mixer::resolveLatency(std::size_t index)
{
	if (m_latencyVisitScratch[index] == 2)
	{
		return m_latencyOutputScratch[index];
	}
	if (m_latencyVisitScratch[index] == 1)
	{
		// Defensive: every REAL cycle has at least one cycle-permitted
		// (feedback) edge, and the loops below skip those edges, so the solve
		// never enters one. A cycle here would be a legacy file's regular-send
		// loop with no feedback flag (checkInfiniteLoop refuses creating one),
		// not a latency source.
		return 0;
	}
	m_latencyVisitScratch[index] = 1;

	const MixerChannel* channel = m_mixerChannels[index];
	int input = m_directSourceLatencyScratch[index];

	for (const MixerRoute* route : channel->m_receives)
	{
		if (route->feedback())
		{
			// board card #709: a cycle-permitted send reads a one-period-old
			// snapshot; that offset is inherent to feedback and cannot be
			// compensated, so the edge does not raise the receiver's
			// alignment point - PDC is suspended for the loop, and this skip
			// is what keeps the solve acyclic (every cycle has one of these).
			continue;
		}
		if (!route->enabled())
		{
			// A switched-off send delivers nothing (0.5.0 L08 / MX-047), so it
			// cannot raise the alignment point: keeping it here would delay
			// every other path into this channel for a route that is silent.
			continue;
		}
		resolveLatency(route->senderIndex());
		input = std::max(input, sendTapLatency(*route));
	}
	for (const MixerSidechainRoute* route : channel->m_sidechainReceives)
	{
		if (route->deferred())
		{
			// Deferred routes read a one-period-old snapshot; that offset is
			// inherent and cannot be compensated, so they do not raise the
			// receiver's alignment point.
			continue;
		}
		resolveLatency(route->senderIndex());
		input = std::max(input, sidechainTapLatency(*route));
	}

	m_latencyInputScratch[index] = input;
	m_latencyOutputScratch[index] =
		input + std::max(0, channel->m_fxChain.latencyFrames())
		+ std::max(0, channel->m_postInserts.chain().latencyFrames())  // 0.5.0 MX-082 (0 while empty)
		+ hardwareio::insertLatency( *channel )  // 0.5.0 F1 (MX-061): 0 while off or bypassed
		- channel->offsetFrames();  // Q050-339: a channel delay moves every path out of the channel (0 by default)
	m_latencyVisitScratch[index] = 2;
	return m_latencyOutputScratch[index];
}



int Mixer::sendTapLatency(const MixerRoute& route) const
{
	// The source postinserts::sendSource() hands the send: the post-insert output for a post-fader
	// send, the kept pre-fader copy (ahead of the post chain) for a pre-fader one (0.5.0 MX-082).
	const std::size_t sender = route.senderIndex();
	const int out = m_latencyOutputScratch[sender];
	return route.preFader() ? out - std::max(0, m_mixerChannels[sender]->m_postInserts.chain().latencyFrames()) : out;
}



int Mixer::sidechainTapLatency(const MixerSidechainRoute& route) const
{
	// Every tap is written before the post-fader inserts run (MixerChannel::doProcessing).
	const std::size_t sender = route.senderIndex();
	if (route.mode() == SidechainTapPoint::PreFx) { return m_latencyInputScratch[sender]; }
	return m_latencyOutputScratch[sender] - std::max(0, m_mixerChannels[sender]->m_postInserts.chain().latencyFrames());
}



void Mixer::updateLatencyCompensation()
{
#ifdef LMMS_TESTING
	if (s_latencyUpdateProbe != nullptr) { s_latencyUpdateProbe(this); }
#endif
	const std::size_t count = m_mixerChannels.size();
	if (count == 0 || m_latencyInputScratch.size() < count)
	{
		// Scratch is sized by createChannel() on the control thread; never
		// allocate here (audio thread).
		return;
	}

	std::fill_n(m_directSourceLatencyScratch.begin(), count, 0);
	std::fill_n(m_latencyVisitScratch.begin(), count, std::uint8_t{0});

	// Direct track inputs: a handle feeding channel c contributes its effect
	// chain's latency to that channel's input alignment. The handle list is
	// stable under the change mutex the render period holds.
	if (Engine::audioEngine() != nullptr)
	{
		for (const AudioBusHandle* handle : Engine::audioEngine()->audioBusHandles())
		{
			const mix_ch_t channel = handle->nextMixerChannel();
			// A bus never receives instrument output (mixToChannel refuses),
			// so a handle pointing at a bus contributes nothing.
			if (channel < count && !m_mixerChannels[channel]->isBus())
			{
				// Q050-339: less the track's manual offset (a negative offset raises the alignment point).
				m_directSourceLatencyScratch[channel] =
					std::max(m_directSourceLatencyScratch[channel],
						std::max(0, handle->alignedLatencyFrames()));
			}
		}
	}

	addRoutedBusLatency(m_mixerChannels, m_directSourceLatencyScratch);  // MX-157

	for (std::size_t i = 0; i < count; ++i)
	{
		resolveLatency(i);
	}

	// Publish the alignment point of every channel.
	for (std::size_t i = 0; i < count; ++i)
	{
		m_mixerChannels[i]->setInputLatencyFrames(m_latencyInputScratch[i]);
	}

	// A requested delay above the ring capacity is clamped by the delay line
	// itself (LatencyCompensation::effectiveDelay); track whether any edge
	// needs more than the graph can apply so the published total can report
	// only what the graph can honour (#605 follow-up, audit B-1).
	const int cap = LatencyCompensation::MaxFrames;
	bool clamped = false;

	// Regular sends: delay the sender so it lands on the receiver's point.
	for (MixerRoute* route : m_mixerRoutes)
	{
		if (route->feedback())
		{
			// board card #709: compensation is SUSPENDED for a cycle-permitted
			// send - a loop has no single latency to compensate, so its edge
			// gets no delay line at all (0 frames is the bit-identical
			// bypass), the alignment solve never traverses it (resolveLatency
			// skips it) and feedback.* says so at the point of use. Every
			// other edge is computed exactly as before.
			route->setCompensationFrames(0);
			continue;
		}
		const int delay = m_latencyInputScratch[route->receiverIndex()] - sendTapLatency(*route);
		clamped = clamped || delay > cap;
		route->setCompensationFrames(std::max(delay, 0));
	}

	// Sidechain sends: the tap point decides the latency the receiver sees.
	for (MixerSidechainRoute* route : m_mixerSidechainRoutes)
	{
		const int delay = m_latencyInputScratch[route->receiverIndex()] - sidechainTapLatency(*route);
		clamped = clamped || delay > cap;
		route->setCompensationFrames(std::max(delay, 0));
	}

	// Direct track inputs are delayed by the handle itself (see
	// AudioBusHandle::doProcessing), which uses the same cap.
	if (Engine::audioEngine() != nullptr)
	{
		for (const AudioBusHandle* handle : Engine::audioEngine()->audioBusHandles())
		{
			const mix_ch_t channel = handle->nextMixerChannel();
			// Q050-174: a handle monitored unaligned applies no delay, so it cannot be clamped.
			if (channel < count && !m_mixerChannels[channel]->isBus() && !handle->monitorUnaligned())
			{
				clamped = clamped
					|| (m_latencyInputScratch[channel]
						- handle->alignedLatencyFrames()) > cap;
			}
		}
	}

	clamped = routedBusClamped(m_mixerChannels, m_latencyInputScratch, cap, clamped);  // MX-157

	const int masterChain =
		std::max(0, m_mixerChannels[0]->m_fxChain.latencyFrames())
		+ std::max(0, m_mixerChannels[0]->m_postInserts.chain().latencyFrames())  // 0.5.0 MX-082
		+ hardwareio::insertLatency( *m_mixerChannels[0] );
	const int alignment = m_latencyInputScratch[0];
	// The alignment point is realised by delaying the earlier paths; no path
	// can be delayed by more than the ring capacity, so a clamped alignment
	// cannot be honoured. Publish what the graph can actually apply, keeping
	// the master chain's own (always applied) latency on top. At or below the
	// cap this is the exact requested value.
	m_latencyCompensationClamped.store(clamped, std::memory_order_relaxed);
	m_totalLatencyFrames.store(
		(clamped ? std::min(alignment, cap) : alignment) + masterChain,
		std::memory_order_relaxed);
}



void Mixer::masterMix( SampleFrame* _buf )
{
	const int fpp = Engine::audioEngine()->framesPerPeriod();

	// cluster new-p10-249: fold render-thread input partials into the channel
	// buses at the period barrier, before any channel is processed.
	reduceInputPartials();

	// add the channels that have no dependencies (no incoming senders, ie.
	// no receives) to the jobqueue. The channels that have receives get
	// added when their senders get processed, which is detected by
	// dependency counting.
	// also instantly add all muted channels as they don't need to care
	// about their senders, and can just increment the deps of their
	// recipients right away.
	AudioEngineWorkerThread::resetJobQueue( AudioEngineWorkerThread::JobQueue::OperationMode::Dynamic );

	// D1 (mixer concurrency audit): latch the whole mixer's mute state for this
	// period *before* acting on any channel. processed() below reads the latch
	// of the channels this one sends to, so a single fused pass tested a
	// higher-indexed receiver against the previous period's value: an unmuted
	// receiver of a muted sender was never counted, was therefore never
	// queued, and the period that un-mutes it (or un-solos a set of channels)
	// came out silent - a deterministic one-period dropout. Splitting the pass
	// also means no channel is in the job queue while the latches are being
	// written, so a worker can only ever observe the value this period decided.
	for( MixerChannel * ch : m_mixerChannels )
	{
		// Relaxed: this period's value reaches a worker through the job
		// queue's release/acquire pair (ThreadableJob::queue() / process()),
		// which orders every store made here before the job becomes visible.
		ch->m_muted.store( ch->m_muteModel.value(), std::memory_order_relaxed );
	}

	for( MixerChannel * ch : m_mixerChannels )
	{
		if( ch->m_muted.load(std::memory_order_relaxed) ) // instantly "process" muted channels
		{
			// PDC (#605 audit C7): a muted channel never runs doProcessing(),
			// so the incoming delay lines would freeze for the whole mute and
			// replay pre-mute history on unmute. Advance their timelines with
			// silence here, where muted channels are actually handled.
			for( MixerRoute * route : ch->m_receives )
			{
				route->advanceSilence(fpp);
			}
			for( MixerSidechainRoute * route : ch->m_sidechainReceives )
			{
				route->advanceSilence(fpp);
			}
			// D2(ii)/(iii) (mixer concurrency audit): the other half of the C7
			// fix. A muted channel writes no sidechain taps, so its outgoing
			// intermediates keep the last block they were given; doProcessing()
			// - which is where they would be cleared by the receiver - never
			// runs. Clear them here instead. Without this a receiver sums a
			// stale tap (D2(ii)), and because prepareMasterMix() re-commits a
			// deferred route's intermediate every period, a deferred receiver
			// loops the same stale block forever (D2(iii)).
			for( MixerSidechainRoute * route : ch->m_sidechainSends )
			{
				route->clearIntermediate();
			}
			// board card #709: a muted sender writes no feedback snapshot
			// (doProcessing never runs), so clear its outgoing ones here -
			// otherwise the loop would replay the last block forever, the
			// D2(iii) rule for cycle-permitted sends.
			for( MixerRoute * route : ch->m_sends )
			{
				if( route->feedback() )
				{
					route->clearFeedback();
				}
			}
			mixerstrip::silenceMeters( *ch );  // its live meters must not hold the last unmuted period
			ch->processed();
			ch->done();
		}
		else if( ch->gatingReceives() == 0 && ch->gatingSidechainReceives() == 0 )
		{
			ch->m_queued = true;
			AudioEngineWorkerThread::addJob( ch );
		}
	}
	while (m_mixerChannels[0]->state() != ThreadableJob::ProcessingState::Done)
	{
		bool found = false;
		for( MixerChannel * ch : m_mixerChannels )
		{
			const auto s = ch->state();
			if (s == ThreadableJob::ProcessingState::Queued
				|| s == ThreadableJob::ProcessingState::InProgress)
			{
				found = true;
				break;
			}
		}
		if( !found )
		{
			break;
		}
		AudioEngineWorkerThread::startAndWaitForJobs();
	}

	// handle sample-exact data in master volume fader
	ValueBuffer * volBuf = postinserts::faderBuffer( *m_mixerChannels[0], nullptr );  // 0.5.0 MX-082

	if( volBuf )
	{
		// Keep the master channel's source buffer pre-fader: the listen bus reads
		// it after this mix, applying the master fader once for AFL and bypassing
		// it for PFL.
		MixHelpers::addMultipliedByBuffer( _buf, m_mixerChannels[0]->m_buffer, 1.0f, volBuf, fpp );
	}
	else
	{
		const float v = postinserts::faderValue( *m_mixerChannels[0], nullptr );
		MixHelpers::addMultiplied( _buf, m_mixerChannels[0]->m_buffer, v, fpp );
	}
	// 0.5.0 F7 (MX-060): a surround master's other speakers - onto the outputs, or folded into the mix.
	surround::masterOutput( *this, _buf, fpp );
	recordertaps::master( _buf, fpp );  // 0.5.0 WF-072: a record route resampling the master
	// 0.5.0 F1 (MX-040, MX-036, MX-061): direct outs, cue mixes and insert sends onto the hardware outputs.
	hardwareio::mixOutputs( *this, _buf, fpp );
	// 0.5.0 L09 (MX-037): a channel listened to (AFL / PFL) replaces the mix on the output - unless the
	// listen goes to a cue mix instead (0.5.0 F1, Ableton's Solo/Cue switch).
	hardwareio::listenOnMain( _buf, *this, fpp );

	// clear all channel buffers and
	// reset channel process state
	for( int i = 0; i < numChannels(); ++i)
	{
		m_mixerChannels[i]->m_bus.silenceAllChannels();
		// Phase D: the sidechain input and the post-fader snapshot are
		// per-period scratch data
		m_mixerChannels[i]->m_sidechainBuffer.silenceAllChannels();
		m_mixerChannels[i]->m_postFaderBuffer.silenceAllChannels();
		m_mixerChannels[i]->m_surround.clearPeriod();  // 0.5.0 F7
		m_mixerChannels[i]->reset();
		m_mixerChannels[i]->m_queued = false;
		// also reset hasInput
		m_mixerChannels[i]->m_hasInput = false;
		m_mixerChannels[i]->m_dependenciesMet = 0;
	}
}




void Mixer::clear()
{
	// VCA groups (#622) name channel indices; drop them before the channels so
	// the index bookkeeping has nothing left to chase.
	clearVcaGroups();

	const bool deleted = m_mixerChannels.size() > 1;
	m_clearing = true;
	while( m_mixerChannels.size() > 1 )
	{
		deleteChannel(1);
	}
	m_clearing = false;
	if (deleted) { emit channelDeleted(); }

	clearChannel(0);
	hardwareio::resetMixer();  // 0.5.0 F1 (MX-036): the cue masters
	surround::resetMixer();  // 0.5.0 F7 (MX-060): the downmix options
}



void Mixer::clearChannel(mix_ch_t index)
{
	MixerChannel * ch = m_mixerChannels[index];
	ch->m_fxChain.clear();
	// #599: a cleared channel has no rack (no parallel chains, no selector).
	ch->m_rack.clear();
	ch->m_postInserts.clear();  // 0.5.0 MX-082
	ch->m_hardwareIo.clear();  // 0.5.0 F1
	ch->m_surround.reset();  // 0.5.0 F7
	ch->m_volumeModel.setValue( 1.0f );
	ch->m_panModel.setValue( 0.0f );
	ch->m_muteModel.setValue( false );
	ch->m_soloModel.setValue( false );
	// Luna cluster 4279: the strip's own settings are reset with the rest, so a cleared
	// channel (the master after Mixer::clear) does not keep applying the previous
	// project's trim, polarity, EQ, pan mode and the rest to later audio.
	mixerstrip::reset(*ch);
	ch->m_name = ( index == 0 ) ? tr( "Master" ) : tr( "Channel %1" ).arg( index );
	ch->m_volumeModel.setDisplayName( ch->m_name + ">" + tr( "Volume" ) );
	ch->m_panModel.setDisplayName( ch->m_name + ">" + tr( "Pan" ) );
	ch->m_muteModel.setDisplayName( ch->m_name + ">" + tr( "Mute" ) );
	ch->m_soloModel.setDisplayName( ch->m_name + ">" + tr( "Solo" ) );
	ch->setColor(std::nullopt);

	// Phase D: drop bus flag and any sidechain routing
	ch->setIsBus(false);
	while (!ch->m_sidechainSends.empty())
	{
		deleteSidechainSend(ch->m_sidechainSends.front());
	}
	while (!ch->m_sidechainReceives.empty())
	{
		deleteSidechainSend(ch->m_sidechainReceives.front());
	}
	ch->m_sidechainBuffer.silenceAllChannels();
	ch->m_postFaderBuffer.silenceAllChannels();
	// cluster new-p10-249: drop any pending render-thread input partials.
	for( MixerChannel::InputPartial & partial : ch->m_inputPartials )
	{
		partial.live = false;
		partial.frames = 0;
		partial.quiet.set();
		std::fill(partial.buffer.begin(), partial.buffer.end(), SampleFrame{});
	}

	// send only to master
	if( index > 0)
	{
		// delete existing sends
		while (!ch->m_sends.empty())
		{
			deleteChannelSend(ch->m_sends.front());
		}

		// add send to master
		createChannelSend( index, 0 );
	}

	// delete receives
	while (!ch->m_receives.empty())
	{
		deleteChannelSend(ch->m_receives.front());
	}
}

void Mixer::saveSettings( QDomDocument & _doc, QDomElement & _this )
{
	// save channels
	for (auto i = std::size_t{0}; i < m_mixerChannels.size(); ++i)
	{
		MixerChannel * ch = m_mixerChannels[i];

		QDomElement mixch = _doc.createElement( QString( "mixerchannel" ) );
		_this.appendChild( mixch );

		ch->m_fxChain.saveState( _doc, mixch );
		// #599: the rack's parallel chains and the chain selector; a channel
		// with no rack writes no element at all.
		ch->m_rack.saveSettings( _doc, mixch );
		ch->m_postInserts.save( _doc, mixch );  // 0.5.0 MX-082: only while it holds devices
		ch->m_hardwareIo.save( _doc, mixch );  // 0.5.0 F1: only while it routes something to the hardware
		ch->m_surround.save( _doc, mixch );  // 0.5.0 F7: only while it is not stereo or pans
		ch->m_volumeModel.saveSettings( _doc, mixch, "volume" );
		// Written only when it says something: a centred, unautomated pan
		// writes nothing, so a project that never panned a channel saves
		// byte-identically to one written before the model existed.
		if (ch->m_panModel.value() != 0.0f || ch->m_panModel.isAutomatedOrControlled())
		{
			ch->m_panModel.saveSettings( _doc, mixch, "pan" );
		}
		ch->m_muteModel.saveSettings( _doc, mixch, "muted" );
		ch->m_soloModel.saveSettings( _doc, mixch, "soloed" );
		mixerstrip::save( *ch, mixch );  // 0.5.0 L08: only what differs from the defaults
		mixch.setAttribute("num", static_cast<qulonglong>(i));
		// SPEC-stable-ids.md 3.1/3.2, slice 2: the channel's stable id as an
		// ATTRIBUTE on the channel's own element, BESIDE `num` - which keeps
		// its meaning as the channel's position in the mixer (the load
		// allocates channels up to it). They are two different numbers now and
		// must not be conflated: `num` is reconstructed positionally on load,
		// `id` names the object and is read back. An ATTRIBUTE and not a child
		// element, for the reason Track::saveTrack records for the track id -
		// this loader walks the element's children, so a child would become a
		// phantom send to a legacy reader on every channel of every project.
		// An id this save is ABOUT to write must clear the counter even when no
		// load pass ever saw it: engine-born default channels take their id from
		// allocate() with no document element to setId() from, so nothing noted
		// it. Without this the file writes an id at/above its own `next-id`, the
		// next load's setId() floors above that, and every save/load/save round
		// trip grows `next-id` once (LanesUpconvertTest::
		// aProjectThatUsesNoneOfThisWritesNoLanesSection caught it on a one-track
		// fixture whose highest track id sits below the Master channel's).
		ProjectIds::observe( ch->id() );
		mixch.setAttribute("id", ch->id());
		mixch.setAttribute( "name", ch->m_name );
		if (const auto& color = ch->color()) { mixch.setAttribute("color", color->name()); }

		// Phase D: parallel bus marker. A legacy LMMS ignores this element
		// and loads the channel as a regular channel, which is the intended
		// forward-compatible degradation.
		if (ch->isBus())
		{
			QDomElement busDom = _doc.createElement( QString( "bus" ) );
			mixch.appendChild( busDom );
			QStringList sources;
			for (const MixerChannel * other : m_mixerChannels)
			{
				for (const auto& send : other->m_sends)
				{
					if (send->receiver() == ch)
					{
						sources.append(QString::number(other->index()));
					}
				}
			}
			busDom.setAttribute("sources", sources.join(','));
		}

		// add the channel sends
		for (const auto& send : ch->m_sends)
		{
			QDomElement sendsDom = _doc.createElement( QString( "send" ) );
			mixch.appendChild( sendsDom );

			sendsDom.setAttribute("channel", send->receiverIndex());
			// Luna cluster 1512: a send to a bus is pre-fader by default, so
			// its post-fader setting has to be written out explicitly -
			// otherwise the file records nothing and the load applies the
			// default again.
			if (send->preFader() || send->receiver()->isBus()) { sendsDom.setAttribute("prefader", send->preFader() ? "1" : "0"); }
			// board card #709: a cycle-permitted send carries its own flag -
			// the persisted half of the submode (the mode itself is derived
			// from these flags on load). A send without the flag writes byte
			// exactly what it always wrote, so a normal project's saved bytes
			// are unchanged.
			if (send->feedback()) { sendsDom.setAttribute("feedback", "1"); }
			send->amount()->saveSettings(_doc, sendsDom, "amount");
			// 0.5.0 L09 (MX-048): a send's own pan, only when it has one.
			if (send->pan()->value() != 0.f || send->pan()->isAutomated()) { send->pan()->saveSettings(_doc, sendsDom, "pan"); }
			if (!send->enabled()) { sendsDom.setAttribute("off", "1"); }  // 0.5.0 L08 (MX-047)
		}

		// Phase D: sidechain sends. Legacy LMMS ignores these elements, so
		// the sidechain routing is silently dropped on downgrade.
		for (const auto& send : ch->m_sidechainSends)
		{
			QDomElement scDom = _doc.createElement( QString( "sidechain-send" ) );
			mixch.appendChild( scDom );

			scDom.setAttribute("channel", send->receiverIndex());
			scDom.setAttribute("mode", static_cast<int>(send->mode()));
			// Luna cluster 4277: a deferred route is INFERRED at creation from
			// the routing graph, and loadSettings re-infers it against a
			// half-built graph (this channel's element is read before the
			// regular send that makes it deferred exists), so the flag has to
			// be written out. Only a deferred route carries the attribute, so a
			// project with no deferred route saves exactly what it always did.
			if (send->deferred()) { scDom.setAttribute("deferred", "1"); }
			send->amount()->saveSettings(_doc, scDom, "amount");
		}
	}

	// VCA / mix-and-edit groups (#622). One element per group, beside the
	// channels. A legacy LMMS skips these children (Mixer::loadSettings in a
	// pre-#622 build reads only <mixerchannel> elements by name), so the
	// grouping degrades to "no groups" and every member plays at its own
	// volume -- the same forward-compatible degradation as the <bus> marker.
	for (VcaGroup* group : m_vcaGroups)
	{
		QDomElement groupDom = _doc.createElement( QString( "vcagroup" ) );
		_this.appendChild( groupDom );

		ProjectIds::observe( group->id() );		// same unreported-id rule as the channel id above
		groupDom.setAttribute("id", group->id());
		groupDom.setAttribute("name", group->name());
		group->vcaModel()->saveSettings(_doc, groupDom, "vca");
		group->muteModel()->saveSettings(_doc, groupDom, "muted");
		group->soloModel()->saveSettings(_doc, groupDom, "soloed");

		// OWNER-31 item 11: the edit half. `locked` is written on EVERY group
		// (so the loader has one attribute to read and an element written
		// before the edit half existed reads as locked), and one <edittrack>
		// child per edit-set track, by STABLE TRACK ID rather than position -
		// the number the track's own element carries. A pre-0.3.0 reader skips
		// both exactly as it skips a <vcagroup>, so the grouping still degrades
		// to "no groups" in an older LMMS.
		groupDom.setAttribute("locked", group->isPhaseLocked() ? 1 : 0);
		// 0.5.0 L09 (MX-070 / MX-071): nesting and the chosen links, written only when not the default.
		if (group->parentId() != VcaGroup::NoParent) { groupDom.setAttribute("parent", group->parentId()); }
		if (!group->linksGain() || !group->linksMute() || !group->linksSolo())
		{
			groupDom.setAttribute("links", QStringLiteral("%1%2%3").arg(group->linksGain() ? "g" : "").arg(group->linksMute() ? "m" : "")
				.arg(group->linksSolo() ? "s" : ""));
		}
		if (group->isSuspended()) { groupDom.setAttribute("suspended", 1); }

		for (mix_ch_t member : group->members())
		{
			QDomElement memberDom = _doc.createElement( QString( "member" ) );
			groupDom.appendChild( memberDom );
			memberDom.setAttribute("channel", member);
		}

		for (int editTrack : group->editTracks())
		{
			QDomElement editDom = _doc.createElement( QString( "edittrack" ) );
			groupDom.appendChild( editDom );
			editDom.setAttribute("track", editTrack);
		}
	}
	hardwareio::saveMixer( _doc, _this );  // 0.5.0 F1 (MX-036): the cue masters, only what is not the default
	surround::saveMixer( _doc, _this );  // 0.5.0 F7 (MX-060): the downmix options, only when not the default
}

// make sure we have at least num channels
void Mixer::allocateChannelsTo(int num, int describedChannels)
{
	// A project-driven index is bounded by what the file describes: the
	// number comes from a file, and a file that names two billion channels
	// must not allocate two billion channels (it would hang the load and
	// exhaust the memory).
	if (!isSupportedProjectMixerIndex(num, describedChannels)) { return; }
	while (static_cast<std::size_t>(num) >= m_mixerChannels.size())
	{
		createChannel();

		// delete the default send to master
		deleteChannelSend( m_mixerChannels.size()-1, 0 );
	}
}


void Mixer::loadSettings( const QDomElement & _this )
{
	clear();
	m_feedbackMode = false;
	const QScopedValueRollback<bool> loadingSettings(m_loadingSettings, true);
	QDomNode node = _this.firstChild();
	// The channels this file describes. Every project-supplied index is
	// bounded by them, so the loader can never be talked into allocating more
	// channels than the file itself holds - and a project the application
	// created and saved (it writes one <mixerchannel> element per channel)
	// always loads, however many channels it has.
	const int describedChannels = countDescribedChannels( _this );
	const auto reportUnsupportedIndex = [](const QString& item, const QString& value, int describedChannels)
	{
		if (Song* song = Engine::getSong())
		{
			song->collectError(QObject::tr(
				"Project mixer %1 index '%2' is outside the range this project describes (0-%3); item ignored.")
				.arg(item, value, QString::number(describedChannels)));
		}
	};

	// Phase D pre-pass: bus flags must be known before sends are loaded,
	// otherwise a send to a bus that appears later in the document would be
	// loaded as post-fader. A missing <bus> element leaves the channel a
	// regular channel, so legacy projects load unchanged.
	for( QDomNode pre = _this.firstChild(); ! pre.isNull(); pre = pre.nextSibling() )
	{
		QDomElement mixch = pre.toElement();
		if( mixch.nodeName() == QString( "mixerchannel" )
			&& ! mixch.firstChildElement( QString( "bus" ) ).isNull() )
		{
			// A `num` that is not a channel index must be rejected, not used as
			// one: allocateChannelsTo() ignores a number outside the supported
			// range, so indexing with it read outside m_mixerChannels. The
			// value is read wide, so a number that overflows an int cannot
			// become channel 0 (the master) here. The main pass below
			// reports the element once, so this pre-pass stays quiet.
			int busNum = 0;
			if( parseProjectChannelIndex( mixch.attribute( "num" ), describedChannels, busNum ) != ProjectIndex::Ok )
			{
				continue;
			}
			// Zero is the master, which cannot be a bus, so only a positive
			// number is one.
			if( busNum <= 0 ) { continue; }
			allocateChannelsTo( busNum, describedChannels );
			m_mixerChannels[busNum]->setIsBus( true );
		}
	}

	while( ! node.isNull() )
	{
		QDomElement mixch = node.toElement();

		// #622: <vcagroup> elements live beside the channels. A pre-#622
		// project has none; this build must not mistake one for a channel
		// (that would load master's name and volume from a foreign element).
		if( mixch.nodeName() != QString( "mixerchannel" ) )
		{
			node = node.nextSibling();
			continue;
		}

		// index of the channel we are about to load
		// The number is read as a WIDE integer, because a value that does not
		// fit in an int must not become channel 0: toInt() alone would return
		// 0 and let this element overwrite the master. A `num` that is not a
		// number at all names no channel, so it is skipped whole; a number
		// the file does not back up is refused AND reported, so the user
		// learns that the file names a channel it does not have. Zero is the
		// master, a real channel that clear() creates, so its saved state
		// must still load.
		int num = 0;
		const ProjectIndex numKind =
			parseProjectChannelIndex( mixch.attribute( "num" ), describedChannels, num );
		if( numKind != ProjectIndex::Ok )
		{
			if( numKind == ProjectIndex::OutOfRange )
			{
				reportUnsupportedIndex( QStringLiteral( "channel" ), mixch.attribute( "num" ), describedChannels );
			}
			node = node.nextSibling();
			continue;
		}

		// allocate enough channels
		allocateChannelsTo( num, describedChannels );

		// The stable id (SPEC-stable-ids.md rules R1/R2, slice 2) - the same
		// shape Track::loadTrack uses for the track element. A file that
		// carries one keeps it; a legacy file that does not keeps the number
		// the constructor already handed out, which is deterministic because
		// this load walks the <mixerchannel> elements in document order.
		// Either way ProjectIds::loadAssignments() counts it, and project.open
		// reports the count as `ids_assigned`, so a legacy file's one-time
		// upgrade is stated rather than silent. Read AFTER allocateChannelsTo:
		// that call is what constructs the channel this element describes.
		if( mixch.hasAttribute( "id" ) )
		{
			bool ok = false;
			const int stored = mixch.attribute( "id" ).toInt( &ok );
			if( ok && stored >= 0 ) { m_mixerChannels[num]->setId( stored ); }
			else { ProjectIds::noteLoadAssignment( m_mixerChannels[num]->id() ); }
		}
		else
		{
			ProjectIds::noteLoadAssignment( m_mixerChannels[num]->id() );
		}

		loadFiniteMixerSetting(m_mixerChannels[num]->m_volumeModel, mixch, QStringLiteral("volume"));
		// Absent means centred - every file written before channel pan existed.
		// Explicit, so a restored journal checkpoint without the attribute
		// takes a pan back off rather than keeping it.
		if (mixch.hasAttribute("pan") || !mixch.firstChildElement("pan").isNull()
			|| !mixch.firstChildElement("connection").firstChildElement("pan").isNull())
		{
			loadFiniteMixerSetting(m_mixerChannels[num]->m_panModel, mixch, QStringLiteral("pan"));
		}
		else
		{
			m_mixerChannels[num]->m_panModel.setValue( 0.0f );
		}
		m_mixerChannels[num]->m_muteModel.loadSettings( mixch, "muted" );
		m_mixerChannels[num]->m_soloModel.loadSettings( mixch, "soloed" );
		mixerstrip::load( *m_mixerChannels[num], mixch );  // absent: the defaults
		m_mixerChannels[num]->m_name = mixch.attribute( "name" );
		if (mixch.hasAttribute("color"))
		{
			m_mixerChannels[num]->setColor(QColor{mixch.attribute("color")});
		}

		m_mixerChannels[num]->m_fxChain.restoreState( mixch.firstChildElement(
			m_mixerChannels[num]->m_fxChain.nodeName() ) );

		// #599: the channel's rack, if the project has one. A project with no
		// <rack> element loads an empty rack, which is off the signal path.
		m_mixerChannels[num]->m_rack.loadSettings( mixch );
		m_mixerChannels[num]->m_postInserts.load( mixch );  // 0.5.0 MX-082: absent empties it
		m_mixerChannels[num]->m_hardwareIo.load( mixch, Engine::audioEngine()->framesPerPeriod() );  // 0.5.0 F1
		m_mixerChannels[num]->m_surround.load( mixch );  // 0.5.0 F7: absent is stereo, panner off

		// mixer sends
		QDomNodeList chData = mixch.childNodes();
		for (auto i = 0; i < chData.length(); ++i)
		{
			QDomElement chDataItem = chData.at(i).toElement();
			if( chDataItem.nodeName() == QString( "send" ) )
			{
				int sendTo = 0;
				const ProjectIndex sendKind =
					parseProjectChannelIndex( chDataItem.attribute( "channel" ), describedChannels, sendTo );
				// A `channel` that is not a channel index is not routed
				// anywhere: createChannelSend() indexes m_mixerChannels with
				// it. Zero (the master) is the one valid low number, so a
				// target that is not a number at all, or that is negative or
				// outside the range the file describes, is refused here
				// rather than converted to an unsigned index downstream - and
				// rather than silently becoming the master, which is what a
				// failed conversion to an int used to do.
				if( sendKind != ProjectIndex::Ok )
				{
					if( sendKind == ProjectIndex::OutOfRange )
					{
						reportUnsupportedIndex( QStringLiteral( "send target" ), chDataItem.attribute( "channel" ), describedChannels );
					}
					continue;
				}
				allocateChannelsTo( sendTo, describedChannels ) ;
				const bool preFader =
					chDataItem.attribute( "prefader" ).toInt() != 0;
				MixerRoute * mxr = createChannelSend( num, sendTo, 1.0f, preFader );
				// Luna cluster 1512: createChannelSend() applies the bus
				// pre-fader default, which is right for a send the file says
				// nothing about. A file that states the setting is taken at
				// its word, so a post-fader send to a bus stays post-fader.
				if( mxr && chDataItem.hasAttribute( "prefader" ) ) { mxr->setPreFader( preFader ); }
				if (mxr) { loadFiniteMixerSetting(*mxr->amount(), chDataItem, QStringLiteral("amount")); }
				if (mxr && chDataItem.hasAttribute("pan"))
				{
					loadFiniteMixerSetting(*mxr->pan(), chDataItem, QStringLiteral("pan"));
				}
				if( mxr ) { mxr->enabledModel()->setValue( chDataItem.attribute( "off" ).toInt() == 0 ); }
				// board card #709: a cycle-permitted send carries its own
				// flag, and loading one turns the submode on WITH it (the
				// mode is derived from the flags - it is not saved itself).
				// A file without the attribute loads exactly as before.
				if( mxr && chDataItem.attribute( "feedback" ).toInt() != 0 )
				{
					mxr->setFeedback( true );
					m_feedbackMode = true;
				}
			}
			else if( chDataItem.nodeName() == QString( "sidechain-send" ) )
			{
				int sendTo = 0;
				const ProjectIndex sendKind =
					parseProjectChannelIndex( chDataItem.attribute( "channel" ), describedChannels, sendTo );
				// Same rule as a <send>: a target that is not a channel index
				// is not a channel, and createSidechainSend() converts it to
				// an unsigned index - so a failed conversion must not become
				// the master either.
				if( sendKind != ProjectIndex::Ok )
				{
					if( sendKind == ProjectIndex::OutOfRange )
					{
						reportUnsupportedIndex( QStringLiteral( "sidechain send target" ), chDataItem.attribute( "channel" ), describedChannels );
					}
					continue;
				}
				allocateChannelsTo( sendTo, describedChannels );
				const int mode = chDataItem.attribute( "mode" ).toInt();
				const SidechainTapPoint tap =
					( mode >= 0 && mode <= 3 )
						? static_cast<SidechainTapPoint>( mode )
						: SidechainTapPoint::PreFader;
				MixerSidechainRoute * mxr =
					createSidechainSend( num, sendTo, 1.0f, tap );
				// Luna cluster 4277: a file that states the route is deferred
				// is taken at its word. Inference at this point runs against a
				// graph whose later channels have not been read yet, so it
				// cannot see the regular send that closes the cycle.
				if( mxr && chDataItem.hasAttribute( "deferred" ) )
				{
					mxr->setDeferred( chDataItem.attribute( "deferred" ).toInt() != 0 );
				}
				if (mxr) { loadFiniteMixerSetting(*mxr->amount(), chDataItem, QStringLiteral("amount")); }
			}
		}



		node = node.nextSibling();
	}

	// VCA / mix-and-edit groups (#622). Absent from a pre-#622 project, in
	// which case no group exists, every channel keeps publishing unity gain,
	// and the project plays exactly as it did before this feature existed.
	for( QDomNode groupNode = _this.firstChild(); ! groupNode.isNull();
			groupNode = groupNode.nextSibling() )
	{
		QDomElement groupDom = groupNode.toElement();
		if( groupDom.nodeName() != QString( "vcagroup" ) )
		{
			continue;
		}

		// An id the file recorded is preserved (so a group keeps its identity
		// across a round trip); a file without one gets the lowest free id.
		const QString idAttr = groupDom.attribute( "id" );
		const QString name = groupDom.attribute( "name" );
		const int requestedId = idAttr.isEmpty() ? -1 : idAttr.toInt();
		VcaGroup* group = createVcaGroup( name, requestedId );
		if( group == nullptr )
		{
			group = createVcaGroup( name, -1 );
			if( group == nullptr ) { continue; }
			qWarning() << "Duplicate VCA group id" << requestedId
				<< "in project; assigned id" << group->id();
		}

		loadFiniteMixerSetting(*group->vcaModel(), groupDom, QStringLiteral("vca"));
		group->muteModel()->loadSettings( groupDom, "muted" );
		group->soloModel()->loadSettings( groupDom, "soloed" );
		// OWNER-31 item 11: the edit half. `locked` is read for EVERY group
		// with a default of 1, so a <vcagroup> written before the edit half
		// existed loads as the locked group it was.
		group->setPhaseLocked( groupDom.attribute( "locked", "1" ) != QString( "0" ) );
		group->setParentId( groupDom.attribute( "parent", QString::number( VcaGroup::NoParent ) ).toInt() );
		const QString links = groupDom.attribute( "links", "gms" );
		group->setLinks( links.contains( 'g' ), links.contains( 'm' ), links.contains( 's' ) );
		group->setSuspended( groupDom.attribute( "suspended", "0" ).toInt() != 0 );

		for( QDomNode memberNode = groupDom.firstChild(); ! memberNode.isNull();
				memberNode = memberNode.nextSibling() )
		{
			QDomElement memberDom = memberNode.toElement();
			// The edit set, by STABLE TRACK ID. Nothing is resolved here: the
			// mixer is restored BEFORE the track container (Song::loadState),
			// so no Track exists yet to check the id against - and a group that
			// dropped an id it could not resolve would rewrite the user's own
			// membership. vca.get_state reports an id whose track is gone as
			// `missing` instead.
			if( memberDom.nodeName() == QString( "edittrack" ) )
			{
				group->addEditTrack( memberDom.attribute( "track" ).toInt() );
				continue;
			}
			if( memberDom.nodeName() != QString( "member" ) )
			{
				continue;
			}
			// Luna cluster 3295: the attribute is read before it is known to be a
			// channel index. A missing or non-numeric value names no channel, a
			// negative one wraps to 65535 when cast to mix_ch_t, and a large
			// positive one is handed to allocateChannelsTo(), so a malformed
			// file could allocate a whole mixer. The value is read wide, so a
			// number that overflows an int is refused and reported instead of
			// being dropped without a word. A member must name an EXISTING
			// non-master channel, which is exactly what a saved group ever
			// names, so nothing is skipped that a real project could contain.
			const QString channelAttr = memberDom.attribute( "channel" );
			int channel = 0;
			const ProjectIndex memberKind =
				parseProjectChannelIndex( channelAttr, describedChannels, channel );
			if( memberKind != ProjectIndex::Ok )
			{
				if( memberKind == ProjectIndex::OutOfRange )
				{
					reportUnsupportedIndex( QStringLiteral( "VCA member" ), channelAttr, describedChannels );
				}
				continue;
			}
			if( channel <= 0 )
			{
				continue;
			}
			if( static_cast<std::size_t>( channel ) >= m_mixerChannels.size() )
			{
				continue;
			}
			group->addMember( static_cast<mix_ch_t>( channel ) );
		}
	}

	// Publish the loaded gains (and reset every ungrouped channel to unity).
	refreshGroups();
	hardwareio::loadMixer( _this );  // 0.5.0 F1 (MX-036): the cue masters (absent: the defaults)
	surround::loadMixer( _this );  // 0.5.0 F7 (MX-060): the downmix options (absent: the defaults)

	emit dataChanged();
}


void Mixer::validateChannelName( int index, int oldIndex )
{
	if( m_mixerChannels[index]->m_name == tr( "Channel %1" ).arg( oldIndex ) )
	{
		m_mixerChannels[index]->m_name = tr( "Channel %1" ).arg( index );
	}
}

bool Mixer::isChannelInUse(int index)
{
	// check if the index mixer channel receives audio from any other channel
	if (!m_mixerChannels[index]->m_receives.empty())
	{
		return true;
	}

	// Phase D: a sidechain receiver is in use as well
	if (!m_mixerChannels[index]->m_sidechainReceives.empty())
	{
		return true;
	}

	// check if the destination mixer channel on any instrument or sample track is the index mixer channel
	TrackContainer::TrackList tracks;

	auto& songTracks = Engine::getSong()->tracks();
	auto& patternStoreTracks = Engine::patternStore()->tracks();
	tracks.insert(tracks.end(), songTracks.begin(), songTracks.end());
	tracks.insert(tracks.end(), patternStoreTracks.begin(), patternStoreTracks.end());

	for (const auto t : tracks)
	{
		if (t->type() == Track::Type::Instrument)
		{
			auto inst = dynamic_cast<InstrumentTrack*>(t);
			if (inst->mixerChannelModel()->value() == index)
			{
				return true;
			}
		}
		else if (t->type() == Track::Type::Sample)
		{
			auto strack = dynamic_cast<SampleTrack*>(t);
			if (strack->mixerChannelModel()->value() == index)
			{
				return true;
			}
		}
	}

	return false;
}


} // namespace lmms
