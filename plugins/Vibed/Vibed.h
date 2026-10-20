/*
 * Vibed.h - combination of PluckedStringSynth and BitInvader
 *
 * Copyright (c) 2006-2008 Danny McRae <khjklujn/at/yahoo/com>
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

#ifndef LMMS_VIBED_H
#define LMMS_VIBED_H

#include "Instrument.h"
#include "InstrumentView.h"
#include "NineButtonSelector.h"
#include "Knob.h"
#include "LedCheckBox.h"
#include "Graph.h"
#include "PixmapButton.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <memory>

namespace lmms
{


class NotePlayHandle;  // IWYU pragma: keep

namespace gui
{
class VibedView;
}

class Vibed : public Instrument
{
	Q_OBJECT
public:
	Vibed(InstrumentTrack* instrumentTrack);
	~Vibed() override = default;

	void deleteNotePluginData(NotePlayHandle* n) override;

	void saveSettings(QDomDocument& doc, QDomElement& elem) override;
	void loadSettings(const QDomElement& elem) override;

	QString nodeName() const override;

	gui::PluginView* instantiateView(QWidget* parent) override;

private:
	void playNoteImpl(NotePlayHandle* n, std::span<SampleFrame> out) override;

	class StringContainer;

	static constexpr int s_sampleLength = 128;
	static constexpr int s_stringCount = 9;

public:
	/*! The waveform the audio thread is allowed to read.
	 *
	 * graphModel is GUI-thread storage: a waveform drag, the smooth and normalize
	 * buttons, the wave buttons and a project load all rewrite it in place. A
	 * note that starts while such a write is in flight must not read it -
	 * VibratingString copies the whole waveform, so reading the model there is a
	 * data race that hands the string a half-applied waveform.
	 *
	 * Three slots, one writer (the GUI thread), no lock and no allocation on
	 * either side. publish() fills the one slot that is neither the published
	 * one nor the one a reader may still be copying, then flips the index;
	 * samples() only ever reads the published slot, and no publication touches
	 * that slot again until the one after next, so a reader always sees one
	 * waveform in full.
	 *
	 * The class is public so the regression test can drive the hand-off
	 * itself; nothing outside the plug-in uses it.
	 */
	class WaveformSnapshot
	{
	public:
		//! Hand off `s_sampleLength` samples as the current waveform.
		void publish(const float* samples)
		{
			// m_front and m_stale are always two different slots, so this is the
			// third one.
			const int front = m_front.load(std::memory_order_acquire);
			const int back = 3 - front - m_stale;
			std::copy_n(samples, s_sampleLength, m_slots[back].begin());
			m_stale = front;  // a reader may still be copying that slot
			m_front.store(back, std::memory_order_release);
		}

		//! The published waveform, stable until the next publication.
		const float* samples() const
		{
			return m_slots[m_front.load(std::memory_order_acquire)].data();
		}

	private:
		std::array<std::array<float, s_sampleLength>, 3> m_slots{};
		std::atomic<int> m_front{0};
		int m_stale{1};  //!< GUI thread only: the slot a reader may still be copying
	};

private:
	//! Re-read one string's graph model and hand the waveform off.
	void publishWaveform(int str);

	std::array<std::unique_ptr<FloatModel>, s_stringCount> m_pickModels;
	std::array<std::unique_ptr<FloatModel>, s_stringCount> m_pickupModels;
	std::array<std::unique_ptr<FloatModel>, s_stringCount> m_stiffnessModels;
	std::array<std::unique_ptr<FloatModel>, s_stringCount> m_volumeModels;
	std::array<std::unique_ptr<FloatModel>, s_stringCount> m_panModels;
	std::array<std::unique_ptr<FloatModel>, s_stringCount> m_detuneModels;
	std::array<std::unique_ptr<FloatModel>, s_stringCount> m_randomModels;
	std::array<std::unique_ptr<FloatModel>, s_stringCount> m_lengthModels;
	std::array<std::unique_ptr<BoolModel>, s_stringCount> m_powerModels;
	std::array<std::unique_ptr<graphModel>, s_stringCount> m_graphModels;
	std::array<std::unique_ptr<BoolModel>, s_stringCount> m_impulseModels;
	std::array<std::unique_ptr<NineButtonSelectorModel>, s_stringCount> m_harmonicModels;
	//! What the audio thread reads: one immutable waveform per string.
	std::array<WaveformSnapshot, s_stringCount> m_waveforms;

	friend class gui::VibedView;
};


namespace gui
{


class VibedView : public InstrumentViewFixedSize
{
	Q_OBJECT
public:
	VibedView(Instrument* instrument, QWidget* parent);
	~VibedView() override = default;

public slots:
	void showString(int str);
	void contextMenuEvent(QContextMenuEvent*) override;

protected slots:
	void sinWaveClicked();
	void triangleWaveClicked();
	void sawWaveClicked();
	void sqrWaveClicked();
	void noiseWaveClicked();
	void usrWaveClicked();
	void smoothClicked();
	void normalizeClicked();

private:
	void modelChanged() override;

	// String-related
	VolumeKnob m_volumeKnob;
	Knob m_stiffnessKnob;
	Knob m_pickKnob;
	Knob m_pickupKnob;
	Knob m_panKnob;
	Knob m_detuneKnob;
	Knob m_randomKnob;
	Knob m_lengthKnob;
	Graph m_graph;
	LedCheckBox m_impulse;
	LedCheckBox m_power;
	std::unique_ptr<NineButtonSelector> m_harmonic;

	// Not in model
	std::unique_ptr<NineButtonSelector> m_stringSelector;
	PixmapButton m_smoothBtn;
	PixmapButton m_normalizeBtn;

	// From impulse editor
	PixmapButton m_sinWaveBtn;
	PixmapButton m_triangleWaveBtn;
	PixmapButton m_sawWaveBtn;
	PixmapButton m_sqrWaveBtn;
	PixmapButton m_whiteNoiseWaveBtn;
	PixmapButton m_usrWaveBtn;
};


} // namespace gui

} // namespace lmms

#endif // LMMS_VIBED_H
