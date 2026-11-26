/*
 * BrowserPreviewWindowTest.cpp - cluster 102: browser.preview reads only its 30-second window
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
/*! Cluster 102: browser.preview caps a preview at 30 seconds, and that cap is a DECODE limit, not a
 *  trim after the whole file has been decoded. The handler asks SampleDecoder::decodeLimited() for
 *  the window, so a multi-hour file costs the preview its first 30 seconds of memory and decode
 *  time instead of the whole file - the defect was `SampleDecoder::decode()` followed by
 *  `data.resize(min(size, 30 * rate))`.
 *
 *  The proof is the decoder's own LMMS_TESTING call counters (reset, invoke, read - the command is
 *  synchronous, so no clock, no sleeps and no races take part): a preview that decodes the whole
 *  file bumps `full`, a preview that reads its window bumps `limited`. Everything else is the
 *  handler's contract: a 35 s file plays the 30 s window, a 2 s file is played whole rather than
 *  cut, and a file that is not audio is refused without a full decode.
 *
 *  The test lives in its own binary because tests/src/core/LibraryBrowserTest.cpp (which already
 *  covers the preview playing and stopping, aMatchedPreviewPlaysAndStops) sits at the file-length
 *  ratchet: the same split the neighbouring suites use, not a moved gate.
 */

#include <QtTest>

#include <cstdint>

#include <QDir>
#include <QJsonObject>
#include <QTemporaryDir>

#include "BrowserTestSupport.h"
#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "SampleDecoder.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! A mono 16-bit WAV of @a seconds whose first second carries signal, so a window is not silence.
QString writeFixture(const QString& path, int seconds)
{
	browserfixture::WaveSpec spec;
	spec.sampleRate = 44100;
	spec.channels = 1;
	spec.burstStartFrame = 0;
	spec.burstFrames = 44100;
	spec.frames = seconds * 44100;
	return browserfixture::writeWave(path, spec) ? path : QString();
}

} // namespace

class BrowserPreviewWindowTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY2(m_dir.isValid(), "no temporary directory for the preview fixtures");
		ConfigManager::inst()->setWorkingDir(QDir(m_dir.path()).filePath(QStringLiteral("work")));
		QVERIFY(QDir().mkpath(ConfigManager::inst()->workingDir()));
		qputenv("ZENE_BROWSER_INDEX", QDir(m_dir.path()).filePath(QStringLiteral("index.json")).toUtf8());
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		qunsetenv("ZENE_BROWSER_INDEX");
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! The regression: a 35 s file is never decoded whole. Without the fix the handler calls
	//! SampleDecoder::decode(), so `full` is 1 and the whole file sits in memory before two thirds
	//! of it are dropped; with the fix the bounded decoder reads the window and `full` stays 0.
	void aLongPreviewDecodesOnlyItsWindow()
	{
		const QString longFile = writeFixture(m_dir.filePath(QStringLiteral("long-preview.wav")), 35);
		QVERIFY2(!longFile.isEmpty(), "the long fixture was not written");

		SampleDecoder::resetDecodeCallCountsForTesting();
		const ControlResult played = run(QStringLiteral("browser.preview"), {{QStringLiteral("path"), longFile}});
		QVERIFY2(played.ok, qPrintable(played.errorMessage));
		const SampleDecoder::DecodeCallCountsForTesting calls = SampleDecoder::decodeCallCountsForTesting();
		QCOMPARE(calls.full, std::uint64_t{0});  // the whole 35 s file was NOT decoded
		QVERIFY2(calls.limited >= std::uint64_t{1}, "the preview did not reach the bounded decoder");

		// The cap itself is unchanged: the preview plays the window and no more of it.
		const double seconds = played.result.value(QStringLiteral("seconds")).toDouble();
		QVERIFY2(seconds <= 30.0 && seconds > 29.0, qPrintable(QStringLiteral("played %1 s").arg(seconds)));
		QVERIFY(played.result.value(QStringLiteral("playing")).toBool());
		QVERIFY(run(QStringLiteral("browser.preview_stop")).ok);
	}

	//! A file shorter than the window is played whole: the decode limit must not cut what it has.
	void aShortPreviewIsPlayedWhole()
	{
		const QString shortFile = writeFixture(m_dir.filePath(QStringLiteral("short-preview.wav")), 2);
		QVERIFY2(!shortFile.isEmpty(), "the short fixture was not written");

		SampleDecoder::resetDecodeCallCountsForTesting();
		const ControlResult played = run(QStringLiteral("browser.preview"), {{QStringLiteral("path"), shortFile}});
		QVERIFY2(played.ok, qPrintable(played.errorMessage));
		QCOMPARE(SampleDecoder::decodeCallCountsForTesting().full, std::uint64_t{0});
		QCOMPARE(played.result.value(QStringLiteral("seconds")).toDouble(), 2.0);
		QVERIFY(run(QStringLiteral("browser.preview_stop")).ok);
	}

	//! The failure path too: a file no decoder can read is refused, and it is refused without a
	//! full decode of whatever the file holds.
	void anUnreadableFileIsRefusedWithoutAFullDecode()
	{
		const QString text = m_dir.filePath(QStringLiteral("not-audio.wav"));
		QVERIFY(browserfixture::writeText(text, QStringLiteral("this file is not audio")));

		SampleDecoder::resetDecodeCallCountsForTesting();
		const ControlResult refused = run(QStringLiteral("browser.preview"), {{QStringLiteral("path"), text}});
		QVERIFY2(!refused.ok, "a text file was played as audio");
		QCOMPARE(refused.errorKind, ControlErrorKind::Refused);
		QCOMPARE(SampleDecoder::decodeCallCountsForTesting().full, std::uint64_t{0});
	}

private:
	QTemporaryDir m_dir;
};

QTEST_MAIN(BrowserPreviewWindowTest)
#include "BrowserPreviewWindowTest.moc"
