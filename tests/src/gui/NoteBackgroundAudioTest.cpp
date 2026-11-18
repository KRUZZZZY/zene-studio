/*
 * NoteBackgroundAudioTest.cpp - the audio clip's waveform drawn behind a MIDI clip's notes
 *
 * Copyright (c) 2026 Zene Studio contributors
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

/*! The note editor can draw an audio clip's waveform faintly behind the notes. Which clip: none, "auto" (the first audio
 *  clip that sounds while the MIDI clip does - touching is not overlapping), or one chosen by id (and only an audio clip
 *  will do). Where it is drawn: at its place in the song relative to the MIDI clip, clipped to the area, and nothing at
 *  all for a clip with no sound or a view that shows none of it. */

#include <QtTest>

#include <QDir>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "MidiClip.h"
#include "NoteBackgroundAudio.h"
#include "NoteGrid.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "Song.h"
#include "InstrumentTrack.h"
#include "../core/TakeLaneTestSupport.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

constexpr tick_t Bar = 192;

//! How many pixels of @a image differ from the white it was filled with inside @a area.
int inked(const QImage& image, const QRect& area)
{
	int count = 0;
	for (int y = area.top(); y <= area.bottom(); ++y)
	{
		for (int x = area.left(); x <= area.right(); ++x)
		{
			if (image.pixelColor(x, y) != QColor(Qt::white)) { ++count; }
		}
	}
	return count;
}

} // namespace

class NoteBackgroundAudioTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_dir.isValid());
		Engine::init(true);
		ControlRegistry::setReady(true);
		m_tone = taketest::writeToneWav(QDir(m_dir.path()).filePath(QStringLiteral("tone.wav")), 220.0);
		QVERIFY(!m_tone.isEmpty());
		m_audioTrack = Track::create(Track::Type::Sample, Engine::getSong());
		m_midiTrack = Track::create(Track::Type::Instrument, Engine::getSong());
		m_audio = dynamic_cast<SampleClip*>(m_audioTrack->createClip(TimePos(Bar)));
		m_audio->setSampleFile(m_tone);
		QVERIFY(m_audio->sample().sampleSize() > 0);
		m_midi = dynamic_cast<MidiClip*>(m_midiTrack->createClip(TimePos(0)));
		m_midi->changeLength(TimePos(4 * Bar));
		m_late = dynamic_cast<MidiClip*>(m_midiTrack->createClip(TimePos(40 * Bar)));
		m_late->changeLength(TimePos(2 * Bar));
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void autoFindsTheAudioClipThatSoundsWhileTheMidiClipDoes()
	{
		QCOMPARE(NoteBackgroundAudio::overlapping(*m_midi), m_audio);
		QVERIFY(NoteBackgroundAudio::overlapping(*m_late) == nullptr);

		// Touching is not overlapping: an audio clip that starts where the MIDI clip ends.
		auto* touching = dynamic_cast<MidiClip*>(m_midiTrack->createClip(TimePos(0)));
		touching->changeLength(TimePos(Bar));  // ends where the audio clip starts
		QVERIFY(NoteBackgroundAudio::overlapping(*touching) == nullptr);
		delete touching;
	}

	void aClipIsChosenByNothingAutoOrItsId()
	{
		QVERIFY(NoteBackgroundAudio::resolve(QString(), *m_midi) == nullptr);
		QCOMPARE(NoteBackgroundAudio::resolve(QStringLiteral("auto"), *m_midi), m_audio);
		QVERIFY(NoteBackgroundAudio::resolve(QStringLiteral("auto"), *m_late) == nullptr);
		QCOMPARE(NoteBackgroundAudio::resolve(control::clipIdOf(m_audio), *m_late), m_audio);  // by id, wherever it is
		QVERIFY(NoteBackgroundAudio::resolve(QStringLiteral("clip-999999"), *m_midi) == nullptr);
		QVERIFY(NoteBackgroundAudio::resolve(QStringLiteral("not a clip"), *m_midi) == nullptr);
		QVERIFY2(NoteBackgroundAudio::resolve(control::clipIdOf(m_late), *m_midi) == nullptr, "a MIDI clip was taken for audio");
	}

	void theWaveformIsDrawnAtItsPlaceInTheSongAndClippedToTheArea()
	{
		NoteGrid grid;
		grid.setPixelsPerTick(0.5);
		grid.setViewport(1000, 200);
		const QRect area(0, 0, 1000, 200);
		QImage image(area.size(), QImage::Format_RGB32);
		NoteBackgroundAudio background;

		image.fill(Qt::white);
		{
			QPainter p(&image);
			background.paint(p, grid, *m_midi, m_audio, area);
		}
		const int sampleLeft = static_cast<int>(grid.xOfTick(Bar));  // the audio clip starts a bar into the MIDI clip
		const int sampleRight = static_cast<int>(grid.xOfTick(Bar + m_audio->sampleLength().getTicks()));
		QVERIFY(sampleRight > sampleLeft);
		QVERIFY2(inked(image, QRect(sampleLeft, 0, sampleRight - sampleLeft, 200)) > 100, "no waveform inside the audio clip's span");
		QCOMPARE(inked(image, QRect(0, 0, sampleLeft - 1, 200)), 0);   // before it: nothing
		QCOMPARE(inked(image, QRect(sampleRight + 2, 0, 1000 - sampleRight - 2, 200)), 0);  // after it: nothing

		// An area narrower than the clip clips the drawing to the area.
		image.fill(Qt::white);
		const QRect narrow(sampleLeft, 50, 20, 80);
		{
			QPainter p(&image);
			background.paint(p, grid, *m_midi, m_audio, narrow);
		}
		QVERIFY(inked(image, narrow) > 0);
		QCOMPARE(inked(image, QRect(0, 0, 1000, 49)), 0);
		QCOMPARE(inked(image, QRect(0, 131, 1000, 69)), 0);
		QCOMPARE(inked(image, QRect(sampleLeft + 21, 0, 500, 200)), 0);

		// The same clip again keeps its thumbnail; a different file rebuilds it and still draws.
		QString other = taketest::writeToneWav(QDir(m_dir.path()).filePath(QStringLiteral("other.wav")), 660.0);
		m_audio->setSampleFile(other);
		image.fill(Qt::white);
		{
			QPainter p(&image);
			background.paint(p, grid, *m_midi, m_audio, area);
		}
		QVERIFY(inked(image, area) > 100);
		m_audio->setSampleFile(m_tone);
	}

	void nothingIsDrawnWithoutSoundOrWithoutAnyOfItInView()
	{
		NoteGrid grid;
		grid.setPixelsPerTick(0.5);
		grid.setViewport(1000, 200);
		const QRect area(0, 0, 1000, 200);
		QImage image(area.size(), QImage::Format_RGB32);
		NoteBackgroundAudio background;

		image.fill(Qt::white);
		{
			QPainter p(&image);
			background.paint(p, grid, *m_midi, nullptr, area);
			auto* silent = dynamic_cast<SampleClip*>(m_audioTrack->createClip(TimePos(Bar)));  // no file: no sound
			background.paint(p, grid, *m_midi, silent, area);
			delete silent;
			// Scrolled so far on that none of the clip's span is in the area.
			grid.setScrollTicks(30 * Bar);
			background.paint(p, grid, *m_midi, m_audio, area);
		}
		QCOMPARE(inked(image, area), 0);
	}

private:
	QTemporaryDir m_dir;
	QString m_tone;
	Track* m_audioTrack = nullptr;
	Track* m_midiTrack = nullptr;
	SampleClip* m_audio = nullptr;
	MidiClip* m_midi = nullptr;
	MidiClip* m_late = nullptr;
};

QTEST_MAIN(NoteBackgroundAudioTest)
#include "NoteBackgroundAudioTest.moc"
