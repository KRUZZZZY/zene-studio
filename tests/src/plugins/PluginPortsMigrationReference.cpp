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
#include <memory>
#include <vector>

#include <QCoreApplication>
#include <QLibrary>
#include <QString>

#include "Engine.h"
#include "InstrumentTrack.h"
#include "LmmsTypes.h"
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
		{"compressor", PART_C_REF_compressor},
		{"crossovereq", PART_C_REF_crossovereq},
		{"dynamicsprocessor", PART_C_REF_dynamicsprocessor},
		{"lomm", PART_C_REF_lomm},
		{"multitapecho", PART_C_REF_multitapecho},
		{"reverbsc", PART_C_REF_reverbsc},
		{"stereoenhancer", PART_C_REF_stereoenhancer},
		{"stereomatrix", PART_C_REF_stereomatrix},
		// Slice 3 (task #589)
		{"dispersion", PART_C_REF_dispersion},
		{"vectorscope", PART_C_REF_vectorscope},
		{"analyzer", PART_C_REF_analyzer},
		{"granularpitchshifter", PART_C_REF_granularpitchshifter},
		{"eq", PART_C_REF_eq},
	};
	return modules;
}

struct ReferenceInstrument
{
	const char* plugin;
	const char* path;
};

/*!
 * One reference module per migrated instrument (slice 4, task #589). The paths
 * are injected by tests/CMakeLists.txt via $<TARGET_FILE:...> so they always
 * point at the freshly built reference modules.
 */
auto referenceInstruments() -> const std::vector<ReferenceInstrument>&
{
	static const std::vector<ReferenceInstrument> instruments{
		{"freeboy", PART_C_REF_freeboy},
		{"nes", PART_C_REF_nes},
		{"sid", PART_C_REF_sid},
		{"opulenz", PART_C_REF_opulenz},
		{"sfxr", PART_C_REF_sfxr},
		{"bitinvader", PART_C_REF_bitinvader},
		{"watsyn", PART_C_REF_watsyn},
		{"xpressive", PART_C_REF_xpressive},
		{"vibedstrings", PART_C_REF_vibedstrings},
		{"kicker", PART_C_REF_kicker},
	};
	return instruments;
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
	renders.reserve(referenceModules().size() + referenceInstruments().size());

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

	for (const auto& m : referenceInstruments())
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

		// Instruments are parented to an InstrumentTrack, exactly like the
		// engine creates them (InstrumentTrack's constructor).
		auto track = std::make_unique<lmms::InstrumentTrack>(lmms::Engine::getSong());
		auto* inst = static_cast<lmms::Instrument*>(entry(track.get(), nullptr));
		if (inst == nullptr)
		{
			std::fprintf(stderr, "reference: %s returned no instrument\n", m.plugin);
			return 2;
		}

		partc::applyInstrumentTestSettings(*inst, m.plugin);

		partc::PluginRender render;
		render.name = QString::fromUtf8(m.plugin);
		render.samples = partc::renderInstrumentInFreshThread(*inst, frames, lmms::DefaultKey);
		render.checksum = partc::checksum(render.samples);

		std::printf("reference %s: %zu samples, sha256=%s\n", m.plugin, render.samples.size(),
			qPrintable(render.checksum));

		renders.push_back(std::move(render));
		delete inst;
		track.reset();
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
