/*
 * SfzPlayer.h - the built-in SFZ instrument: loads .sfz files and plays them with sfizz
 *
 * This file is part of Zene Studio, derived from LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#ifndef LMMS_SFZ_PLAYER_H
#define LMMS_SFZ_PLAYER_H

#include <array>
#include <atomic>
#include <cstdint>

#include <QString>

#include "AutomatableModel.h"
#include "ComboBoxModel.h"
#include "Instrument.h"
#include "MpscQueue.h"
#include "RtPublished.h"
#include "SfzSynth.h"

namespace lmms
{

namespace gui { class SfzPlayerView; }

/*! The SFZ instrument (Q050-484).
 *
 *  An .sfz file is loaded - with its samples, velocity layers, round robins and key switches - into an sfizz
 *  synthesizer. MIDI reaches it through handleMidiEvent() from any thread and is queued; the render thread
 *  plays the queued events at their frame offsets and renders the synth. Loading builds a complete new synth on
 *  the control thread and publishes it to the render thread in one swap, so audio never waits for a file: the
 *  render path allocates nothing, takes no lock and reads no file (sfizz streams the samples' tails from its own
 *  worker threads).
 */
class SfzPlayer : public Instrument
{
	Q_OBJECT
public:
	explicit SfzPlayer(InstrumentTrack* track);
	~SfzPlayer() override;

	void saveSettings(QDomDocument& doc, QDomElement& element) override;
	void loadSettings(const QDomElement& element) override;
	void loadFile(const QString& file) override;
	QString nodeName() const override;
	gui::PluginView* instantiateView(QWidget* parent) override;
	bool handleMidiEvent(const MidiEvent& event, const TimePos& time = TimePos(), f_cnt_t offset = 0) override;

	//! The loaded (or last asked-for) file as an absolute path; empty before one was chosen.
	const QString& file() const { return m_file; }
	//! Regions in the loaded SFZ; 0 when nothing is loaded.
	Q_INVOKABLE int regionCount() const { return m_regions.load(std::memory_order_relaxed); }
	//! One line for the window: what is loaded, or why the last load failed.
	const QString& status() const { return m_status; }

	FloatModel& volumeModel() { return m_volumeModel; }
	IntModel& qualityModel() { return m_qualityModel; }
	IntModel& polyphonyModel() { return m_polyphonyModel; }
	ComboBoxModel& oversamplingModel() { return m_oversamplingModel; }

	//! The window shows \a message on its status line (an open that the control surface refused).
	void reportOpenFailure(const QString& message);

	//! Load \a file now (control thread): a new synth is built, then handed to the render thread.
	Q_INVOKABLE bool openFile(const QString& file, bool renameTrack = true);

signals:
	void fileChanged();

protected:
	void playImpl(std::span<SampleFrame> out) override;

private:
	friend class gui::SfzPlayerView;

	static constexpr std::size_t EventQueueSize = 1024;

	SfzSynth::Settings currentSettings() const;
	bool installSynth(const QString& absolutePath);
	void scheduleReload();
	void renameTrackAfter(const QString& file);

	// render thread
	std::size_t drainEvents() noexcept;
	void configureSynth(SfzSynth& synth) noexcept;
	void renderPeriod(SfzSynth& synth, std::span<SampleFrame> out, std::size_t eventCount) noexcept;

	FloatModel m_volumeModel;
	IntModel m_qualityModel;
	IntModel m_polyphonyModel;
	ComboBoxModel m_oversamplingModel;

	RtPublished<SfzSynth> m_synth;
	MpscQueue<SfzEvent, EventQueueSize> m_events;

	// control thread
	QString m_file;
	QString m_status;
	std::atomic<bool> m_reloadQueued{false};
	bool m_loadingSettings = false;
	std::atomic<int> m_regions{0};

	// render thread
	std::array<SfzEvent, EventQueueSize> m_batch{};
	std::uint64_t m_configuredSynth = 0;
	float m_appliedVolume = 0.f;
	int m_appliedQuality = 0;
	bool m_freewheeling = false;
};

} // namespace lmms

#endif // LMMS_SFZ_PLAYER_H
