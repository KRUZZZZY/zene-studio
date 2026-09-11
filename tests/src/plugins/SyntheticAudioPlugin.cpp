/*
 * SyntheticAudioPlugin.cpp - test double for the AudioPlugin legacy bridge
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

/*
 * A minimal plugin module that subclasses DefaultEffect (i.e. the Effect
 * specialization of AudioPlugin) so AudioPluginTest can drive the parts of
 * AudioPlugin's Effect specialization that no production plugin reaches.
 *
 * The test only ever sees the lmms::Effect base class, so all knobs that
 * steer the behaviour under test are exported as C functions and resolved
 * through QLibrary. That keeps the test translation unit free of the class
 * definition: instantiating AudioPlugin there would emit a call to the
 * legacy `AudioProcessingMethod::processImpl` overload that intentionally
 * has no definition.
 */

#include "AudioPlugin.h"
#include "EffectControls.h"
#include "Plugin.h"
#include "embed.h"

// MSVC has no __attribute__. Mirror tests/reference/plugin_export.h — the header the
// ported reference plugins use — so this test double compiles with cl.exe as well as
// with GCC/Clang (msvc-x64 failed on the bare attribute with C3861).
#if defined(_MSC_VER)
#define PLUGIN_EXPORT __declspec(dllexport)
#else
#define PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

namespace lmms
{

extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT synthetic_plugin_descriptor =
{
	"synthetic",
	"Synthetic",
	QT_TRANSLATE_NOOP("PluginBrowser", "AudioPlugin test double"),
	"LMMS test suite",
	0x0100,
	Plugin::Type::Effect,
	new PixmapLoader("lmms-plugin-logo"),
	nullptr,
};

} // extern "C"

namespace
{

//! 0 = Continue, 1 = ContinueIfNotQuiet, 2 = Sleep, anything else is cast to
//! an out-of-range ProcessStatus so the switch's default arm runs.
int g_statusCode = 0;
int g_pendingLockFailures = 0;
int g_lockCalls = 0;
int g_processCalls = 0;
int g_bypassCalls = 0;

//! Minimal EffectControls: Effect::saveSettings/loadSettings need a real
//! controls object (they call saveState()/nodeName() on it).
class SyntheticControls final : public EffectControls
{
public:
	explicit SyntheticControls(Effect* effect) :
		EffectControls{effect}
	{
	}

	auto controlCount() -> int override { return 0; }
	auto createView() -> gui::EffectControlDialog* override { return nullptr; }
	auto nodeName() const -> QString override { return QStringLiteral("syntheticcontrols"); }

	void saveSettings(QDomDocument&, QDomElement&) override {}
	void loadSettings(const QDomElement&) override {}
};

class SyntheticEffect final : public DefaultEffect
{
public:
	SyntheticEffect(Model* parent, const Plugin::Descriptor::SubPluginFeatures::Key* key) :
		AudioPlugin{&synthetic_plugin_descriptor, parent, key}
	{
	}

	auto controls() -> EffectControls* override { return &m_controls; }

protected:
	auto processLock() -> bool override
	{
		++g_lockCalls;
		if (g_pendingLockFailures > 0)
		{
			--g_pendingLockFailures;
			return false;
		}
		return true;
	}

	void processUnlock() override {}

	auto processImpl(InterleavedBufferView<float, 2> inOut) -> ProcessStatus override
	{
		++g_processCalls;
		for (f_cnt_t f = 0; f < inOut.frames(); ++f)
		{
			auto& frame = inOut.sampleFrameAt(f);
			frame = frame * 0.5f;
		}
		return static_cast<ProcessStatus>(g_statusCode);
	}

	void processBypassedImpl() override { ++g_bypassCalls; }

private:
	SyntheticControls m_controls{this};
};

//! Variant that does not override processBypassedImpl(), so bypassing runs
//! AudioPlugin's default (empty) implementation.
class PlainSyntheticEffect final : public DefaultEffect
{
public:
	PlainSyntheticEffect(Model* parent, const Plugin::Descriptor::SubPluginFeatures::Key* key) :
		AudioPlugin{&synthetic_plugin_descriptor, parent, key}
	{
	}

	auto controls() -> EffectControls* override { return &m_controls; }

protected:
	auto processImpl(InterleavedBufferView<float, 2> inOut) -> ProcessStatus override
	{
		Q_UNUSED(inOut)
		return ProcessStatus::Continue;
	}

private:
	SyntheticControls m_controls{this};
};

} // namespace

extern "C"
{

PLUGIN_EXPORT Plugin* lmms_plugin_main(Model* parent, void* data)
{
	return new SyntheticEffect{parent, static_cast<const Plugin::Descriptor::SubPluginFeatures::Key*>(data)};
}

PLUGIN_EXPORT Plugin* synthetic_create_plain(Model* parent, void* data)
{
	return new PlainSyntheticEffect{parent, static_cast<const Plugin::Descriptor::SubPluginFeatures::Key*>(data)};
}

PLUGIN_EXPORT void synthetic_set_status(int code) { g_statusCode = code; }

PLUGIN_EXPORT void synthetic_set_lock_failures(int count) { g_pendingLockFailures = count; }

PLUGIN_EXPORT void synthetic_reset_counts() { g_lockCalls = g_processCalls = g_bypassCalls = 0; }

PLUGIN_EXPORT void synthetic_get_counts(int* lockCalls, int* processCalls, int* bypassCalls)
{
	if (lockCalls) { *lockCalls = g_lockCalls; }
	if (processCalls) { *processCalls = g_processCalls; }
	if (bypassCalls) { *bypassCalls = g_bypassCalls; }
}

} // extern "C"

} // namespace lmms
