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

#include <atomic>

#include <QTimer>

#include "AutomatableModel.h"
#include "ComboBoxModel.h"
#include "Instrument.h"
#include "Note.h"
#include "Sample.h"
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

	void saveSettings(QDomDocument& document, QDomElement& element) override;
	void loadSettings(const QDomElement& element) override;

	void loadFile(const QString& file) override;
	void findSlices();
	void findBPM();

	QString getSampleName() { return m_originalSample.sampleFile(); }

	QString nodeName() const override;
	gui::PluginView* instantiateView(QWidget* parent) override;

	std::vector<Note> getMidi();

private:
	void playNoteImpl(NotePlayHandle* handle, std::span<SampleFrame> out) override;

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
	QTimer m_playheadPoll;

	friend class gui::SlicerTView;
	friend class gui::SlicerTWaveform;
};
} // namespace lmms
#endif // LMMS_SLICERT_H
