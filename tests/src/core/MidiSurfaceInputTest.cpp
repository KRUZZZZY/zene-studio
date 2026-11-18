/*
 * MidiSurfaceInputTest.cpp - a MIDI surface reaches the audio engine's MIDI client only through a null
 *                            check: with no engine (early startup, teardown) it stays closed and reports
 *                            "not subscribed" instead of dereferencing a null engine.
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

#include <QtTest>

#include "Engine.h"
#include "MidiEvent.h"
#include "MidiSurfaceInput.h"

using namespace lmms;

namespace
{

struct TestItem
{
	int value = 0;
};

// Decodes nothing: the port handling in the base class is what is under test.
class TestSurface : public MidiSurfaceInput<TestItem>
{
public:
	~TestSurface() override { close(); }

protected:
	bool decode(const MidiEvent&, TestItem*) override { return false; }
	void apply(const TestItem&) override {}
};

} // namespace

class MidiSurfaceInputTest : public QObject
{
	Q_OBJECT

private slots:
	// The order the app really has: a surface opened on the engine outlives Engine::destroy(), which does
	// not close the launcher's or the remote's port. The port is leaked on purpose: destroying it would
	// unregister it from the MIDI client the engine just freed.
	void subscribedAfterEngineTeardownReportsNotSubscribed()
	{
		Engine::init(true);
		auto* surface = new TestSurface;
		surface->open(QStringLiteral("Test"), QStringLiteral("no-such-port"), QString());
		QVERIFY(surface->isOpen());
		Engine::destroy();
		QVERIFY(Engine::audioEngine() == nullptr);
		QVERIFY(!surface->subscribed());
	}

	// No Engine::init(): the engine pointer is null, as it is before startup.
	void openWithoutAudioEngineLeavesSurfaceClosed()
	{
		QVERIFY(Engine::audioEngine() == nullptr);
		TestSurface surface;
		surface.open(QStringLiteral("Test"), QStringLiteral("no-such-port"), QString());
		QVERIFY(!surface.isOpen());
		QVERIFY(!surface.subscribed());
		QCOMPARE(surface.input(), QStringLiteral("no-such-port"));
	}

	// With an engine, an input the MIDI client does not have leaves the surface open and not subscribed.
	void unknownInputIsOpenButNotSubscribed()
	{
		Engine::init(true);
		{
			TestSurface surface;
			surface.open(QStringLiteral("Test"), QStringLiteral("no-such-port"), QString());
			QVERIFY(surface.isOpen());
			QVERIFY(!surface.subscribed());
		}
		Engine::destroy();
	}
};

QTEST_GUILESS_MAIN(MidiSurfaceInputTest)
#include "MidiSurfaceInputTest.moc"
