/*
 * RemotePluginAudioPortsTest.cpp - buffer attach/detach lifecycle of the
 *                                  remote plugin audio-ports controller
 *
 * Copyright (c) 2026 LMMS developers
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

#include "RemotePluginAudioPorts.h"

#include <QtTest>

#include "AudioPortsModel.h"
#include "RemotePlugin.h"

using namespace lmms;

namespace
{

//! AudioPortsModel is abstract (bufferPropertiesChanging)
class TestAudioPortsModel : public AudioPortsModel
{
public:
	using AudioPortsModel::AudioPortsModel;

protected:
	void bufferPropertiesChanging(ch_cnt_t, ch_cnt_t, f_cnt_t) override {}
};

//! Concrete controller; exposes the protected buffer pointer for assertions
class TestAudioPortsController : public RemotePluginAudioPortsController
{
public:
	explicit TestAudioPortsController(AudioPortsModel& model)
		: RemotePluginAudioPortsController{model}
	{
	}

	void activate(f_cnt_t /*frames*/) override { ++m_activateCalls; }

	auto buffers() const -> RemotePlugin* { return m_buffers; }
	auto activateCalls() const -> int { return m_activateCalls; }

private:
	int m_activateCalls = 0;
};

} // namespace

class RemotePluginAudioPortsTest : public QObject
{
	Q_OBJECT

private slots:
	//! RemotePlugin's ctor attaches its buffers to the ports and its dtor
	//! detaches them again; both are the controller's documented entry points.
	void ConnectAndDisconnectBuffers()
	{
		TestAudioPortsModel model{false};
		TestAudioPortsController controller{model};

		QCOMPARE(controller.buffers(), nullptr);
		QCOMPARE(&controller.audioPortsModel(), static_cast<AudioPortsModel*>(&model));

		{
			RemotePlugin plugin{controller};
			QCOMPARE(controller.buffers(), &plugin);
		}
		QCOMPARE(controller.buffers(), nullptr);

		controller.activate(512);
		QCOMPARE(controller.activateCalls(), 1);
	}
};

QTEST_APPLESS_MAIN(RemotePluginAudioPortsTest)

#include "RemotePluginAudioPortsTest.moc"
