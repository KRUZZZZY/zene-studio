/*
 * PluginPortsMigrationReference.cpp - Part C (task #585) reference renderer
 *
 * Renders the *pre-migration* plugin DSP so that the migrated plugins can be
 * compared against it sample-exactly. The plugin sources are the verbatim
 * base-commit (4ac5c3e38) files in tests/reference/ (blob hashes in
 * tests/reference/ORIGIN.tsv); they are built into the partc_ref_* modules by
 * tests/CMakeLists.txt and loaded here through the normal plugin entry point,
 * exactly like PluginPortsMigrationTest loads the migrated modules.
 *
 * Usage: PluginPortsMigrationReference <output-file>
 *
 * This file is part of LMMS - https://lmms.io
 * Licensed under the GNU General Public License version 2 or later.
 */

#include "PluginPortsHarness.h"

#include <cstdio>
#include <vector>

#include <QCoreApplication>
#include <QLibrary>
#include <QString>

#include "Engine.h"
#include "Plugin.h"

namespace
{

using MainFn = lmms::Plugin* (*)(lmms::Model*, void*);

struct ReferenceModule
{
	const char* plugin;
	const char* path;
};

/*!
 * One reference module per migrated plugin. The paths are injected by
 * tests/CMakeLists.txt via $<TARGET_FILE:...> so they always point at the
 * freshly built reference modules.
 */
auto referenceModules() -> const std::vector<ReferenceModule>&
{
	static const std::vector<ReferenceModule> modules{
		{"amplifier", PART_C_REF_amplifier},
		{"bassbooster", PART_C_REF_bassbooster},
		{"bitcrush", PART_C_REF_bitcrush},
		{"dualfilter", PART_C_REF_dualfilter},
		{"waveshaper", PART_C_REF_waveshaper},
		{"flanger", PART_C_REF_flanger},
		{"delay", PART_C_REF_delay},
	};
	return modules;
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app{argc, argv};

	if (argc != 2)
	{
		std::fprintf(stderr, "usage: %s <output-file>\n", argv[0]);
		return 1;
	}
	const QString outputPath = QString::fromLocal8Bit(argv[1]);

	lmms::Engine::init(true);
	lmms::Engine::audioEngine()->audioDev()->stopProcessing();

	const lmms::f_cnt_t frames = lmms::Engine::audioEngine()->framesPerPeriod();
	if (frames <= 0)
	{
		std::fprintf(stderr, "reference: engine framesPerPeriod() == %d\n", static_cast<int>(frames));
		return 1;
	}
	std::printf("reference: engine framesPerPeriod=%d, buffers per plugin=%d\n", static_cast<int>(frames),
		partc::Buffers);

	std::vector<partc::PluginRender> renders;
	renders.reserve(referenceModules().size());

	for (const auto& m : referenceModules())
	{
		QLibrary lib{QString::fromUtf8(m.path)};
		lib.setLoadHints(QLibrary::PreventUnloadHint);
		if (!lib.load())
		{
			std::fprintf(stderr, "reference: failed to load %s: %s\n", m.path,
				qPrintable(lib.errorString()));
			return 2;
		}

		auto entry = reinterpret_cast<MainFn>(lib.resolve("lmms_plugin_main"));
		if (entry == nullptr)
		{
			std::fprintf(stderr, "reference: %s has no lmms_plugin_main\n", m.path);
			return 2;
		}

		auto* fx = static_cast<lmms::Effect*>(entry(nullptr, nullptr));
		if (fx == nullptr)
		{
			std::fprintf(stderr, "reference: %s returned no effect\n", m.plugin);
			return 2;
		}

		partc::applyTestSettings(*fx, m.plugin);

		partc::PluginRender render;
		render.name = QString::fromUtf8(m.plugin);
		render.samples = partc::renderInFreshThread(*fx, frames);
		render.checksum = partc::checksum(render.samples);

		std::printf("reference %s: %zu samples, sha256=%s\n", m.plugin, render.samples.size(),
			qPrintable(render.checksum));

		renders.push_back(std::move(render));
		delete fx;
	}

	if (!partc::writeRenders(outputPath, renders))
	{
		std::fprintf(stderr, "reference: failed to write %s\n", qPrintable(outputPath));
		return 3;
	}

	std::printf("reference: wrote %zu plugin renders to %s\n", renders.size(),
		qPrintable(outputPath));
	return 0;
}
