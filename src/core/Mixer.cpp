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

#include <algorithm>

#include <QDomElement>

#include "AudioEngine.h"
#include "AudioEngineWorkerThread.h"
#include "Mixer.h"
#include "Song.h"

#include "InstrumentTrack.h"
#include "MixHelpers.h"
#include "PatternStore.h"
#include "SampleTrack.h"
#include "TrackContainer.h" // For TrackContainer::TrackList typedef

namespace lmms
{


MixerRoute::MixerRoute( MixerChannel * from, MixerChannel * to, float amount,
			bool preFader ) :
	m_from( from ),
	m_to( to ),
	m_amount(amount, 0, 1, 0.001f, nullptr,
			tr("Amount to send from channel %1 to channel %2").arg(m_from->index()).arg(m_to->index())),
	m_preFader( preFader )
{
	//qDebug( "created: %d to %d", m_from->m_channelIndex, m_to->m_channelIndex );
	// create send amount model
}


void MixerRoute::updateName()
{
	m_amount.setDisplayName(
			tr("Amount to send from channel %1 to channel %2").arg(m_from->index()).arg(m_to->index()));
}


MixerSidechainRoute::MixerSidechainRoute( MixerChannel * from, MixerChannel * to,
			float amount, SidechainTapPoint mode ) :
	m_from( from ),
	m_to( to ),
	m_amount(amount, 0, 1, 0.001f, nullptr,
			tr("Sidechain amount from channel %1 to channel %2").arg(m_from->index()).arg(m_to->index())),
	m_mode( mode ),
	// one private intermediate buffer per (sender, receiver) pair: the sender
	// worker writes only this buffer, the receiver worker sums it post-hoc.
	m_intermediate( Engine::audioEngine()->framesPerPeriod(), 2 )
{
	m_intermediate.silenceAllChannels();
}


void MixerSidechainRoute::clearIntermediate()
{
	m_intermediate.silenceAllChannels();
}


void MixerSidechainRoute::updateName()
{
	m_amount.setDisplayName(
			tr("Sidechain amount from channel %1 to channel %2").arg(m_from->index()).arg(m_to->index()));
}



MixerChannel::MixerChannel( int idx, Model * _parent ) :
	m_fxChain( nullptr ),
	m_stillRunning( false ),
	m_peakLeft( 0.0f ),
	m_peakRight( 0.0f ),
	m_buffer( new SampleFrame[Engine::audioEngine()->framesPerPeriod()] ),
	m_bus( &m_buffer, 1, Engine::audioEngine()->framesPerPeriod() ),
	m_hasInput( false ),
	m_muteModel( false, _parent ),
	m_soloModel( false, _parent ),
	m_volumeModel(1.f, 0.f, 2.f, 0.001f, _parent),
	m_name(),
	m_lock(),
	m_queued( false ),
	m_sidechainBuffer( Engine::audioEngine()->framesPerPeriod(), 2 ),
	m_postFaderBuffer( Engine::audioEngine()->framesPerPeriod(), 2 ),
	m_sidechainSends(),
	m_sidechainReceives(),
	m_isBus( false ),
	m_dependenciesMet(0),
	m_channelIndex(idx)
{
	m_bus.silenceAllChannels();
	m_sidechainBuffer.silenceAllChannels();
	m_postFaderBuffer.silenceAllChannels();
}




MixerChannel::~MixerChannel()
{
	delete[] m_buffer;
}


inline void MixerChannel::processed()
{
	for( const MixerRoute * receiverRoute : m_sends )
	{
		if( receiverRoute->receiver()->m_muted == false )
		{
			receiverRoute->receiver()->incrementDeps();
		}
	}
	// Phase D: a sidechain send is also a scheduling edge. The receiver must
	// run after the sender has written the sender's private intermediate
	// buffer, otherwise the post-hoc sum would read a partial or stale tap.
	for( const MixerSidechainRoute * receiverRoute : m_sidechainSends )
	{
		if( receiverRoute->receiver()->m_muted == false )
		{
			receiverRoute->receiver()->incrementDeps();
		}
	}
}

void MixerChannel::incrementDeps()
{
	const auto i = m_dependenciesMet++ + 1;
	if( i >= m_receives.size() + m_sidechainReceives.size() && ! m_queued )
	{
		m_queued = true;
		AudioEngineWorkerThread::addJob( this );
	}
}

void MixerChannel::unmuteForSolo()
{
	m_muteModel.setValue(false);

	// if channel is not master, unmute also every channel it sends to/receives from
	if (!isMaster())
	{
		for (const MixerRoute* sendsRoute : m_sends)
		{
			sendsRoute->receiver()->unmuteSenderForSolo();
		}

		for (const MixerRoute* receiverRoute : m_receives)
		{
			receiverRoute->sender()->unmuteReceiverForSolo();
		}
	}
}

void MixerChannel::unmuteSenderForSolo()
{
	m_muteModel.setValue(false);

	// if channel is not master, unmute every channel it sends to
	if (!isMaster())
	{
		for (const MixerRoute* sendsRoute : m_sends)
		{
			sendsRoute->receiver()->unmuteSenderForSolo();
		}
	}
}


void MixerChannel::unmuteReceiverForSolo()
{
	m_muteModel.setValue(false);

	// if channel is not master, unmute every channel it receives from, and of those, unmute the channels they send to
	if (!isMaster())
	{
		for (const MixerRoute* receiverRoute : m_receives)
		{
			receiverRoute->sender()->unmuteReceiverForSolo();
		}

		for (const MixerRoute* sendsRoute : m_sends)
		{
			sendsRoute->receiver()->unmuteSenderForSolo();
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
		const AudioBuffer& src = route->intermediate();
		const float amount = route->mode() == SidechainTapPoint::PostFaderNoGain
			? 1.0f
			: route->amount()->value();
		const float* const s0 = src.buffer(0).data();
		const float* const s1 = src.buffer(1).data();
		if( amount == 1.0f )
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
		route->clearIntermediate();
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

	if( m_muted == false )
	{
		for( MixerRoute * senderRoute : m_receives )
		{
			MixerChannel * sender = senderRoute->sender();
			FloatModel * sendModel = senderRoute->amount();
			if( ! sendModel ) qFatal( "Error: no send model found from %d to %d", senderRoute->senderIndex(), m_channelIndex );

			if( sender->m_hasInput || sender->m_stillRunning )
			{
				// figure out if we're getting sample-exact input
				ValueBuffer * sendBuf = sendModel->valueBuffer();
				ValueBuffer * volBuf = sender->m_volumeModel.valueBuffer();

				// mix it's output with this one's output
				SampleFrame* ch_buf = sender->m_buffer;

				if( senderRoute->preFader() )
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
					const float v = sender->m_volumeModel.value() * sendModel->value();
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
					const float v = sender->m_volumeModel.value();
					MixHelpers::addMultipliedByBuffer( m_buffer, ch_buf, v, sendBuf, fpp );
				}
				m_bus.quietChannels() &= sender->m_bus.quietChannels(); // mix silence status
				m_hasInput = true;
			}
		}

		// Phase D: pre-FX sidechain tap, taken from the raw channel buffer
		// before the FX chain runs (spec 4.3).
		writeSidechainTaps(SidechainTapPoint::PreFx, fpp);

		// Phase D: fold the per-sender sidechain intermediates of this period
		// into m_sidechainBuffer before our own FX chain reads it.
		sumSidechainInputs(fpp);

		m_stillRunning = m_sidechainReceives.empty()
			? m_fxChain.processAudioBuffer(m_bus)
			: m_fxChain.processAudioBuffer(m_bus, &m_sidechainBuffer);

		// D1: the volume multiply happens after the send loop, producing the
		// post-fader snapshot used by post-fader sidechain taps.
		const float v = m_volumeModel.value();
		updatePostFaderBuffer(v, fpp);

		// Phase D: post-FX sidechain taps (pre-fader = post-FX/pre-volume,
		// post-fader = post-volume).
		writeSidechainTaps(SidechainTapPoint::PreFader, fpp);
		writeSidechainTaps(SidechainTapPoint::PostFader, fpp);
		writeSidechainTaps(SidechainTapPoint::PostFaderNoGain, fpp);

		SampleFrame peakSamples = getAbsPeakValues(m_buffer, fpp);
		m_peakLeft = std::max(m_peakLeft, peakSamples[0] * v);
		m_peakRight = std::max(m_peakRight, peakSamples[1] * v);
	}
	else
	{
		// a muted channel contributes silence to its sidechain receivers: the
		// receiver consumes and clears our intermediates, so clear them here
		// as well to cover the case where the receiver does not run.
		for( MixerSidechainRoute * route : m_sidechainSends )
		{
			route->clearIntermediate();
		}
		m_sidechainBuffer.silenceAllChannels();
		m_postFaderBuffer.silenceAllChannels();
		m_peakLeft = m_peakRight = 0.0f;
	}

	// increment dependency counter of all receivers
	processed();
}



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
	const int index = m_mixerChannels.size();
	// create new channel
	m_mixerChannels.push_back( new MixerChannel( index, this ) );

	// reset channel state
	clearChannel( index );

	// if there is a soloed channel, mute the new track
	if (m_lastSoloed != -1 && m_mixerChannels[m_lastSoloed]->m_soloModel.value())
	{
		m_mixerChannels[index]->m_muteBeforeSolo = m_mixerChannels[index]->m_muteModel.value();
		m_mixerChannels[index]->m_muteModel.setValue(true);
	}

	return index;
}

void Mixer::activateSolo()
{
	for (auto i = std::size_t{1}; i < m_mixerChannels.size(); ++i)
	{
		m_mixerChannels[i]->m_muteBeforeSolo = m_mixerChannels[i]->m_muteModel.value();
		m_mixerChannels[i]->m_muteModel.setValue( true );
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
		m_mixerChannels[soloedChan]->unmuteForSolo();
	} else {
		deactivateSolo();
	}
	m_lastSoloed = soloedChan;
}



void Mixer::deleteChannel( int index )
{
	// channel deletion is performed between mixer rounds
	Engine::audioEngine()->requestChangeInModel();

	// go through every instrument and adjust for the channel index change
	TrackContainer::TrackList tracks;

	auto& songTracks = Engine::getSong()->tracks();
	auto& patternStoreTracks = Engine::patternStore()->tracks();
	tracks.insert(tracks.end(), songTracks.begin(), songTracks.end());
	tracks.insert(tracks.end(), patternStoreTracks.begin(), patternStoreTracks.end());

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

	// if m_lastSoloed was our index, reset it
	if (m_lastSoloed == index) { m_lastSoloed = -1; }
	// if m_lastSoloed is > delete index, it will move left
	else if (m_lastSoloed > index) { --m_lastSoloed; }

	// actually delete the channel
	m_mixerChannels.erase(m_mixerChannels.begin() + index);
	delete ch;

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

	Engine::audioEngine()->doneChangeInModel();
}



void Mixer::moveChannelLeft( int index )
{
	// can't move master or first channel
	if (index <= 1 || static_cast<std::size_t>(index) >= m_mixerChannels.size())
	{
		return;
	}
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
			// simply adjust the amount
			send->amount()->setValue(amount);
			send->setPreFader(routePreFader);
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

	// add us to from's sends
	from->m_sends.push_back(route);

	// add us to to's receives
	to->m_receives.push_back(route);

	// add us to mixer's list
	Engine::mixer()->m_mixerRoutes.push_back(route);
	Engine::audioEngine()->doneChangeInModel();

	return route;
}


int Mixer::createBusChannel()
{
	const int index = createChannel();
	m_mixerChannels[index]->setIsBus(true);
	m_mixerChannels[index]->m_name = tr("Bus %1").arg(index);
	m_mixerChannels[index]->m_volumeModel.setDisplayName(
			m_mixerChannels[index]->m_name + ">" + tr("Volume"));
	m_mixerChannels[index]->m_muteModel.setDisplayName(
			m_mixerChannels[index]->m_name + ">" + tr("Mute"));
	m_mixerChannels[index]->m_soloModel.setDisplayName(
			m_mixerChannels[index]->m_name + ">" + tr("Solo"));
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
			route->amount()->setValue(amount);
			route->setMode(mode);
			return route;
		}
	}

	// a sidechain edge is part of the scheduling graph, so reject routes that
	// would close a cycle (they would deadlock the dependency counter).
	if( checkInfiniteLoop(from, to) )
	{
		return nullptr;
	}

	Engine::audioEngine()->requestChangeInModel();
	auto route = new MixerSidechainRoute(from, to, amount, mode);
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


