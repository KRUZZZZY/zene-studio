/*
 * DrumSynthTest.cpp - malformed .ds envelope parsing regression coverage.
 *
 * This file is part of Zene Studio (an LMMS-derived product).
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
 */

#include <QtTest>

#include <QFile>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <numbers>
#include <thread>
#include <vector>
#include <limits>
#include <new>

#include "DrumSynth.h"

using namespace lmms;

namespace
{

// "0,9 4,9 8,9 ..." with the given number of points.
QString envelopeText(int points)
{
	QString result;
	for (int point = 0; point < points; ++point)
	{
		if (point > 0) { result += QLatin1Char(' '); }
		result += QStringLiteral("%1,9").arg(point * 4);
	}
	return result;
}

bool writeToneFixture(const QString& path, const QString& toneEnvelope)
{
	const auto contents = QStringLiteral(
		"[General]\n"
		"Version=DrumSynth v2.0\n"
		"[Tone]\n"
		"On=1\n"
		"Level=100\n"
		"Envelope=%1\n"
		"F1=440\n"
		"F2=220\n").arg(toneEnvelope).toUtf8();
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

std::vector<int16_t> render(const QString& path)
{
	DrumSynth synth;
	int16_t* rawSamples = nullptr;
	const int frames = synth.GetDSFileSamples(path, rawSamples, 1, 44100);
	const auto samples = std::unique_ptr<int16_t[]>{rawSamples};
	if (frames <= 0 || !samples) { return {}; }
	return {samples.get(), samples.get() + frames};
}

// A tone whose envelope ends at time zero: the tone has no positive duration to sweep over.
bool writeZeroDurationToneFixture(const QString& path, int droop)
{
	const auto contents = QStringLiteral(
		"[General]\n"
		"Version=DrumSynth v2.0\n"
		"Level=0\n"
		"[Tone]\n"
		"On=1\n"
		"Level=100\n"
		"F1=440\n"
		"F2=880\n"
		"Phase=0\n"
		"Droop=%1\n"
		"Envelope=0,100\n").arg(droop).toUtf8();
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

// A tone fixture whose F1 line is `f1Line`; an empty line omits the key so F1 takes its default.
bool writeToneFrequencyFixture(const QString& path, const QString& f1Line, const QString& toneEnvelope)
{
	const auto contents = QStringLiteral(
		"[General]\n"
		"Version=DrumSynth v2.0\n"
		"[Tone]\n"
		"On=1\n"
		"Level=100\n"
		"Envelope=%1\n"
		"%2"
		"F2=220\n").arg(toneEnvelope, f1Line).toUtf8();
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

/*! One .ds file with a short, deterministic tone.
 *
 *  \a level makes two fixtures differ, \a leadingLongLine puts an over-long
 *  line in front of the settings and \a middleLongLine puts one between them.
 *  The pre-fix profile parser read its lines into a 200 byte buffer, so an
 *  over-long line set the stream's failure state and the settings behind it
 *  fell back to their defaults. */
QByteArray drumSynthFixture(int level, bool leadingLongLine, bool middleLongLine)
{
	QByteArray contents = "[General]\n";
	if (leadingLongLine) { contents += QByteArray(512, 'x') + '\n'; }
	contents += "Version=DrumSynth v2.0\n[Tone]\nOn=1\nLevel=" + QByteArray::number(level) + '\n';
	if (middleLongLine) { contents += QByteArray(512, 'x') + '\n'; }
	contents += "Envelope=0,9 4,9 8,9\nF1=440\nF2=220\n";
	return contents;
}

bool writeDrumSynthFixture(const QString& path, const QByteArray& contents)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
		&& file.write(contents) == contents.size();
}

} // namespace

namespace lmms {
// The parse state of DrumSynth is thread local, so that two threads can parse a
// .ds file at the same time without overwriting each other's settings.
extern thread_local float envpts[8][3][32];
extern thread_local float timestretch;
extern thread_local int chkOn[8];
}

#if !defined(_WIN32)

// The C library's strtok() keeps its position in ONE process-wide pointer, so
// two threads that tokenise at the same time take each other's tokens. The
// pre-fix DrumSynth profile parser tokenised every setting line with strtok(),
// which is why two threads parsing a .ds file at the same time read each
// other's values.
//
// The stand-in below keeps that process-wide pointer - the parser under test
// therefore behaves exactly like the C library - and adds one rendezvous. The
// pre-fix parser asked strtok() for the key of a line and then for its value,
// so the stand-in tells the two calls apart: the call with a buffer is the key
// and the call without one is the value. While the rendezvous is armed, a
// thread takes its key token, waits until the other thread has taken its key
// token as well, and only then asks for its value. The process-wide pointer
// therefore holds the other thread's buffer when the first value is taken -
// the interleaving the C library produces by itself, but at a known point
// instead of at a random one. Nothing here depends on timing: the rendezvous
// is released by the second arrival, not by a timeout.
//
// The stand-in replaces strtok() for this test binary; the rendezvous is armed
// only while the concurrency case runs, so every other strtok() call in the
// binary keeps the C library's behaviour.
namespace
{

//! The C library's process-wide strtok() position.
std::mutex g_strtokMutex;
char* g_strtokPosition = nullptr;

std::mutex g_rendezvousMutex;
std::condition_variable g_rendezvousCondition;
int g_rendezvousParticipants = 0;
int g_rendezvousKeys = 0;
int g_rendezvousValues = 0;
//! Set in the threads that take part in the rendezvous.
thread_local bool g_rendezvousParticipant = false;
//! Set in a thread that has taken its key token.
thread_local bool g_rendezvousKeyTaken = false;
//! Set in a thread that has taken its value token.
thread_local bool g_rendezvousValueTaken = false;

//! The C library's own algorithm, with its process-wide position.
char* takeToken(char* str, const char* delim)
{
	std::lock_guard<std::mutex> lock(g_strtokMutex);
	if (str != nullptr) { g_strtokPosition = str; }
	if (g_strtokPosition == nullptr) { return nullptr; }

	char* cursor = g_strtokPosition;
	while (*cursor != '\0' && std::strchr(delim, *cursor) != nullptr) { ++cursor; }
	if (*cursor == '\0') { g_strtokPosition = nullptr; return nullptr; }

	char* token = cursor;
	while (*cursor != '\0' && std::strchr(delim, *cursor) == nullptr) { ++cursor; }
	if (*cursor != '\0') { *cursor = '\0'; ++cursor; }
	g_strtokPosition = cursor;
	return token;
}

} // namespace

// glibc declares strtok() with __THROW (noexcept); other C libraries declare
// it without an exception specification, and C++17 makes the two incompatible.
#if defined(__GLIBC__)
#define ZENE_TEST_STROTK noexcept
#else
#define ZENE_TEST_STROTK
#endif

extern "C" char* strtok(char* str, const char* delim) ZENE_TEST_STROTK
{
	if (g_rendezvousParticipant)
	{
		if (str != nullptr)
		{
			if (!g_rendezvousKeyTaken)
			{
				char* token = takeToken(str, delim);
				// An empty line has no token to steal: the rendezvous starts at
				// the first line that has one.
				if (token == nullptr) { return token; }
				g_rendezvousKeyTaken = true;
				{
					std::unique_lock<std::mutex> lock(g_rendezvousMutex);
					++g_rendezvousKeys;
					g_rendezvousCondition.notify_all();
					g_rendezvousCondition.wait(lock,
						[] { return g_rendezvousKeys >= g_rendezvousParticipants; });
				}
				return token;
			}
		}
		else if (g_rendezvousKeyTaken && !g_rendezvousValueTaken)
		{
			// The value of the line: it must not be taken before the other
			// thread has taken its key, or the process-wide pointer would still
			// hold this thread's own buffer. The second barrier keeps the next
			// key of either thread from resetting that pointer in between the
			// two values.
			g_rendezvousValueTaken = true;
			char* token = takeToken(str, delim);
			{
				std::unique_lock<std::mutex> lock(g_rendezvousMutex);
				++g_rendezvousValues;
				g_rendezvousCondition.notify_all();
				g_rendezvousCondition.wait(lock,
					[] { return g_rendezvousValues >= g_rendezvousParticipants; });
			}
			return token;
		}
	}
	return takeToken(str, delim);
}

namespace
{

//! Lets the calling thread take part in the rendezvous above.
void joinStrtokRendezvous() { g_rendezvousParticipant = true; }

//! Arms the rendezvous for \a participants parsing threads.
void armStrtokRendezvous(int participants)
{
	std::lock_guard<std::mutex> lock(g_rendezvousMutex);
	g_rendezvousParticipants = participants;
	g_rendezvousKeys = 0;
	g_rendezvousValues = 0;
}

//! Releases everybody and disarms the rendezvous.
void disarmStrtokRendezvous()
{
	{
		std::lock_guard<std::mutex> lock(g_rendezvousMutex);
		g_rendezvousParticipants = 0;
		g_rendezvousKeys = 0;
		g_rendezvousValues = 0;
	}
	g_rendezvousCondition.notify_all();
}

} // namespace

#else // _WIN32

// The Windows C library marks strtok() as deprecated, so the stand-in above is
// left out there; the concurrency case then relies on the over-long line of
// its fixture, which the pre-fix parser could not read past either.
namespace
{
void joinStrtokRendezvous() {}
void armStrtokRendezvous(int) {}
void disarmStrtokRendezvous() {}
} // namespace

#endif // _WIN32

class DrumSynthTest : public QObject
{
	Q_OBJECT

private slots:
	void rejectsOverlongEnvelopeLevel()
	{
		QTemporaryDir directory;
		QVERIFY(directory.isValid());

		const QString path = directory.filePath(QStringLiteral("overlong-envelope.ds"));
		QFile file(path);
		QVERIFY(file.open(QIODevice::WriteOnly));
		QVERIFY(file.write(R"([General]
Version=DrumSynth v1.0

[Tone]
On=1
Level=128
F1=440
F2=440
Phase=90
Envelope=0,00000001 100,0
)") > 0);
		file.close();

		DrumSynth synth;
		int16_t* samples = nullptr;
		const int sampleCount = synth.GetDSFileSamples(path, samples, 1, 44100);
		const bool rendered = sampleCount > 0 && samples != nullptr;
		const bool silent = rendered
			&& std::all_of(samples, samples + sampleCount,
				[](int16_t sample) { return sample == 0; });
		delete[] samples;

		QVERIFY2(rendered, "the valid file around the malformed envelope must still render");
		QVERIFY2(silent, "an eight-character envelope level must be rejected safely");
	}

	void ignoresEnvelopePointsBeyondSupportedCapacity()
	{
		// envpts holds 32 points per envelope and needs one slot for the end sentinel, so the 32nd
		// point must be dropped rather than written past the array and into the first level.
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const QString prefixPath = directory.filePath(QStringLiteral("prefix.ds"));
		const QString oversizedPath = directory.filePath(QStringLiteral("oversized.ds"));
		QVERIFY(writeToneFixture(prefixPath, envelopeText(31)));
		QVERIFY(writeToneFixture(oversizedPath, envelopeText(32)));

		const auto prefixSamples = render(prefixPath);
		const auto oversizedSamples = render(oversizedPath);
		QVERIFY(!prefixSamples.empty());
		QCOMPARE(oversizedSamples.size(), prefixSamples.size());
		QVERIFY2(oversizedSamples == prefixSamples,
			"an envelope point beyond the supported capacity changed the rendered prefix");
	}

	void zeroDurationToneUsesItsStartingFrequency_data()
	{
		QTest::addColumn<int>("droop");
		QTest::newRow("static") << 0;
		QTest::newRow("with-droop") << 40;
	}

	void zeroDurationToneUsesItsStartingFrequency()
	{
		QFETCH(int, droop);
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const QString path = directory.filePath(QStringLiteral("zero-duration.ds"));
		QVERIFY(writeZeroDurationToneFixture(path, droop));

		const auto samples = render(path);
		QCOMPARE(samples.size(), std::size_t{2400});

		// A tone without a positive envelope duration has no sweep to run, so it stays at F1.
		const float phaseIncrement = 2.f * std::numbers::pi_v<float> * 440.f / 44100.f;
		const auto expected = static_cast<int16_t>(10000.f * std::sin(phaseIncrement));
		QCOMPARE(samples.front(), expected);
	}

	void rejectsNonfiniteToneFrequency_data()
	{
		QTest::addColumn<QString>("f1Line");
		QTest::newRow("nan") << QStringLiteral("F1=nan\n");
		QTest::newRow("inf") << QStringLiteral("F1=inf\n");
		QTest::newRow("negative-inf") << QStringLiteral("F1=-inf\n");
	}

	void rejectsNonfiniteToneFrequency()
	{
		// A non-finite F1 must fall back to its default, so the render matches a file without the key.
		QFETCH(QString, f1Line);
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const QString envelope = QStringLiteral("0,100 1200,0");
		const QString nonfinitePath = directory.filePath(QStringLiteral("nonfinite-f1.ds"));
		const QString defaultPath = directory.filePath(QStringLiteral("default-f1.ds"));
		QVERIFY(writeToneFrequencyFixture(nonfinitePath, f1Line, envelope));
		QVERIFY(writeToneFrequencyFixture(defaultPath, QString(), envelope));

		const auto expected = render(defaultPath);
		QVERIFY(std::any_of(expected.begin(), expected.end(), [](int16_t sample) { return sample != 0; }));
		QVERIFY2(render(nonfinitePath) == expected, "a non-finite F1 must render as the default frequency");
	}

	void rejectsNonfiniteEnvelopeLevel_data()
	{
		QTest::addColumn<QString>("level");
		QTest::newRow("nan") << QStringLiteral("nan");
		QTest::newRow("inf") << QStringLiteral("inf");
		QTest::newRow("negative-inf") << QStringLiteral("-inf");
	}

	void rejectsNonfiniteEnvelopeLevel()
	{
		// A non-finite envelope level must be stored as zero, so the render matches a literal zero level.
		QFETCH(QString, level);
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const QString nonfinitePath = directory.filePath(QStringLiteral("nonfinite-envelope.ds"));
		const QString zeroPath = directory.filePath(QStringLiteral("zero-envelope.ds"));
		QVERIFY(writeToneFixture(nonfinitePath, QStringLiteral("0,100 600,%1 1200,0").arg(level)));
		QVERIFY(writeToneFixture(zeroPath, QStringLiteral("0,100 600,0 1200,0")));

		const auto expected = render(zeroPath);
		QVERIFY(std::any_of(expected.begin(), expected.end(), [](int16_t sample) { return sample != 0; }));
		QVERIFY2(render(nonfinitePath) == expected, "a non-finite envelope level must be stored as zero");
	}

	void refusesAnEnvelopeWhoseRoundedLengthOverflows()
	{
		QTemporaryDir tempDir;
		QVERIFY(tempDir.isValid());
		QFile file(tempDir.filePath("oversized.ds"));
		QVERIFY(file.open(QIODevice::WriteOnly));
		const QByteArray content = "[General]\nVersion=DrumSynth v2.0\nStretch=100\n\n"
			"[Tone]\nOn=1\nEnvelope=0,0 3e9,0\n";
		QCOMPARE(file.write(content), static_cast<qint64>(content.size()));
		file.close();

		int16_t* samples = nullptr;
		int frames = -1;
		bool allocationFailed = false;
		try
		{
			DrumSynth synth;
			frames = synth.GetDSFileSamples(file.fileName(), samples, 2, 44100);
		}
		catch (const std::bad_alloc&)
		{
			allocationFailed = true;
		}
		delete[] samples;

		QVERIFY2(!allocationFailed, "An overflowing duration must be rejected before allocation");
		QCOMPARE(frames, 0);
		QVERIFY(samples == nullptr);
	}

	void mapsAFractionalStepAboveMaxToTheSafeMaximumLength()
	{
		// globals declared at file scope below

		// Reset envelope state, then install a duration whose step count is
		// fractional just above maxSteps; truncation must yield maxSteps.
		for (int e = 0; e < 8; e++)
		{
			chkOn[e] = 0;
			for (int t = 0; t < 3; t++)
			{
				for (int p = 0; p < 32; p++)
				{
					envpts[e][t][p] = -1.f;
				}
			}
		}
		constexpr int blockLength = 1200;
		constexpr int baseLength = 2 * blockLength;
		constexpr int maxSteps = (std::numeric_limits<int>::max() - baseLength) / blockLength;
		const float boundarySteps = static_cast<float>(maxSteps) + 0.5f;
		envpts[1][0][0] = boundarySteps * blockLength;
		envpts[1][0][1] = -1.f;
		chkOn[0] = 1;
		timestretch = 1.f;

		DrumSynth synth;
		const int length = synth.LongestEnv();
		const int expectedLength = baseLength + (blockLength * maxSteps);
		QCOMPARE(length, expectedLength);
	}

	/*! An over-long line must not stop the profile parser.
	 *
	 *  The pre-fix parser read every line into a 200 byte buffer: an over-long
	 *  line set the stream's failure state, the scan ended there and the
	 *  settings behind it fell back to their defaults without any error. The
	 *  comparison is on the complete rendered samples, because the settings
	 *  that fall back can leave the frame count untouched. */
	void longLinesDoNotStopProfileParsing()
	{
		QTemporaryDir directory;
		QVERIFY(directory.isValid());

		const QString plainPath = directory.filePath(QStringLiteral("plain.ds"));
		const QString leadingPath = directory.filePath(QStringLiteral("leading-long-line.ds"));
		const QString middlePath = directory.filePath(QStringLiteral("middle-long-line.ds"));
		QVERIFY(writeDrumSynthFixture(plainPath, drumSynthFixture(100, false, false)));
		QVERIFY(writeDrumSynthFixture(leadingPath, drumSynthFixture(100, true, false)));
		QVERIFY(writeDrumSynthFixture(middlePath, drumSynthFixture(100, false, true)));

		const auto plain = render(plainPath);
		QVERIFY(!plain.empty());

		const auto leading = render(leadingPath);
		QCOMPARE(leading.size(), plain.size());
		QVERIFY2(leading == plain,
			"a long line in front of the settings stopped the DrumSynth profile parser");

		const auto middle = render(middlePath);
		QCOMPARE(middle.size(), leading.size());
		QVERIFY2(middle == leading,
			"a long line between the settings stopped the DrumSynth profile parser");
	}

	/*! Threads that parse .ds files at the same time must see the same
	 *  settings as one thread that parses on its own.
	 *
	 *  The pre-fix parser tokenised every line with strtok(), whose position
	 *  lives in one process-wide pointer, so two threads took each other's
	 *  tokens and read each other's values. The rendezvous in the stand-in
	 *  above makes that interleaving happen at a known point, so the case
	 *  fails on every run with the pre-fix parser instead of depending on the
	 *  scheduler. The over-long line of the fixture keeps the case
	 *  deterministic even where the stand-in is left out. */
	void concurrentParsesMatchSingleThreadedParses()
	{
		QTemporaryDir directory;
		QVERIFY(directory.isValid());

		// Two fixtures with different settings, each of them with a plain file
		// and a file that carries an over-long line in front of the settings.
		QString plainPaths[2];
		QString longPaths[2];
		for (int fixture = 0; fixture < 2; ++fixture)
		{
			plainPaths[fixture] = directory.filePath(QStringLiteral("plain-%1.ds").arg(fixture));
			longPaths[fixture] = directory.filePath(QStringLiteral("long-%1.ds").arg(fixture));
			const int level = fixture == 0 ? 100 : 60;
			QVERIFY(writeDrumSynthFixture(plainPaths[fixture], drumSynthFixture(level, false, false)));
			QVERIFY(writeDrumSynthFixture(longPaths[fixture], drumSynthFixture(level, true, false)));
		}

		// The reference: the same two files, parsed one after the other.
		std::vector<std::vector<int16_t>> referencePlain(2);
		std::vector<std::vector<int16_t>> referenceLong(2);
		for (int fixture = 0; fixture < 2; ++fixture)
		{
			referencePlain[static_cast<std::size_t>(fixture)] = render(plainPaths[fixture]);
			referenceLong[static_cast<std::size_t>(fixture)] = render(longPaths[fixture]);
			QVERIFY(!referencePlain[static_cast<std::size_t>(fixture)].empty());
		}

		constexpr int threadCount = 2;
		std::vector<std::vector<std::vector<int16_t>>> results(
			threadCount, std::vector<std::vector<int16_t>>(2));
		armStrtokRendezvous(threadCount);
		std::vector<std::thread> threads;
		for (int thread = 0; thread < threadCount; ++thread)
		{
			threads.emplace_back([&, thread]() {
				joinStrtokRendezvous();
				const int fixture = thread % 2;
				results[static_cast<std::size_t>(thread)][0] = render(plainPaths[fixture]);
				results[static_cast<std::size_t>(thread)][1] = render(longPaths[fixture]);
			});
		}
		for (std::thread& thread : threads) { thread.join(); }
		disarmStrtokRendezvous();

		for (int thread = 0; thread < threadCount; ++thread)
		{
			const int fixture = thread % 2;
			const auto& plain = results[static_cast<std::size_t>(thread)][0];
			const auto& longLine = results[static_cast<std::size_t>(thread)][1];
			QVERIFY2(!plain.empty(),
				"a thread that parsed a .ds file at the same time as another one read "
				"no settings at all");
			// The frames are the ones a parse on its own produces.
			QCOMPARE(plain.size(), referencePlain[static_cast<std::size_t>(fixture)].size());
			QCOMPARE(longLine.size(), referenceLong[static_cast<std::size_t>(fixture)].size());
			// The samples of the two files of this thread are the ones of a parse
			// on its own: the renderer keeps its state in the thread that runs
			// it, so the two files of a thread are the ones to compare.
			QVERIFY2(longLine == plain,
				"a thread that parsed a .ds file with a long line at the same time as "
				"another thread saw different settings");
		}
	}
};

QTEST_APPLESS_MAIN(DrumSynthTest)

#include "DrumSynthTest.moc"
