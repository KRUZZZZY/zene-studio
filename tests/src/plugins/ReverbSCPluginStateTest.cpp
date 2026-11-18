/*
 * ReverbSCPluginStateTest.cpp - the DSP state lifetime of the ReverbSC effect
 *
 * This file is part of Zene Studio - https://zene.studio
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
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/*
 * The effect's own sources (ReverbSC.cpp, ReverbSCControls.cpp and the three
 * soundpipe C files) are compiled into this test binary, so the test drives
 * the REAL plugin code. The soundpipe C files are built with malloc/calloc/
 * free redirected to the shim below, which is the seam this test uses:
 *
 *  - a failure can be injected into ONE allocation of a known size, so the
 *    effect can be constructed while an allocation is failing, and
 *  - every free of a soundpipe block is counted, so the test can prove that
 *    the DSP state the audio thread may be using is NOT freed underneath it.
 *
 * Each scenario runs in a child process (the parent passes --child <case>),
 * because the pre-fix code crashes the process when an allocation fails, and
 * a crashed child is reported as a failed case instead of taking the whole
 * test binary down with it.
 */

#include <QtTest>

#include <QCoreApplication>
#include <QProcess>

#include <array>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include "AudioBus.h"
#include "Engine.h"
#include "SampleFrame.h"

#include "ReverbSC.h"

using namespace lmms;

namespace
{

constexpr f_cnt_t RenderFrames = 64;

//! Size of the soundpipe allocations the test can fail on demand.
std::size_t g_failSize = 0;
bool g_failArmed = false;
int g_allocCount = 0;
int g_freeCount = 0;

void resetAllocators()
{
	g_failSize = 0;
	g_failArmed = false;
	g_allocCount = 0;
	g_freeCount = 0;
}

void armAllocationFailure(std::size_t size)
{
	g_failSize = size;
	g_failArmed = true;
}

//! A stereo track channel pair the effect can be rendered into.
class TrackChannel
{
public:
	TrackChannel() : m_frames{RenderFrames}
	{
		m_pointers[0] = m_frames.data();
	}

	auto bus() -> AudioBus& { return m_bus; }

	void fill(float left, float right)
	{
		for (f_cnt_t frame = 0; frame < RenderFrames; ++frame)
		{
			m_frames[frame] = SampleFrame{left, right};
		}
	}

	bool unchanged() const
	{
		for (f_cnt_t frame = 0; frame < RenderFrames; ++frame)
		{
			if (m_frames[frame][0] != m_inputLeft || m_frames[frame][1] != m_inputRight)
			{
				return false;
			}
		}
		return true;
	}

