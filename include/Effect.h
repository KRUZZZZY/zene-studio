/*
 * Effect.h - base class for effects
 *
 * Copyright (c) 2006-2007 Danny McRae <khjklujn/at/users.sourceforge.net>
 * Copyright (c) 2006-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#ifndef LMMS_EFFECT_H
#define LMMS_EFFECT_H

#include <algorithm>
#include <limits>
#include <span>

#include "AudioEngine.h"
#include "AutomatableModel.h"
#include "DeltaTap.h"
#include "DeviceOptions.h"
#include "Engine.h"
#include "Plugin.h"
#include "TempoSyncKnobModel.h"

namespace lmms
{

class AudioBuffer;
class AudioBus;
class AudioPortsModel;
class EffectChain;
class EffectControls;

namespace gui
{

class EffectView;

} // namespace gui


class LMMS_EXPORT Effect : public Plugin
{
	Q_OBJECT
public:
	Effect( const Plugin::Descriptor * _desc,
			Model * _parent,
			const Descriptor::SubPluginFeatures::Key * _key );

	void saveSettings( QDomDocument & _doc, QDomElement & _parent ) override;
	void loadSettings( const QDomElement & _this ) override;

	inline QString nodeName() const override
	{
		return "effect";
	}

	/*! The effect's STABLE ID - the number in `fx-<n>` (SPEC-stable-ids.md
	 *  slice 2). Assigned once, in the constructor, and never changed while the
	 *  effect is alive; written to the project file as an `id` ATTRIBUTE on the
	 *  effect's own element and taken back by loadSettings, so a cached `fx-<n>`
	 *  still names this effect after a sibling effect is inserted, deleted,
	 *  reordered or undone - and after a save/open cycle.
	 */
	int id() const { return m_id; }
	//! Overrides the constructor's id with a file's value. Raises the project
	//! counter past \a id so a retired number is never handed out again.
	void setId(int id);

	//! Returns true if audio was processed and should continue being processed
	bool processAudioBuffer(AudioBuffer& inOut);

	//! Returns true if audio was processed and should continue being processed.
	//! Multi-channel variant of processAudioBuffer(). Effects which have audio
	//! ports override this method in order to route their ports on the bus.
	virtual bool processAudioBuffer(AudioBus& inOut);

	//! Returns the audio ports model of this effect or nullptr if it does not
	//! have any audio ports (e.g. legacy effects)
	virtual auto audioPortsModel() const -> const AudioPortsModel*
	{
		return nullptr;
	}

	inline bool isOkay() const
	{
		return m_okay;
	}

	inline void setOkay( bool _state )
	{
		m_okay = _state;
	}

	//! @returns true if the processing outputted corrupted audio (infs/nans).
	bool isCorrupted() const { return m_corrupted.load(std::memory_order_relaxed); }

	//! "Awake" means the effect has not been put to sleep by auto-quit
	bool isAwake() const
	{
		return m_awake;
	}

	inline bool isEnabled() const
	{
		return m_enabledModel.value();
	}

	//! Drive the same On/Off control the rack's LED checkbox drives (SPEC A11:
	//! one action, one implementation). Added for the agent control surface
	//! (plugin.bypass / dsp.get_state read-back); the model is journalled, so
	//! the change is reversible through the ProjectJournal.
	void setEnabled( bool enabled )
	{
		m_enabledModel.setValue( enabled );
	}

	inline f_cnt_t timeout() const
	{
		const float samples = Engine::audioEngine()->outputSampleRate() * m_autoQuitModel.value() / 1000.0f;
		// 0.5.0 (engine audit E6): a device that declares its own tail - a reverb, a delay -
		// keeps running at least that long after its input falls silent, and one with an
		// infinite tail never stops on silence; before, the Decay setting alone cut the tail.
		const int tail = tailFrames();
		if (tail < 0) { return std::numeric_limits<f_cnt_t>::max(); }
		return 1 + ( std::max( static_cast<int>( samples ), tail ) / Engine::audioEngine()->framesPerPeriod() );
	}

	//! Frames the device keeps sounding after its input stops, as the device itself declares
	//! it (a hosted plug-in's tail); -1 for an infinite tail. Read on the audio thread, so an
	//! override returns a cached value.
	virtual int tailFrames() const
	{
		return 0;
	}

	//! 0.5.0 L08 (MX-130): how far the device is turning its signal down right now, in dB (0 for a
	//! device that does not compress). Read on the audio thread after processing; a dynamics device
	//! overrides it with the value its own meter shows.
	virtual float gainReductionDb() const
	{
		return 0.f;
	}

	inline float wetLevel() const
	{
		return m_wetDryModel.value();
	}

	inline float dryLevel() const
	{
		return 1.0f - m_wetDryModel.value();
	}

	inline bool dontRun() const
	{
		return m_noRun;
	}

	//! Mark the effect as not processing (bypass). Defined out of line so the
	//! owning chain can drop the effect's latency from the PDC graph (#605).
	void setDontRun( bool _state );

	bool isProcessingAudio() const
	{
		return isEnabled() && isAwake() && isOkay() && !dontRun();
	}

	inline TempoSyncKnobModel* autoQuitModel()
	{
		return &m_autoQuitModel;
	}

	bool autoQuitEnabled() const
	{
		return m_autoQuitEnabled;
	}

	EffectChain * effectChain() const
	{
		return m_parent;
	}

	//! Phase D: set the owning chain when the effect is appended, so that
	//! sidechainBuffer() also works for directly-constructed effects
	//! (Effect::instantiate() sets it for factory-created ones).
	void setEffectChain( EffectChain * _chain )
	{
		m_parent = _chain;
	}

	//! Sidechain input for the current processing block, or nullptr when the
	//! owning channel has no incoming sidechain sends (Phase D, spec 4.2).
	//! Delivered through the EffectChain back-pointer so the processImpl()
	//! signature stays unchanged.
	const AudioBuffer* sidechainBuffer() const;

	virtual EffectControls * controls() = 0;

	//! Frames of latency this effect adds to the signal path.
	//! Latency-compensating hosts (see docs/specs/SPEC-dynamic-routing.md) query
	//! this to align parallel paths. Defaults to 0 for zero-latency effects.
	virtual int latencyFrames() const
	{
		return 0;
	}

	//! 0.5.0 L10 (W3): the options a user set on this device - parameter aliases, folding, editor
	//! scale, compatibility (include/DeviceOptions.h). Saved with the device, only when set.
	DeviceOptions& deviceOptions() { return m_deviceOptions; }
	const DeviceOptions& deviceOptions() const { return m_deviceOptions; }
	//! 0.5.0 L10 (MX-080): the switch that plays this device's delta (output minus input) instead
	//! of its output - a monitor switch, not saved. The chain renders the device through it.
	DeltaTap& deltaTap() { return m_deltaTap; }
	const DeltaTap& deltaTap() const { return m_deltaTap; }

	//! MX-099: the latency delay compensation counts for this device - the user's override when one
	//! is set, else the device's own latencyFrames().
	int compensatedLatencyFrames() const
	{
		return m_deviceOptions.latencyOverride >= 0 ? m_deviceOptions.latencyOverride : latencyFrames();
	}

	static Effect * instantiate( const QString & _plugin_name,
				Model * _parent,
				Descriptor::SubPluginFeatures::Key * _key );


