/*
 * FakeHostModule.cpp - a stand-in for a CLAP/VST3 host module, so the device catalogue's hosted halves run without the SDKs
 *
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

/*! A TEST SUBJECT, not a product plug-in. The control surface lists hosted CLAP and VST3 classes by asking the host
 *  module's own descriptor (src/core/ControlDeviceClap.cpp, ControlDeviceVst3.cpp): the descriptor named
 *  "clapeffect" / "clapinstrument" / "vst3effect" / "vst3instrument" and its SubPluginFeatures::listSubPluginKeys.
 *  The real modules need the CLAP headers or the Steinberg SDK, so a build without them lists nothing and none of
 *  that dispatch runs. This file is compiled once per host name (PLUGIN_NAME, FAKE_HOST_FORMAT = clap | vst3,
 *  FAKE_HOST_TYPE = Effect | Instrument) and lists four keys: one valid class, one whose module file does not
 *  exist (it lists, and is refused when loaded), and two with a missing attribute (never offered). It instantiates
 *  nothing. Never installed; ControlHostedDeviceCatalogueTest points LMMS_PLUGIN_DIR at the directory they build into. */

#include <QCoreApplication>
#include <QString>

#include "Plugin.h"

#include "embed.h"

#if defined(_MSC_VER)
#define PLUGIN_EXPORT __declspec(dllexport)
#else
#define PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

#define FAKE_HOST_CAT2(a, b) a##b
#define FAKE_HOST_CAT(a, b) FAKE_HOST_CAT2(a, b)

namespace lmms
{

namespace
{

//! The attribute that names the class: a CLAP key carries "id", a VST3 key "class".
QString classAttribute() { return QStringLiteral(FAKE_HOST_FORMAT) == QLatin1String("clap") ? QStringLiteral("id") : QStringLiteral("class"); }

class FakeFeatures : public Plugin::Descriptor::SubPluginFeatures
{
public:
	explicit FakeFeatures(Plugin::Type type) : SubPluginFeatures(type) {}

	void listSubPluginKeys(const Plugin::Descriptor* descriptor, KeyList& result) const override
	{
		const QString module = QCoreApplication::applicationFilePath();  // any file that exists
		const QString base = QStringLiteral(LMMS_STRINGIFY(PLUGIN_NAME));
		const auto add = [&](const QString& file, const QString& classId, const QString& display) {
			Key::AttributeMap attributes;
			if (!file.isEmpty()) { attributes.insert(QStringLiteral("file"), file); }
			if (!classId.isEmpty()) { attributes.insert(classAttribute(), classId); }
			if (!display.isEmpty()) { attributes.insert(QStringLiteral("name"), display); }
			result.append(Key(descriptor, display, attributes));
		};
		add(module, base + QStringLiteral(".valid"), base + QStringLiteral(" Valid"));
		add(QStringLiteral("/nonexistent-dir-for-coverage-test/fake.module"), base + QStringLiteral(".gone"), base + QStringLiteral(" Gone"));
		add(QString(), base + QStringLiteral(".nofile"), base + QStringLiteral(" No File"));
		add(module, QString(), base + QStringLiteral(" No Class"));
	}
};

FakeFeatures s_features(Plugin::Type::FAKE_HOST_TYPE);

} // namespace

extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT FAKE_HOST_CAT(PLUGIN_NAME, _plugin_descriptor) =
{
	LMMS_STRINGIFY( PLUGIN_NAME ),
	"Fake hosted-plug-in host",
	QT_TRANSLATE_NOOP( "PluginBrowser", "test subject: lists hosted classes that do not exist" ),
	"Zene Studio test suite",
	0x0100,
	Plugin::Type::FAKE_HOST_TYPE,
	nullptr,
	nullptr,
	&s_features,
};

PLUGIN_EXPORT Plugin* lmms_plugin_main( Model*, void* )
{
	return nullptr;
}

} // extern "C"

} // namespace lmms
