/*
 * Mixer.h - effect-mixer for LMMS
 *
 * Copyright (c) 2008-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#ifndef LMMS_MIXER_H
#define LMMS_MIXER_H

#include "AudioBus.h"
#include "AudioBuffer.h"
#include "EffectChain.h"
#include "JournallingObject.h"
#include "Model.h"
#include "ThreadableJob.h"

#include <atomic>
#include <optional>
#include <QColor>

namespace lmms
{


class MixerRoute;
using MixerRouteVector = std::vector<MixerRoute*>;

class MixerSidechainRoute;
using MixerSidechainRouteVector = std::vector<MixerSidechainRoute*>;

//! Tap point for a sidechain send (Phase D, spec 4.3; Reaper I_SENDMODE 0-3).
enum class SidechainTapPoint : int
{
	PostFader = 0, //!< after m_volumeModel scaling, same as a regular send
	PreFx = 1, //!< raw channel buffer, before the FX chain
	PreFader = 2, //!< after the FX chain, before the volume multiply
	PostFaderNoGain = 3, //!< after the fader, send amount forced to 1.0
};

class MixerChannel : public ThreadableJob
{
public:
	MixerChannel(int idx, Model* _parent);
	virtual ~MixerChannel();

	EffectChain m_fxChain;

	// set to true if any effect in the channel is enabled and running
	bool m_stillRunning;

	float m_peakLeft;
	float m_peakRight;
	//! Interleaved stereo buffer (one track channel pair) this channel is mixed into
	SampleFrame* m_buffer;
	//! Bus view over m_buffer, used for effect processing and mixing
	AudioBus m_bus;
	//! Set to true when input is fed from mixToChannel or a child channel
	bool m_hasInput;
	bool m_muteBeforeSolo;
	BoolModel m_muteModel;
	BoolModel m_soloModel;
	FloatModel m_volumeModel;
	QString m_name;
	QMutex m_lock;
	bool m_queued; // are we queued up for rendering yet?
	bool m_muted; // are we muted? updated per period so we don't have to call m_muteModel.value() twice

	// pointers to other channels that this one sends to
	MixerRouteVector m_sends;

	// pointers to other channels that send to this one
	MixerRouteVector m_receives;

	// ---- Phase D (task #587): sidechain sends + parallel buses ----

	//! Sidechain input for this period: the post-hoc sum of the per-sender
	//! intermediates of m_sidechainReceives. Cleared after each period.
	AudioBuffer m_sidechainBuffer;
	//! m_buffer with the channel volume multiply applied (D1: the multiply
	//! happens after the send loop). Used as the post-fader sidechain tap.
	AudioBuffer m_postFaderBuffer;
	//! Outgoing sidechain sends owned by this channel (routes hold the
	//! per-sender intermediate buffers written during our processing).
	MixerSidechainRouteVector m_sidechainSends;
	//! Incoming sidechain sends; each route contributes its own intermediate.
	MixerSidechainRouteVector m_sidechainReceives;
	//! True for parallel bus channels: they never receive instrument output
	//! and their incoming sends default to pre-fader.
	bool m_isBus;

	int index() const { return m_channelIndex; }
	void setIndex(int index) { m_channelIndex = index; }

	bool isMaster() { return m_channelIndex == 0; }

	bool isBus() const { return m_isBus; }
	void setIsBus(bool bus) { m_isBus = bus; }

	bool requiresProcessing() const override { return true; }
	void unmuteForSolo();
	void unmuteSenderForSolo();
	void unmuteReceiverForSolo();

	//! Sum the per-sender sidechain intermediates into m_sidechainBuffer and
	//! consume (clear) them. Called once per period before the FX chain.
	void sumSidechainInputs(const f_cnt_t frames);
	//! Produce m_postFaderBuffer = m_buffer * fader (D1).
	void updatePostFaderBuffer(const float volume, const f_cnt_t frames);
	//! Copy this channel's signal at the given tap point into the private
	//! intermediate buffer of every outgoing sidechain send using that mode.
	void writeSidechainTaps(const SidechainTapPoint point, const f_cnt_t frames);

	auto color() const -> const std::optional<QColor>& { return m_color; }
	void setColor(const std::optional<QColor>& color) { m_color = color; }

	std::atomic_size_t m_dependenciesMet;
	void incrementDeps();
	void processed();

private:
	void doProcessing() override;
	int m_channelIndex;
	std::optional<QColor> m_color;
};

class MixerRoute : public QObject
{
	Q_OBJECT
public:
	MixerRoute( MixerChannel * from, MixerChannel * to, float amount,
			bool preFader = false );
	~MixerRoute() override = default;

	mix_ch_t senderIndex() const
	{
		return m_from->index();
	}

	mix_ch_t receiverIndex() const
	{
		return m_to->index();
	}

	FloatModel * amount()
	{
		return &m_amount;
	}

	MixerChannel * sender() const
	{
		return m_from;
	}

	MixerChannel * receiver() const
	{
		return m_to;
	}

	//! Pre-fader sends (the native default for parallel buses) are taken
	//! before the sender's volume multiply; post-fader sends keep the legacy
	//! combined sender-volume * amount scaling (Phase D, D1).
	bool preFader() const
	{
		return m_preFader;
	}

	void setPreFader(bool preFader)
	{
		m_preFader = preFader;
	}

	void updateName();

	private:
		MixerChannel * m_from;
		MixerChannel * m_to;
		FloatModel m_amount;
		bool m_preFader;
};

//! A sidechain send (Phase D, task #587). Unlike MixerRoute, sidechain audio
//! never mixes into the receiver's m_buffer: the sender writes a copy of its
//! signal at the route's tap point into this route's private intermediate
//! buffer, and the receiver performs a post-hoc sum of the intermediates of
//! all its incoming sidechain routes into m_sidechainBuffer (spec 5.4
//! strategy (a)). One intermediate per (sender, receiver) pair means parallel
//! senders never write the same buffer.
class MixerSidechainRoute : public QObject
{
	Q_OBJECT
public:
	MixerSidechainRoute( MixerChannel * from, MixerChannel * to, float amount,
			SidechainTapPoint mode );

	mix_ch_t senderIndex() const
	{
		return m_from->index();
	}

	mix_ch_t receiverIndex() const
	{
		return m_to->index();
	}

	FloatModel * amount()
	{
		return &m_amount;
	}

	MixerChannel * sender() const
	{
		return m_from;
	}

	MixerChannel * receiver() const
	{
		return m_to;
	}

	SidechainTapPoint mode() const
	{
		return m_mode;
	}

	void setMode(SidechainTapPoint mode)
	{
		m_mode = mode;
	}

	//! Private per-sender intermediate buffer. Written only by the sender's
	//! worker, read and consumed only by the receiver's worker.
	AudioBuffer& intermediate()
	{
		return m_intermediate;
	}

	void clearIntermediate();

	void updateName();

	private:
		MixerChannel * m_from;
		MixerChannel * m_to;
		FloatModel m_amount;
		SidechainTapPoint m_mode;
		AudioBuffer m_intermediate;
};


class LMMS_EXPORT Mixer : public Model, public JournallingObject
{
	Q_OBJECT
public:
	Mixer();
	~Mixer() override;

	void mixToChannel(const AudioBus& bus, mix_ch_t channel);

	void prepareMasterMix();
	void masterMix( SampleFrame* _buf );

	void saveSettings( QDomDocument & _doc, QDomElement & _parent ) override;
	void loadSettings( const QDomElement & _this ) override;

	QString nodeName() const override
	{
		return "mixer";
	}

	MixerChannel * mixerChannel( int _ch )
	{
		return m_mixerChannels[_ch];
	}

	// make the output of channel fromChannel go to the input of channel toChannel
	// it is safe to call even if the send already exists
	// preFader: tap the sender before its volume multiply (default for buses)
	MixerRoute * createChannelSend(mix_ch_t fromChannel, mix_ch_t toChannel,
					   float amount = 1.0f, bool preFader = false);
	MixerRoute * createRoute( MixerChannel * from, MixerChannel * to, float amount,
				  bool preFader = false );

	// delete the connection made by createChannelSend
	void deleteChannelSend(mix_ch_t fromChannel, mix_ch_t toChannel);
	void deleteChannelSend( MixerRoute * route );

	// ---- Phase D (task #587): sidechain sends + parallel buses ----

	//! Create a sidechain send. Sidechain audio is delivered through a
	//! private per-route intermediate buffer and summed by the receiver.
	//! Returns nullptr when the route would close a scheduling cycle.
	MixerSidechainRoute * createSidechainSend(mix_ch_t fromChannel,
			mix_ch_t toChannel, float amount = 1.0f,
			SidechainTapPoint mode = SidechainTapPoint::PostFader);
	void deleteSidechainSend(mix_ch_t fromChannel, mix_ch_t toChannel);
	void deleteSidechainSend( MixerSidechainRoute * route );

	//! Find an existing sidechain send, or nullptr.
	MixerSidechainRoute * channelSidechainSend(mix_ch_t fromChannel,
			mix_ch_t toChannel);

	//! Create a parallel bus channel and return its index. A bus never
	//! receives instrument output and its incoming sends default to
	//! pre-fader; its own output is routed by its regular sends.
	int createBusChannel();

	//! True if the channel at the given index is a parallel bus.
	bool isBusChannel(int channel) const;

	//! Cycle check over the union of regular and sidechain edges, so a
	//! sidechain edge can never deadlock the dependency counter.
	bool checkInfiniteLoop( MixerChannel * from, MixerChannel * to );

	// determine if adding a send from sendFrom to
	// sendTo would result in an infinite mixer loop.
	bool isInfiniteLoop(mix_ch_t fromChannel, mix_ch_t toChannel);

	// return the FloatModel of fromChannel sending its output to the input of
	// toChannel. NULL if there is no send.
	FloatModel * channelSendModel(mix_ch_t fromChannel, mix_ch_t toChannel);

	// add a new channel to the mixer.
	// returns the index of the channel that was just added
	int createChannel();

	// delete a channel from the mixer.
	void deleteChannel(int index);

	// delete all the mixer channels except master and remove all effects
	void clear();

	// re-arrange channels
	void moveChannelLeft(int index);
	void moveChannelRight(int index);

	// reset a channel's name, fx, sends, etc
	void clearChannel(mix_ch_t channelIndex);

	// rename channels when moving etc. if they still have their original name
	void validateChannelName( int index, int oldIndex );

	// check if the index channel receives audio from any other channel
	// or from any instrument or sample track
	bool isChannelInUse(int index);

	void toggledSolo();
	void activateSolo();
	void deactivateSolo();

	inline mix_ch_t numChannels() const
	{
		return m_mixerChannels.size();
	}

	MixerRouteVector m_mixerRoutes;
	MixerSidechainRouteVector m_mixerSidechainRoutes;

private:
	// the mixer channels in the mixer. index 0 is always master.
	std::vector<MixerChannel*> m_mixerChannels;

	// make sure we have at least num channels
	void allocateChannelsTo(int num);

	int m_lastSoloed;
} ;


} // namespace lmms

#endif // LMMS_MIXER_H