protected:
	enum class ProcessStatus
	{
		//! Unconditionally continue processing
		Continue,

		//! Calculate the RMS out sum and call `checkGate` to determine whether to stop processing
		ContinueIfNotQuiet,

		//! Do not continue processing
		Sleep
	};

	/**
	 * The main audio processing method that runs when plugin is awake and running
	 */
	virtual ProcessStatus processImpl(SampleFrame* buf, const f_cnt_t frames) = 0;

	/**
	 * Optional method that runs instead of `processImpl` when an effect
	 * is awake but not running.
	 */
	virtual void processBypassedImpl() {}


	gui::PluginView* instantiateView( QWidget * ) override;

	void goToSleep()
	{
		m_quietBufferCount = 0;
		m_awake = false;
	}

	void wakeUp()
	{
		m_quietBufferCount = 0;
		m_awake = true;
	}

	virtual void onEnabledChanged() {}

	/**
	 * If auto-quit is enabled ("Keep effects running even without input" setting is disabled),
	 * after "decay" ms of the output buffer remaining below the silence threshold, the effect is
	 * turned off and won't be processed again until it receives new audio input.
	 */
	void handleAutoQuit(bool silentOutput);

private:
	EffectChain * m_parent;

	/*! The effect's stable id: the number in `fx-<n>`. Assigned once, in the
	 *  constructor, through ProjectIds - the same project-scoped counter the
	 *  track, clip, note and channel ids come from, so `next-id` on the project
	 *  root covers all of them - and replaced by the file's value on a project
	 *  load (see saveSettings / loadSettings). */
	int m_id;

	bool m_okay;
	bool m_noRun;
	bool m_awake;
	std::atomic<bool> m_corrupted = false;

	//! The number of consecutive periods where output buffers remain below the silence threshold
	f_cnt_t m_quietBufferCount = 0;

	BoolModel m_enabledModel;
	FloatModel m_wetDryModel;
	TempoSyncKnobModel m_autoQuitModel;

	bool m_autoQuitEnabled = false;

	DeviceOptions m_deviceOptions;
	DeltaTap m_deltaTap;

	friend class gui::EffectView;
	friend class EffectChain;

} ;

using EffectKey = Effect::Descriptor::SubPluginFeatures::Key;
using EffectKeyList = Effect::Descriptor::SubPluginFeatures::KeyList;

} // namespace lmms

#endif // LMMS_EFFECT_H