bool Mixer::checkInfiniteLoop( MixerChannel * from, MixerChannel * to )
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

	// follow sendTo's outputs recursively looking for something that sends
	// to sendFrom. Phase D: sidechain edges are traversed as well, because a
	// sidechain send is a scheduling dependency and a cycle through one would
	// deadlock the dependency counter.
	for (const auto& send : to->m_sends)
	{
		if (checkInfiniteLoop(from, send->receiver()))
		{
			return true;
		}
	}
	for (const auto& send : to->m_sidechainSends)
	{
		if (checkInfiniteLoop(from, send->receiver()))
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
	if (mixerChannel->m_muteModel.value() == false)
	{
		mixerChannel->m_lock.lock();

		MixHelpers::add(mixerChannel->m_bus.bus()[0], bus.bus()[0], bus.frames());
		mixerChannel->m_bus.quietChannels() &= bus.quietChannels(); // mix silence status
		mixerChannel->m_hasInput = true;

		mixerChannel->m_lock.unlock();
	}
}




void Mixer::prepareMasterMix()
{
	m_mixerChannels[0]->m_bus.silenceAllChannels();
}



void Mixer::masterMix( SampleFrame* _buf )
{
	const int fpp = Engine::audioEngine()->framesPerPeriod();

	// add the channels that have no dependencies (no incoming senders, ie.
	// no receives) to the jobqueue. The channels that have receives get
	// added when their senders get processed, which is detected by
	// dependency counting.
	// also instantly add all muted channels as they don't need to care
	// about their senders, and can just increment the deps of their
	// recipients right away.
	AudioEngineWorkerThread::resetJobQueue( AudioEngineWorkerThread::JobQueue::OperationMode::Dynamic );
	for( MixerChannel * ch : m_mixerChannels )
	{
		ch->m_muted = ch->m_muteModel.value();
		if( ch->m_muted ) // instantly "process" muted channels
		{
			ch->processed();
			ch->done();
		}
		else if( ch->m_receives.size() == 0 && ch->m_sidechainReceives.size() == 0 )
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
	ValueBuffer * volBuf = m_mixerChannels[0]->m_volumeModel.valueBuffer();

	if( volBuf )
	{
		for( int f = 0; f < fpp; f++ )
		{
			m_mixerChannels[0]->m_buffer[f][0] *= volBuf->values()[f];
			m_mixerChannels[0]->m_buffer[f][1] *= volBuf->values()[f];
		}
	}

	const float v = volBuf
		? 1.0f
		: m_mixerChannels[0]->m_volumeModel.value();
	MixHelpers::addMultiplied( _buf, m_mixerChannels[0]->m_buffer, v, fpp );

	// clear all channel buffers and
	// reset channel process state
	for( int i = 0; i < numChannels(); ++i)
	{
		m_mixerChannels[i]->m_bus.silenceAllChannels();
		// Phase D: the sidechain input and the post-fader snapshot are
		// per-period scratch data
		m_mixerChannels[i]->m_sidechainBuffer.silenceAllChannels();
		m_mixerChannels[i]->m_postFaderBuffer.silenceAllChannels();
		m_mixerChannels[i]->reset();
		m_mixerChannels[i]->m_queued = false;
		// also reset hasInput
		m_mixerChannels[i]->m_hasInput = false;
		m_mixerChannels[i]->m_dependenciesMet = 0;
	}
}




void Mixer::clear()
{
	while( m_mixerChannels.size() > 1 )
	{
		deleteChannel(1);
	}

	clearChannel(0);
}



void Mixer::clearChannel(mix_ch_t index)
{
	MixerChannel * ch = m_mixerChannels[index];
	ch->m_fxChain.clear();
	ch->m_volumeModel.setValue( 1.0f );
	ch->m_muteModel.setValue( false );
	ch->m_soloModel.setValue( false );
	ch->m_name = ( index == 0 ) ? tr( "Master" ) : tr( "Channel %1" ).arg( index );
	ch->m_volumeModel.setDisplayName( ch->m_name + ">" + tr( "Volume" ) );
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
		ch->m_volumeModel.saveSettings( _doc, mixch, "volume" );
		ch->m_muteModel.saveSettings( _doc, mixch, "muted" );
		ch->m_soloModel.saveSettings( _doc, mixch, "soloed" );
		mixch.setAttribute("num", static_cast<qulonglong>(i));
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
			if (send->preFader()) { sendsDom.setAttribute("prefader", "1"); }
			send->amount()->saveSettings(_doc, sendsDom, "amount");
		}

		// Phase D: sidechain sends. Legacy LMMS ignores these elements, so
		// the sidechain routing is silently dropped on downgrade.
		for (const auto& send : ch->m_sidechainSends)
		{
			QDomElement scDom = _doc.createElement( QString( "sidechain-send" ) );
			mixch.appendChild( scDom );

			scDom.setAttribute("channel", send->receiverIndex());
			scDom.setAttribute("mode", static_cast<int>(send->mode()));
			send->amount()->saveSettings(_doc, scDom, "amount");
		}
	}
}

