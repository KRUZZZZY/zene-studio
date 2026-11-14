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
	void rejectsOverlongEnvelopeLevel();

	void ignoresEnvelopePointsBeyondSupportedCapacity();

	void zeroDurationToneUsesItsStartingFrequency_data();

	void zeroDurationToneUsesItsStartingFrequency();

	void rejectsNonfiniteToneFrequency_data();

	void rejectsNonfiniteToneFrequency();

	void rejectsNonfiniteEnvelopeLevel_data();

	void rejectsNonfiniteEnvelopeLevel();

	void emptyEnvelopeFieldIsZeroNotAStaleToken_data();

	void emptyEnvelopeFieldIsZeroNotAStaleToken();

	void emptyEnvelopeFieldIgnoresAnEarlierParse();

	void noiseBandStepCountStaysDefinedForOutOfRangeQuotients();

	void negativeNoiseBandFrequencyRendersLikeTheBandOff();

	void refusesAnEnvelopeWhoseRoundedLengthOverflows();

	/*! An envelope past the renderer's own longest hold must be refused before
	 *  any buffer is sized from it. At the largest stretch (Stretch=1000, the
	 *  10x clamp) a 500000 sample envelope asks for about 5M frames; the
	 *  unbounded renderer allocated and rendered all of them. */
	void refusesAnEnvelopeLongerThanTheRendererHold();

	/*! The longest envelope the shipped library uses (data/samples/drumsynth/misc/verylong.ds,
	 *  438020 samples) is under the renderer's hold, so it must still render at the
	 *  largest stretch. */
	void rendersTheLongestShippedEnvelopeAtMaximumStretch();

	void mapsAFractionalStepAboveMaxToTheSafeMaximumLength();

	/*! A setting value longer than its destination buffer is cut to that buffer,
	 *  and a missing setting is read as nothing, not as whatever the buffer held.
	 *
	 *  The Version value is copied into a 32 byte buffer. Padding after the
	 *  version name must leave the version and the rendered samples unchanged,
	 *  and a file without a Version key must not render. */
	void boundedProfileValuesKeepTheirSettings();

	/*! An over-long line must not stop the profile parser.
	 *
	 *  The pre-fix parser read every line into a 200 byte buffer: an over-long
	 *  line set the stream's failure state, the scan ended there and the
	 *  settings behind it fell back to their defaults without any error. The
	 *  comparison is on the complete rendered samples, because the settings
	 *  that fall back can leave the frame count untouched. */
	void longLinesDoNotStopProfileParsing();

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
	void concurrentParsesMatchSingleThreadedParses();
};

#include "DrumSynthTestParts1.h"

QTEST_APPLESS_MAIN(DrumSynthTest)

#include "DrumSynthTest.moc"
