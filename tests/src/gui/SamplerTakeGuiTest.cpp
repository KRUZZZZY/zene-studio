/*
 * SamplerTakeGuiTest.cpp - the sampler views' "Record take" button (0.5.0 L10, Q050-156)
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

/*! Q050-156 from the interface: each sampler view owns a Record take button. Clicking it starts
 *  record.sampler_capture for that track; clicking it again stops the take
 *  (record.sampler_capture_stop) and the view lands it (record.sampler_capture_land) immediately,
 *  so the waveform and the device are updated. A take left to reach its requested length is landed
 *  by the view's own poll. The engine is the real one on the Dummy device; the test drives the
 *  periods. The same registered commands are the agent surface's (SamplerTakeTest). */

#include <QtTest>

#include <QDomDocument>
#include <QDomElement>
#include <QFileInfo>
#include <QJsonObject>
#include <QPushButton>
#include <QTemporaryDir>

#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "ConfigManager.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "Note.h"
#include "NotePlayHandle.h"
#include "PathUtil.h"
#include "PluginView.h"
#include "SampleFrame.h"
#include "Song.h"
#include "Track.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

SampleFrame inputFrame(std::uint64_t n)
{
	// A known, zero-mean ramp gated in 2048-frame bursts: the test can name every frame, and
	// SlicerT's onset detector has real transients to cut at.
	const bool on = ((n / 2048) % 2) == 0;
	const float gain = on ? 1.0f : 0.0f;
	const float sign = (n % 2) == 0 ? -1.0f : 1.0f;
	return SampleFrame(gain * sign * (static_cast<float>(n % 4096) / 4096.0f - 0.5f),
		gain * sign * (static_cast<float>((n / 4096) % 4096) / 4096.0f - 0.5f));
}

QDomElement withAttribute(const QDomElement& element, const QString& attribute)
{
	if (element.hasAttribute(attribute)) { return element; }
	for (QDomElement child = element.firstChildElement(); !child.isNull(); child = child.nextSiblingElement())
	{
		const QDomElement found = withAttribute(child, attribute);
		if (!found.isNull()) { return found; }
	}
	return {};
}

//! The device's serialized element inside the track's own saved state (the project's save path).
QDomElement deviceState(Track* track)
{
	QString xml;
	if (!control::captureTrackXml(track, &xml, 1 << 20)) { return {}; }
	QDomDocument document;
	if (!document.setContent(xml)) { return {}; }
	return withAttribute(document.documentElement(), QStringLiteral("src"));
}

} // namespace

class SamplerTakeGuiTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();  // the test drives the periods itself
		NotePlayHandleManager::init();
		QVERIFY(m_home.isValid());
		ConfigManager::inst()->setWorkingDir(m_home.path());  // the default take lands in <work>/samples/Recordings
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! A click starts, a second click stops and lands; the device now holds and plays the take.
	void clickingTheAudioFileProcessorButtonRecordsAndStops()
	{
		InstrumentTrack* track = samplerTrack(QStringLiteral("audiofileprocessor"));
		QVERIFY(track != nullptr);
		QWidget host;
		gui::PluginView* view = track->instrument()->createView(&host);
		QVERIFY(view != nullptr);
		QPushButton* button = view->findChild<QPushButton*>(QStringLiteral("audioFileProcessorRecordTake"));
		QVERIFY2(button != nullptr, "AudioFileProcessorView has no Record take button");

		QTest::mouseClick(button, Qt::LeftButton);
		QCOMPARE(button->property("zeneSamplerTakeState").toString(), QStringLiteral("recording"));
		QCOMPARE(takeState(), QStringLiteral("recording"));
		for (int i = 0; i < 4; ++i) { period(); }

		QTest::mouseClick(button, Qt::LeftButton);
		QCOMPARE(button->property("zeneSamplerTakeState").toString(), QStringLiteral("idle"));
		QCOMPARE(takeState(), QStringLiteral("idle"));
		const QString src = deviceState(track).attribute(QStringLiteral("src"));
		QVERIFY2(!src.isEmpty(), "the AudioFileProcessor holds no sample after the GUI take");
		QVERIFY(QFileInfo::exists(PathUtil::toAbsolute(src)));
		QVERIFY2(renders(track, track->baseNote()), "AudioFileProcessor rendered silence after the GUI take");
	}

	//! A take left to reach its requested length is landed by the view's own poll.
	void aCompletedSlicerTTakeIsLandedByTheButtonsPoll()
	{
		InstrumentTrack* track = samplerTrack(QStringLiteral("slicert"));
		QVERIFY(track != nullptr);
		QWidget host;
		gui::PluginView* view = track->instrument()->createView(&host);
		QVERIFY(view != nullptr);
		QPushButton* button = view->findChild<QPushButton*>(QStringLiteral("slicerTRecordTake"));
		QVERIFY2(button != nullptr, "SlicerTView has no Record take button");
		button->setProperty("zeneSamplerTakeSeconds", 0.05);

		QTest::mouseClick(button, Qt::LeftButton);
		QCOMPARE(button->property("zeneSamplerTakeState").toString(), QStringLiteral("recording"));
		for (int i = 0; i < 200 && button->property("zeneSamplerTakeState").toString() != QStringLiteral("idle"); ++i)
		{
			period();
			QTest::qWait(10);
		}
		QCOMPARE(button->property("zeneSamplerTakeState").toString(), QStringLiteral("idle"));
		QCOMPARE(takeState(), QStringLiteral("idle"));
		QDomElement saved = deviceState(track);
		QVERIFY2(!saved.attribute(QStringLiteral("src")).isEmpty(), "SlicerT holds no sample after the GUI take");
		QVERIFY(saved.attribute(QStringLiteral("totalSlices")).toInt() >= 2);
		QVERIFY2(renders(track, track->baseNote() + 1), "SlicerT rendered silence after the GUI take");
	}

private:
	InstrumentTrack* samplerTrack(const QString& plugin)
	{
		Engine::getSong()->clearProject();
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		if (track == nullptr) { return nullptr; }
		Instrument* instrument = track->loadInstrument(plugin);
		if (instrument == nullptr || instrument->nodeName().toLower() != plugin) { return nullptr; }
		return track;
	}

	QString takeState() const
	{
		return run(QStringLiteral("record.sampler_capture_status")).result.value(QStringLiteral("state")).toString();
	}

	void period()
	{
		AudioEngine* engine = Engine::audioEngine();
		const f_cnt_t fpp = engine->framesPerPeriod();
		std::vector<SampleFrame> block(static_cast<std::size_t>(fpp));
		for (f_cnt_t f = 0; f < fpp; ++f) { block[static_cast<std::size_t>(f)] = inputFrame(m_fed + static_cast<std::uint64_t>(f)); }
		engine->pushInputFrames(block.data(), fpp);
		m_fed += static_cast<std::uint64_t>(fpp);
		engine->renderNextPeriod();
	}

	bool renders(InstrumentTrack* track, int key)
	{
		const f_cnt_t fpp = Engine::audioEngine()->framesPerPeriod();
		std::vector<SampleFrame> out(static_cast<std::size_t>(fpp));
		const Note note(TimePos(384), TimePos(0), key);
		NotePlayHandle* handle = NotePlayHandleManager::acquire(track, 0, fpp, note);
		if (handle == nullptr) { return false; }
		track->instrument()->playNote(handle, std::span<SampleFrame>{out});
		NotePlayHandleManager::release(handle);
		for (const SampleFrame& frame : out)
		{
			if (std::fabs(frame.left()) > 1e-6f || std::fabs(frame.right()) > 1e-6f) { return true; }
		}
		return false;
	}

	QTemporaryDir m_home;
	std::uint64_t m_fed = 0;
};

QTEST_MAIN(SamplerTakeGuiTest)
#include "SamplerTakeGuiTest.moc"
