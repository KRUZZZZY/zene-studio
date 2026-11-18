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
#include <vector>

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
	 * The GUI thread publishes an immutable copy instead. A publication fills a
	 * waveform nobody can see yet and hands it over with one atomic store; a
	 * published waveform is never written again, so the audio thread reads one
	 * waveform in full however many publications pass while it copies.
	 *
	 * The audio thread announces the waveform it took, so the GUI thread sees
	 * which published waveform it must not reuse: the announced one stays out
	 * of the free list until the reader takes another one (the reader copies
	 * the waveform it took before it takes another one). While the reader is
	 * between taking a waveform and announcing it, the GUI thread cannot tell
	 * which one it took, so it reuses none of them. The two flags are read and
	 * written with sequentially consistent ordering, which is what makes that
	 * hand-over race free: the reader's "I am reading" store and the writer's
	 * "is it reading" load cannot both miss each other.
	 *
	 * One writer (the GUI thread, where every samplesChanged is emitted), one
	 * reader (the audio thread), no lock, no allocation and no logging on the
	 * audio thread: it takes the published waveform with one atomic load and
	 * makes the same copy it always made.
	 *
	 * The class is public so the regression test can drive the hand-off
	 * itself; nothing outside the plug-in uses it.
	 */
	class WaveformSnapshot
	{
	public:
		WaveformSnapshot()
		{
			m_published.store(new Waveform{}, std::memory_order_release);
		}

		~WaveformSnapshot()
		{
			delete m_published.load(std::memory_order_relaxed);
			for (auto* waveform : m_retired) { delete waveform; }
			for (auto* waveform : m_free) { delete waveform; }
		}

		//! The published waveform: the audio thread takes it and copies it.
		const float* samples()
		{
			// Announce the waveform before the writer may reuse it: the writer
			// keeps the announced one out of the free list until the next read.
			m_readerReading.store(true, std::memory_order_seq_cst);
			Waveform* published = m_published.load(std::memory_order_seq_cst);
			m_readerWaveform.store(published, std::memory_order_seq_cst);
			m_readerReading.store(false, std::memory_order_seq_cst);
			return published->data.data();
		}

		//! Hand off `s_sampleLength` samples as the current waveform.
		void publish(const float* samples)
		{
			Waveform* fresh = takeFree();
			std::copy_n(samples, s_sampleLength, fresh->data.begin());
			Waveform* old = m_published.exchange(fresh, std::memory_order_seq_cst);
			m_retired.push_back(old);
			reclaim();
		}

	private:
		//! One immutable waveform.
		struct Waveform
		{
			std::array<float, s_sampleLength> data;
		};

		//! A waveform nobody can be reading: a free one, or a new one.
		Waveform* takeFree()
		{
			if (m_free.empty()) { return new Waveform{}; }
			Waveform* waveform = m_free.back();
			m_free.pop_back();
			return waveform;
		}

		//! The published waveforms the reader cannot be using any more.
		void reclaim()
		{
			// While the reader is between taking a waveform and announcing it,
			// the writer cannot tell which one it took, so it reuses none.
			if (m_readerReading.load(std::memory_order_seq_cst)) { return; }
			Waveform* held = m_readerWaveform.load(std::memory_order_seq_cst);
			for (auto it = m_retired.begin(); it != m_retired.end(); )
			{
				if (*it == held) { ++it; continue; }
				m_free.push_back(*it);
				it = m_retired.erase(it);
			}
		}

		//! The waveform the audio thread reads.
		std::atomic<Waveform*> m_published{nullptr};
		//! The waveform the reader last took: it may still be copying it.
		std::atomic<Waveform*> m_readerWaveform{nullptr};
		//! True while the reader is between taking a waveform and announcing it.
		std::atomic<bool> m_readerReading{false};
		//! Published waveforms waiting to be reused. GUI thread only.
		std::vector<Waveform*> m_retired;
		//! Waveforms that may be written into. GUI thread only.
		std::vector<Waveform*> m_free;
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