	void rememberInput(float left, float right)
	{
		m_inputLeft = left;
		m_inputRight = right;
	}

private:
	std::vector<SampleFrame> m_frames;
	std::array<SampleFrame*, 1> m_pointers{};
	AudioBus m_bus{m_pointers.data(), 1, RenderFrames};
	float m_inputLeft = 0.f;
	float m_inputRight = 0.f;
};

//! Renders one block into the effect and reports whether the block survived.
bool renderIntoBypassedEffect(ReverbSCEffect& effect)
{
	TrackChannel channel;
	channel.fill(0.25f, -0.25f);
	channel.rememberInput(0.25f, -0.25f);

	// processAudioBuffer() is public on Effect and protected on AudioPlugin,
	// so the call goes through the Effect interface, like the audio engine does.
	static_cast<Effect*>(&effect)->processAudioBuffer(channel.bus());

	return channel.unchanged();
}

int runContextAllocationFailure()
{
	resetAllocators();
	armAllocationFailure(sizeof(sp_data));

	std::unique_ptr<ReverbSCEffect> effect{new ReverbSCEffect(nullptr, nullptr)};

	// The context allocation failed: the effect must stay alive and bypassed.
	if (!renderIntoBypassedEffect(*effect))
	{
		qWarning("ReverbSC: a failed context allocation did not leave the effect bypassed");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

int runStateAllocationFailure()
{
	resetAllocators();
	armAllocationFailure(sizeof(sp_revsc));

	std::unique_ptr<ReverbSCEffect> effect{new ReverbSCEffect(nullptr, nullptr)};

	// The reverb state allocation failed: the effect must stay alive and bypassed.
	if (!renderIntoBypassedEffect(*effect))
	{
		qWarning("ReverbSC: a failed reverb state allocation did not leave the effect bypassed");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

int runSampleRateChange()
{
	resetAllocators();

	std::unique_ptr<ReverbSCEffect> effect{new ReverbSCEffect(nullptr, nullptr)};

	TrackChannel channel;
	channel.fill(0.25f, -0.25f);
	static_cast<Effect*>(effect.get())->processAudioBuffer(channel.bus());

	const int freesBeforeChange = g_freeCount;

	// The sample rate changed while the effect is in the graph. The DSP state
	// the audio thread may be using must NOT be freed here: it is replaced by
	// a prepared state and swapped in at an audio boundary.
	effect->changeSampleRate();

	if (g_freeCount != freesBeforeChange)
	{
		qWarning("ReverbSC: the sample-rate change freed %d soundpipe block(s) while the "
			"audio thread could still be using the state",
			g_freeCount - freesBeforeChange);
		return EXIT_FAILURE;
	}

	// The effect still renders after the change.
	channel.fill(0.25f, -0.25f);
	static_cast<Effect*>(effect.get())->processAudioBuffer(channel.bus());

	effect.reset();

	// Every soundpipe block the effect allocated is released again.
	if (g_freeCount != g_allocCount)
	{
		qWarning("ReverbSC: %d soundpipe block(s) allocated, %d freed",
			g_allocCount, g_freeCount);
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

} // namespace

extern "C" void* sp_test_malloc(std::size_t size)
{
	++g_allocCount;
	if (g_failArmed && size == g_failSize)
	{
		g_failArmed = false;
		return nullptr;
	}
	return std::malloc(size);
}

extern "C" void* sp_test_calloc(std::size_t count, std::size_t size)
{
	++g_allocCount;
	if (g_failArmed && size == g_failSize)
	{
		g_failArmed = false;
		return nullptr;
	}
	return std::calloc(count, size);
}

extern "C" void sp_test_free(void* pointer)
{
	++g_freeCount;
	std::free(pointer);
}

class ReverbSCPluginStateTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase();
	void cleanupTestCase();
	void failedContextAllocationLeavesEffectBypassed();
	void failedStateAllocationLeavesEffectBypassed();
	void sampleRateChangeKeepsLiveStateAlive();

private:
	//! Runs one scenario in a child process: a crash is a failed case.
	void runChildCase(const char* scenario)
	{
		QProcess child;
		child.start(QCoreApplication::applicationFilePath(),
			{QStringLiteral("--child"), QString::fromLatin1(scenario)});
		QVERIFY2(child.waitForStarted(), qPrintable(child.errorString()));
		QVERIFY2(child.waitForFinished(30000), qPrintable(child.errorString()));
		if (child.exitStatus() != QProcess::NormalExit)
		{
			qWarning("ReverbSC: the %s case terminated abnormally (crash)", scenario);
		}
		QCOMPARE(child.exitStatus(), QProcess::NormalExit);
		QCOMPARE(child.exitCode(), EXIT_SUCCESS);
	}
};

void ReverbSCPluginStateTest::initTestCase()
{
	Engine::init(true);
}

void ReverbSCPluginStateTest::cleanupTestCase()
{
	Engine::destroy();
}

void ReverbSCPluginStateTest::failedContextAllocationLeavesEffectBypassed()
{
	runChildCase("context-failure");
}

void ReverbSCPluginStateTest::failedStateAllocationLeavesEffectBypassed()
{
	runChildCase("state-failure");
}

void ReverbSCPluginStateTest::sampleRateChangeKeepsLiveStateAlive()
{
	runChildCase("sample-rate");
}

int main(int argc, char** argv)
{
	if (argc == 3 && std::strcmp(argv[1], "--child") == 0)
	{
		QCoreApplication app(argc, argv);
		Engine::init(true);
		int result = EXIT_FAILURE;
		if (std::strcmp(argv[2], "context-failure") == 0)
		{
			result = runContextAllocationFailure();
		}
		else if (std::strcmp(argv[2], "state-failure") == 0)
		{
			result = runStateAllocationFailure();
		}
		else if (std::strcmp(argv[2], "sample-rate") == 0)
		{
			result = runSampleRateChange();
		}
		Engine::destroy();
		return result;
	}

	QCoreApplication app(argc, argv);
	ReverbSCPluginStateTest test;
	return QTest::qExec(&test, argc, argv);
}

#include "ReverbSCPluginStateTest.moc"
