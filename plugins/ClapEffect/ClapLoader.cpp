/*
 * ClapLoader.cpp - the platform boundary and the TYPED error path of the CLAP host
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

#include "ClapLoader.h"

#include <QFile>

#include <utility>

#ifdef _WIN32
// The Windows half of the module loader: LoadLibraryW/GetProcAddress. Neither
// MinGW nor MSVC ships <dlfcn.h>, so CLAP hosting could not be built for
// Windows while the loader was dlopen/dlsym only (the v0.2.1-alpha tag run
// failed at that include). NOMINMAX/WIN32_LEAN_AND_MEAN keep <windows.h> from
// defining the min/max macros the host's translation units use std::min/std::max
// for; they are defined here, in the one translation unit that now includes
// <windows.h>, so no other file has to know about them.
#	ifndef WIN32_LEAN_AND_MEAN
#		define WIN32_LEAN_AND_MEAN
#	endif
#	ifndef NOMINMAX
#		define NOMINMAX
#	endif
#	include <windows.h>
#else
#	include <dlfcn.h>
#endif

namespace lmms::clap::loader
{

namespace
{

#ifdef _WIN32
//! Opens the module through the wide path, so an install directory with
//! non-ASCII characters works. The UTF-8 path is still what clap_entry.init()
//! is handed (see load()), as the CLAP spec requires.
auto openPlatform(const QString& path) -> void*
{
	return static_cast<void*>(::LoadLibraryW(reinterpret_cast<LPCWSTR>(path.utf16())));
}

auto symbolPlatform(void* module, const char* name) -> void*
{
	return reinterpret_cast<void*>(::GetProcAddress(static_cast<HMODULE>(module), name));
}

void closePlatform(void* module)
{
	if (module) { ::FreeLibrary(static_cast<HMODULE>(module)); }
}

//! The Win32 equivalent of dlerror(): the message for the last loader failure.
auto platformError() -> QString
{
	const auto code = ::GetLastError();
	if (code == 0) { return QStringLiteral("unknown error"); }
	LPWSTR buffer = nullptr;
	const auto length = ::FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER |
			FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
	const auto message = length > 0
		? QString::fromWCharArray(buffer, static_cast<int>(length)).trimmed()
		: QStringLiteral("error %1").arg(code);
	if (buffer) { ::LocalFree(buffer); }
	return message;
}
#else
auto openPlatform(const QString& path) -> void*
{
	return dlopen(QFile::encodeName(path).constData(), RTLD_NOW | RTLD_LOCAL);
}

auto symbolPlatform(void* module, const char* name) -> void*
{
	return dlsym(module, name);
}

void closePlatform(void* module)
{
	if (module) { dlclose(module); }
}

auto platformError() -> QString
{
	const auto* message = dlerror();
	return QString::fromLocal8Bit(message ? message : "unknown error");
}
#endif

} // namespace

auto token(Code code) -> const char*
{
	switch (code)
	{
	case Code::None:                return "none";
	case Code::LibraryUnavailable:  return "library-unavailable";
	case Code::SymbolMissing:       return "symbol-missing";
	case Code::VersionUnsupported:  return "version-unsupported";
	case Code::EntryInitFailed:     return "entry-init-failed";
	case Code::FactoryMissing:      return "factory-missing";
	case Code::PluginNotFound:      return "plugin-not-found";
	case Code::PluginCreateFailed:  return "plugin-create-failed";
	case Code::PluginInitFailed:    return "plugin-init-failed";
	case Code::ExtensionMissing:    return "extension-missing";
	case Code::NoAudioPorts:        return "no-audio-ports";
	case Code::Count:               break;
	}
	return "unknown";
}

auto summary(Code code) -> const char*
{
	switch (code)
	{
	case Code::None:                return "no failure";
	case Code::LibraryUnavailable:  return "the module file could not be loaded";
	case Code::SymbolMissing:       return "the module does not export clap_entry";
	case Code::VersionUnsupported:  return "the module is a CLAP version this host cannot host";
	case Code::EntryInitFailed:     return "clap_entry.init() failed";
	case Code::FactoryMissing:      return "the module has no clap.plugin-factory";
	case Code::PluginNotFound:      return "the module has no plug-in with that id";
	case Code::PluginCreateFailed:  return "the plug-in could not be created";
	case Code::PluginInitFailed:    return "clap_plugin.init() failed";
	case Code::ExtensionMissing:    return "the plug-in does not implement a required extension";
	case Code::NoAudioPorts:        return "the plug-in has no usable audio ports";
	case Code::Count:               break;
	}
	return "unknown failure";
}

auto Status::message() const -> QString
{
	// Qualified: the members token()/summary() shadow the free functions of the
	// same name, which are the ones with the table in them.
	return detail.isEmpty() ? QString::fromLatin1(lmms::clap::loader::summary(code)) : detail;
}

Module::Module(Module&& other) noexcept : m_handle{other.m_handle} { other.m_handle = nullptr; }

auto Module::operator=(Module&& other) noexcept -> Module&
{
	if (this != &other)
	{
		close();
		m_handle = other.m_handle;
		other.m_handle = nullptr;
	}
	return *this;
}

Module::~Module() { close(); }

auto Module::symbol(const char* name) const -> void*
{
	if (!m_handle) { return nullptr; }
	return symbolPlatform(m_handle, name);
}

void Module::close()
{
	if (m_handle)
	{
		closePlatform(m_handle);
		m_handle = nullptr;
	}
}

auto open(const QString& path, Status& status) -> Module
{
	status = {};
	auto handle = openPlatform(path);
	if (!handle)
	{
		status.code = Code::LibraryUnavailable;
		status.detail = QStringLiteral("Could not load CLAP module '%1': %2").arg(path, platformError());
		return Module{};
	}
	return Module{handle};
}

void Library::reset()
{
	if (initialized && entry) { entry->deinit(); }
	initialized = false;
	entry = nullptr;
	factory = nullptr;
	pathUtf8.clear();
	module.close();
}

auto load(const QString& modulePath, Library& library, Status& status) -> bool
{
	library.reset();
	status = {};

	auto opened = open(modulePath, status);
	if (!opened.isOpen()) { return false; }
	library.module = std::move(opened);
	library.pathUtf8 = QFile::encodeName(modulePath);

	const auto* entry = static_cast<const clap_plugin_entry_t*>(library.module.symbol("clap_entry"));
	if (!entry)
	{
		status.code = Code::SymbolMissing;
		status.detail = QStringLiteral("'%1' does not export clap_entry").arg(modulePath);
		return false;
	}
	library.entry = entry;

	// CLAP 0.x was a development series and is explicitly not compatible.
	if (entry->clap_version.major < 1)
	{
		status.code = Code::VersionUnsupported;
		status.detail = QStringLiteral("'%1' is a CLAP %2.%3 plug-in, which is not compatible")
			.arg(modulePath)
			.arg(entry->clap_version.major)
			.arg(entry->clap_version.minor);
		return false;
	}
	if (!entry->init(library.pathUtf8.constData()))
	{
		status.code = Code::EntryInitFailed;
		status.detail = QStringLiteral("clap_entry.init() failed for '%1'").arg(modulePath);
		return false;
	}
	library.initialized = true;

	const auto* factory = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
	if (!factory)
	{
		status.code = Code::FactoryMissing;
		status.detail = QStringLiteral("'%1' has no %2 factory").arg(modulePath, CLAP_PLUGIN_FACTORY_ID);
		return false;
	}
	library.factory = factory;
	return true;
}

} // namespace lmms::clap::loader
