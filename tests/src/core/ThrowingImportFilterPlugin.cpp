/*
 * ThrowingImportFilterPlugin.cpp - test importer that throws from tryImport.
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of Zene Studio (an LMMS-derived product).
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include <stdexcept>

#include "ImportFilter.h"

#if defined(_MSC_VER)
#define IMPORT_FILTER_TEST_EXPORT __declspec(dllexport)
#else
#define IMPORT_FILTER_TEST_EXPORT __attribute__((visibility("default")))
#endif

namespace lmms
{

namespace
{

Plugin::Descriptor s_descriptor{
	"throwingimportfilter",
	"Throwing import filter",
	"Test fixture for import exception handling",
	"Zene Studio test",
	0x0100,
	Plugin::Type::ImportFilter,
	nullptr,
	"",
	nullptr
};

class ThrowingImportFilter final : public ImportFilter
{
public:
	explicit ThrowingImportFilter(const QString& file) : ImportFilter(file, &s_descriptor) {}
	gui::PluginView* instantiateView(QWidget*) override { return nullptr; }

protected:
	bool tryImport(TrackContainer*) override
	{
		throw std::runtime_error("test importer failure");
	}
};

} // namespace

extern "C"
{

IMPORT_FILTER_TEST_EXPORT Plugin::Descriptor throwingimportfilter_plugin_descriptor = s_descriptor;

IMPORT_FILTER_TEST_EXPORT Plugin* lmms_plugin_main(Model*, void* data)
{
	return new ThrowingImportFilter(QString::fromUtf8(static_cast<const char*>(data)));
}

} // extern "C"

} // namespace lmms
