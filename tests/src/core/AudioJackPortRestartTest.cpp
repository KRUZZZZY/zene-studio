/*
 * AudioJackPortRestartTest.cpp - JACK port arrays are replaced on client restart
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
 *
 * LMMS is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <QtTest>

#include <cstdint>
#include <vector>

#include "AudioJack.h"

namespace lmms
{

class AudioJackPortRestartTest : public QObject
{
	Q_OBJECT
private slots:
	void newClientStateDropsPortsFromTheDeadClient()
	{
		// These are the stale vectors left when JACK shuts a client down. The
		// sentinels stand for pointers owned by that dead client.
		std::vector<jack_port_t*> outputPorts(2,
			reinterpret_cast<jack_port_t*>(std::uintptr_t{1}));
		std::vector<jack_port_t*> inputPorts(2,
			reinterpret_cast<jack_port_t*>(std::uintptr_t{2}));
		std::vector<jack_default_audio_sample_t*> outputBuffers(2,
			reinterpret_cast<jack_default_audio_sample_t*>(std::uintptr_t{3}));

		AudioJack::clearPortState(outputPorts, inputPorts, outputBuffers);

		QVERIFY(outputPorts.empty());
		QVERIFY(inputPorts.empty());
		QVERIFY(outputBuffers.empty());
	}
};

} // namespace lmms

QTEST_GUILESS_MAIN(lmms::AudioJackPortRestartTest)
#include "AudioJackPortRestartTest.moc"
