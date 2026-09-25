/*
 * AudioPortsModel.h - The model for audio ports used by the
 *                     pin connector
 *
 * Copyright (c) 2025 Dalton Messmer <messmer.dalton/at/gmail.com>
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

#ifndef LMMS_AUDIO_PORTS_MODEL_H
#define LMMS_AUDIO_PORTS_MODEL_H

#include <algorithm>
#include <bitset>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <optional>
#include <type_traits>
#include <vector>

#include "AutomatableModel.h"
#include "LmmsTypes.h"
#include "SerializingObject.h"
#include "lmms_constants.h"
#include "lmms_export.h"

#ifdef LMMS_TESTING
class AudioPortsTest;
#endif

namespace lmms
{

namespace gui
{

class PinConnector;

} // namespace gui


/**
 * The model for audio ports used by the pin connector.
 *
 * Contains:
 * - Pin connections for audio routing in/out of an audio processor
 * - Audio port channel counts
 * - Audio port channel names
 */
class LMMS_EXPORT AudioPortsModel
	: public Model
	, public SerializingObject
{
	Q_OBJECT

public:
	using PinMap = std::vector<std::vector<bool>>;

	//! A processor's input or output connections and other info
	class Matrix
	{
	public:
		explicit Matrix(AudioPortsModel* parent, bool isOutput)
			: m_isOutput{isOutput}
			, m_parent{parent}
		{
		}

		auto pins() const -> const PinMap& { return m_pins; }
		auto pins(ch_cnt_t trackChannel) -> const auto& { return m_pins[trackChannel]; }

		auto channelCount() const -> ch_cnt_t { return m_channelCount; }
		auto trackChannelCount() const -> ch_cnt_t { return m_pins.size(); }

		auto channelName(ch_cnt_t channel) const -> QString;

		auto enabled(ch_cnt_t trackChannel, ch_cnt_t processorChannel) const -> bool
		{
			return m_pins[trackChannel][processorChannel];
		}

		//! Sets a pin connector pin, updates the cache, then emits a dataChanged signal if needed
		void setPin(ch_cnt_t trackChannel, ch_cnt_t processorChannel, bool value);

		/**
		 * Sets a pin connector pin without updating the cache or emitting a dataChanged signal.
		 * Meant for setting multiple pins in one batch efficiently.
		 *
		 * Remember to update the cache and emit the dataChanged signal afterwards!
		 */
		void setPinBatch(ch_cnt_t trackChannel, ch_cnt_t processorChannel, bool value)
		{
			m_pins[trackChannel][processorChannel] = value;
		}

		auto isOutput() const -> bool { return m_isOutput; }

		/**
		 * @brief Fast lookup for which track channels are used by the processor.
		 *
		 * @returns a bitset whose index represents a track channel and whose value at that index tells
		 *          whether the track channel is connected to at least one processor channel.
		 *
		 * For the input matrix, this means which track channels are routed to one or more processor input.
		 * For the output matrix, this means which track channels have one or more processor outputs routed to them.
		 */
		auto usedTrackChannels() const -> const std::bitset<MaxTrackChannels>& { return m_usedTrackChannels; }

		/**
		 * @brief Fast lookup for which processor channels are used by the track channels.
		 *
		 * @returns a dynamic bitset whose index represents a processor channel and whose value at that index tells
		 *          whether the processor channel is connected to at least one track channel.
		 *
		 * For the input matrix, this means which processor inputs have one or more track channel routed to them.
		 * For the output matrix, this means which processor outputs are routed to one or more track channel.
		 */
		auto usedChannels() const -> const std::vector<bool>& { return m_usedChannels; }

		friend class AudioPortsModel;

	private:
		void setTrackChannelCount(ch_cnt_t count);
		void setChannelCount(ch_cnt_t count);

		void setDefaultConnections();

		void updateUsedTrackChannels(ch_cnt_t channel);
		void updateUsedTrackChannels();

		void updateUsedChannels(ch_cnt_t channel);
		void updateUsedChannels();

		void updateAllUsedChannels();

		/**
		 * BUG-PINBOUNDS: set the pin a persisted `cN_M` attribute name names, or do
		 * nothing when the name is malformed or names a channel outside this matrix.
		 *
		 * A project file is user-editable input. This never throws and never indexes
		 * with a value the file supplied: the name must be `c`-prefixed, must carry
		 * digits on both sides of the `_`, and both one-based indices must be inside
		 * the matrix. Anything else is dropped, and the rest of the file still loads.
		 */
		void setPinFromAttributeName(const QString& name)
		{
			if (name.size() < 3 || name.at(0) != QLatin1Char('c')) { return; }
			const auto pos = name.indexOf('_');
			if (pos <= 1) { return; }

			bool trackOk = false;
			bool processorOk = false;
			const auto track = name.mid(1, pos - 1).toInt(&trackOk);
			const auto processor = name.mid(pos + 1).toInt(&processorOk);
			if (!trackOk || !processorOk || track < 1 || track > static_cast<int>(m_pins.size())
				|| processor < 1 || processor > static_cast<int>(m_channelCount))
			{
				return;
			}

			setPinBatch(static_cast<ch_cnt_t>(track - 1), static_cast<ch_cnt_t>(processor - 1), true);
		}

		void saveSettings(QDomDocument& doc, QDomElement& elem) const;
		void loadSettings(const QDomElement& elem);

		PinMap m_pins;
		ch_cnt_t m_channelCount = 0;
		const bool m_isOutput = false;
		AudioPortsModel* m_parent = nullptr;

		std::bitset<MaxTrackChannels> m_usedTrackChannels;
		std::vector<bool> m_usedChannels;
	};

	AudioPortsModel(bool isInstrument, Model* parent = nullptr);
	AudioPortsModel(ch_cnt_t channelCountIn, ch_cnt_t channelCountOut, bool isInstrument, Model* parent = nullptr);

	/**
	 * Getters
	 */
	auto in() -> Matrix& { return m_in; }
	auto in() const -> const Matrix& { return m_in; }
	auto out() -> Matrix& { return m_out; }
	auto out() const -> const Matrix& { return m_out; }
	auto trackChannelCount() const -> ch_cnt_t { return m_totalTrackChannels; }

	/**
	 * The model is initialized once the number of in/out channels are known.
	 *
	 * Audio processors with a dynamic number of input or output channels must manually set
	 * the channel counts (i.e. with `setChannelCounts()`) to initialize the model.
	 */
	auto initialized() const -> bool { return m_in.m_channelCount != 0 || m_out.m_channelCount != 0; }

	auto isInstrument() const -> bool { return m_isInstrument; }

	/**
	 * Setters
	 */
	void setAllChannelCounts(ch_cnt_t trackChannels, ch_cnt_t inCount, ch_cnt_t outCount);
	void setTrackChannelCount(ch_cnt_t count);
	void setChannelCounts(ch_cnt_t inCount, ch_cnt_t outCount);
	void setChannelCountIn(ch_cnt_t inCount);
	void setChannelCountOut(ch_cnt_t outCount);

	/**
	 * SerializingObject implementation
	 */
	void saveSettings(QDomDocument& doc, QDomElement& elem) override;
	void loadSettings(const QDomElement& elem) override;
	auto nodeName() const -> QString override { return "pins"; }

	virtual auto instantiateView() const -> gui::PinConnector*;

	auto getChannelCountText() const -> QString;

	/**
	 * Caches the highest indexed track channel in use, rounded up to a whole L/R pair
	 * (the unit AudioBus and AudioPorts walk), so that the audio ports router can loop
	 * over [0, trackChannelsUpperBound) rather than [0, totalTrackChannels).
	 *
	 * Recomputed from the pin matrices whenever pins or channel counts change
	 * (updateTrackChannelsUpperBound). It is never below DEFAULT_CHANNELS.
	 */
	auto trackChannelsUpperBound() const -> ch_cnt_t { return m_trackChannelsUpperBound; }

	/**
	 * Any processor with 2-channel interleaved buffers connected to the track channels in the default
	 * pin configuration (L --> L, R --> R) can be connected directly without any complicated routing.
	 * This greatly simplifies the job of `AudioPorts::Router` and should bring a significant performance boost.
	 *
	 * In theory, the performance when this optimization is enabled (which should be the case for most processors
	 * most of the time) should be about the same as if the processor did not use the pin connector at all.
	 *
	 * This variable caches whether that optimization is currently possible.
	 * When std::nullopt, the optimization is disabled, otherwise the value equals the index of the track channel
	 * pair currently routed to/from the processor.
	 */
	auto directRouting() const -> std::optional<ch_cnt_t> { return m_directRouting; }

#ifdef LMMS_TESTING
	friend class ::AudioPortsTest;
#endif

signals:
	//! Called when channel counts change (whether audio processor or track channel counts)
	//void propertiesChanged(); // from Model

protected:
	/**
	 * Called when channel counts or the frame count are changing.
	 *
	 * The parameters will contain the new values, but `in().channelCount()` and `out().channelCount()`
	 * will still return the old values until after this method is called.
	 */
	virtual void bufferPropertiesChanging(ch_cnt_t inChannels, ch_cnt_t outChannels, f_cnt_t frames) = 0;

	/**
	 * Audio port implementations can override this to provide custom channel names,
	 * otherwise the default channel names are used.
	 */
	virtual auto channelName(ch_cnt_t channel, bool isOutput) const -> QString;

private:
	auto setTrackChannelCountImpl(ch_cnt_t count) -> bool;
	auto setProcessorChannelCountsImpl(ch_cnt_t inCount, ch_cnt_t outCount, bool silent) -> bool;

	void updateDirectRouting();

	/**
	 * BUG-CHBOUND: recompute the cached highest indexed track channel in use from the
	 * pin matrices, rounded up to a whole L/R pair, and never below DEFAULT_CHANNELS.
	 *
	 * The matrices are read rather than the used-channel cache: a cache entry above
	 * the current track channel count is stale until it is refreshed, and a pair the
	 * user removed must stay removed. This used to be assigned std::min(...) only, so
	 * it could never grow past its DEFAULT_CHANNELS initializer and the router and
	 * AudioBus skipped every pair above the first one.
	 */
	void updateTrackChannelsUpperBound()
	{
		ch_cnt_t bound = DEFAULT_CHANNELS;
		for (ch_cnt_t tc = 0; tc < m_totalTrackChannels; ++tc)
		{
			const auto used = [tc](const Matrix& matrix) {
				const auto& pins = matrix.pins()[tc];
				return std::any_of(pins.begin(), pins.end(), [](bool pin) { return pin; });
			};
			if (used(m_in) || used(m_out))
			{
				bound = std::max(bound, static_cast<ch_cnt_t>(tc + 1));
			}
		}

		m_trackChannelsUpperBound = static_cast<ch_cnt_t>(bound + bound % 2);
	}

	/**
	 * BUG-PINBOUNDS: parse an integer that came out of a project file and range-check
	 * it. False - and no write - for a value that is not a number or is outside
	 * [min, max]. Used for the port counts, which size the matrices and are therefore
	 * just as much untrusted input as the pin names.
	 */
	static auto parseAttributeInRange(const QString& text, int min, int max, ch_cnt_t* out) -> bool
	{
		bool ok = false;
		const auto value = text.toInt(&ok);
		if (!ok || value < min || value > max) { return false; }
		*out = static_cast<ch_cnt_t>(value);
		return true;
	}

	Matrix m_in{this, false}; //!< LMMS --> audio processor
	Matrix m_out{this, true}; //!< audio processor --> LMMS

	// TODO: When full routing is added, get LMMS channel counts from bus or audio router class
	ch_cnt_t m_totalTrackChannels = DEFAULT_CHANNELS;

	/**
	 * This needs to be known because the default connections (and view?) for instruments with sidechain
	 * inputs is different from effects, even though they may both have the same channel counts.
	 */
	const bool m_isInstrument = false;

	/*
	 * The following are cached values primarily meant to improve `AudioPorts::Router` performance
	 */

	ch_cnt_t m_trackChannelsUpperBound = DEFAULT_CHANNELS;
	std::optional<ch_cnt_t> m_directRouting;
};

} // namespace lmms

#endif // LMMS_AUDIO_PORTS_MODEL_H