// make sure we have at least num channels
void Mixer::allocateChannelsTo(int num)
{
	if (num <= 0) { return; }
	while (static_cast<std::size_t>(num) > m_mixerChannels.size() - 1)
	{
		createChannel();

		// delete the default send to master
		deleteChannelSend( m_mixerChannels.size()-1, 0 );
	}
}


void Mixer::loadSettings( const QDomElement & _this )
{
	clear();
	QDomNode node = _this.firstChild();

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
			const int busNum = mixch.attribute( "num" ).toInt();
			allocateChannelsTo( busNum );
			m_mixerChannels[busNum]->setIsBus( true );
		}
	}

	while( ! node.isNull() )
	{
		QDomElement mixch = node.toElement();

		// index of the channel we are about to load
		int num = mixch.attribute( "num" ).toInt();

		// allocate enough channels
		allocateChannelsTo( num );

		m_mixerChannels[num]->m_volumeModel.loadSettings( mixch, "volume" );
		m_mixerChannels[num]->m_muteModel.loadSettings( mixch, "muted" );
		m_mixerChannels[num]->m_soloModel.loadSettings( mixch, "soloed" );
		m_mixerChannels[num]->m_name = mixch.attribute( "name" );
		if (mixch.hasAttribute("color"))
		{
			m_mixerChannels[num]->setColor(QColor{mixch.attribute("color")});
		}

		m_mixerChannels[num]->m_fxChain.restoreState( mixch.firstChildElement(
			m_mixerChannels[num]->m_fxChain.nodeName() ) );

		// mixer sends
		QDomNodeList chData = mixch.childNodes();
		for (auto i = 0; i < chData.length(); ++i)
		{
			QDomElement chDataItem = chData.at(i).toElement();
			if( chDataItem.nodeName() == QString( "send" ) )
			{
				int sendTo = chDataItem.attribute( "channel" ).toInt();
				allocateChannelsTo( sendTo ) ;
				const bool preFader =
					chDataItem.attribute( "prefader" ).toInt() != 0;
				MixerRoute * mxr = createChannelSend( num, sendTo, 1.0f, preFader );
				if( mxr ) mxr->amount()->loadSettings( chDataItem, "amount" );
			}
			else if( chDataItem.nodeName() == QString( "sidechain-send" ) )
			{
				int sendTo = chDataItem.attribute( "channel" ).toInt();
				allocateChannelsTo( sendTo );
				const int mode = chDataItem.attribute( "mode" ).toInt();
				const SidechainTapPoint tap =
					( mode >= 0 && mode <= 3 )
						? static_cast<SidechainTapPoint>( mode )
						: SidechainTapPoint::PreFader;
				MixerSidechainRoute * mxr =
					createSidechainSend( num, sendTo, 1.0f, tap );
				if( mxr ) mxr->amount()->loadSettings( chDataItem, "amount" );
			}
		}



		node = node.nextSibling();
	}

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
