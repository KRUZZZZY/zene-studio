/*
 * SlicerT.h - declaration of class SlicerT
 *
 * Copyright (c) 2023 Daniel Kauss Serna <daniel.kauss.serna@gmail.com>
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

#ifndef LMMS_SLICERT_H
#define LMMS_SLICERT_H

#include <array>
#include <atomic>
#include <cstdint>

#include "AutomatableModel.h"
#include "ComboBoxModel.h"
#include "Instrument.h"
#include "Note.h"
#include "Sample.h"
#include "SlicerSliceControls.h"
#include "SlicerTView.h"

namespace lmms {

class SlicerT : public Instrument
{
	Q_OBJECT

public slots:
	void updateFile(QString file);
	void updateSlices();

signals:
	void isPlaying(float current, float start, float end);

public:
	SlicerT(InstrumentTrack* instrumentTrack);

	void deleteNotePluginData(NotePlayHandle* handle) override;
	void applyConverterMode(AudioResampler::Mode mode) override;
	auto prepareConverterMode(AudioResampler::Mode mode) noexcept -> bool override;
	void adoptPreparedConverterMode() noexcept override;
	void discardPreparedConverterMode() noexcept override;

	void saveSettings(QDomDocument& document, QDomElement& element) override;
	void loadSettings(const QDomElement& element) override;

	void loadFile(const QString& file) override;
	void findSlices();
	void findBPM();

	QString getSampleName() { return m_originalSample.sampleFile(); }

	QString nodeName() const override;
	gui::PluginView* instantiateView(QWidget* parent) override;

	std::vector<Note> getMidi();

	//! Q050-263: the per-slice controls (include/SlicerSliceControls.h). A slice is the interval between two
	//! adjacent slice points; slice i is MIDI key baseNote + 1 + i. The registered commands plugin.slice_get
	//! and plugin.slice_set reach these by name through the meta-object, so the core needs no plug-in header.
	//! They are control-thread calls (never the audio thread): the audio thread reads the same values through
	//! relaxed atomics in applySliceControls().
	Q_INVOKABLE int sliceCount() const;
	//! Slice point @a index as a fraction of the sample (0..1), or -1 when there is none.
	Q_INVOKABLE double slicePoint(int index) const;
	//! One control of one slice, or NaN for an unknown slice or name.
	Q_INVOKABLE double sliceControl(int slice, const QString& name) const;
	//! Writes one control of one slice; false (nothing written) for an unknown slice or name or a value outside
	//! the control's range.
	Q_INVOKABLE bool setSliceControl(int slice, const QString& name, double value);

private:
	void playNoteImpl(NotePlayHandle* handle, std::span<SampleFrame> out) override;
	//! What one sounding slice note keeps between periods: this trigger's two random draws (-1..1) and the
	//! per-channel state of its filter. Plain floats, owned by the note's slot: nothing is allocated.
	struct SliceVoice
	{
		float randomGain = 0.f;
		float randomPan = 0.f;
		std::array<std::array<float, 2>, 2> filterState{}; //!< [channel][integrator]
	};
	struct PlaybackSlot
	{
		Sample::PlaybackState state;
		std::atomic<bool> inUse{false};
		SliceVoice voice;
	};
	PlaybackSlot* acquirePlaybackSlot(int startFrame) noexcept;
	//! Gain, pan, filter and random amount of slice @a slice applied to one rendered block. Audio thread:
	//! reads the control atomics and writes only @a block and @a voice; neutral controls return at once.
	void applySliceControls(SliceVoice& voice, int slice, SampleFrame* block, f_cnt_t frames) noexcept;
	void resetSliceControls() noexcept;
	//! A slice point was deleted/inserted at @a pointIndex: the controls follow their slices.
	void sliceMerged(int pointIndex) noexcept;
	void sliceSplit(int pointIndex) noexcept;
	using SliceBank = std::array<std::atomic<float>, slicer::SliceControlCount>;
	std::array<SliceBank, slicer::MaxSlices> m_sliceControls;
	std::atomic<std::uint32_t> m_triggerCounter{0};
	static constexpr std::size_t PlaybackSlotCount = 128;
	std::array<PlaybackSlot, PlaybackSlotCount> m_playbackSlots{};
	std::uint8_t m_noPlaybackSlotTag = 0;

	FloatModel m_noteThreshold;
	FloatModel m_fadeOutFrames;
	IntModel m_originalBPM;
	ComboBoxModel m_sliceSnap;
	BoolModel m_enableSync;

	Sample m_originalSample;

	std::vector<float> m_slicePoints;

	InstrumentTrack* m_parentTrack;

	//! The playhead the audio thread last published (cluster 4468): three atomics, written by the render
	//! thread and read by pollPlayhead() on the GUI thread. The render thread never emits a signal.
	void publishPlayhead(float current, float start, float end) noexcept;
	void pollPlayhead();
	std::atomic<float> m_playheadCurrent{-1.f};
	std::atomic<float> m_playheadStart{0.f};
	std::atomic<float> m_playheadEnd{0.f};
	//! The values pollPlayhead() last emitted (GUI thread only).
	float m_shownCurrent = -1.f;
	float m_shownStart = 0.f;
	float m_shownEnd = 0.f;
	friend class gui::SlicerTView;
	friend class gui::SlicerTWaveform;
};
} // namespace lmms
#endif // LMMS_SLICERT_H
