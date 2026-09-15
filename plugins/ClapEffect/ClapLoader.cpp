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

#include <iterator>
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

// --- the platform boundary ---------------------------------------------------
// The two bodies below are the loader the host has always used: the POSIX half
// is dlopen/dlsym/dlclose/dlerror exactly as it was in ClapHost.cpp before this
// file existed (same call text, same RTLD_NOW | RTLD_LOCAL), and the Windows
// half is the port, opening the module through the wide path so an install
// directory with non-ASCII characters works.
#ifdef _WIN32
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

// --- the code table ----------------------------------------------------------
/*!
 * The published table behind token() and summary(): one row per Code, in
 * declaration order. A table rather than a switch for two reasons -- the
 * compiler checks the row COUNT against the enum (a new code with no row does
 * not compile), and neither lookup is a 12-way branch, which the complexity
 * ratchet counts.
 */
struct CodeText
{
	const char* token;
	const char* summary;
};

constexpr CodeText kCodes[] = {
	{"none", "no failure"},
	{"library-unavailable", "the module file could not be loaded"},
	{"symbol-missing", "the module does not export clap_entry"},
	{"version-unsupported", "the module is a CLAP version this host cannot host"},
	{"entry-init-failed", "clap_entry.init() failed"},
	{"factory-missing", "the module has no clap.plugin-factory"},
	{"plugin-not-found", "the module has no plug-in with that id"},
	{"plugin-create-failed", "the plug-in could not be created"},
	{"plugin-init-failed", "clap_plugin.init() failed"},
	{"extension-missing", "the plug-in does not implement a required extension"},
	{"no-audio-ports", "the plug-in has no usable audio ports"},
};

//! One row per code, and Code::Count is the row count: adding a code without a
//! token is a compile error here rather than an "unknown" at a call site.
static_assert(std::size(kCodes) == static_cast<std::size_t>(Code::Count),
	"every loader::Code needs a row in kCodes (token + summary)");

} // namespace

auto token(Code code) -> const char*
{
	const auto index = static_cast<std::size_t>(code);
	return index < std::size(kCodes) ? kCodes[index].token : "unknown";
}

auto summary(Code code) -> const char*
{
	const auto index = static_cast<std::size_t>(code);
	return index < std::size(kCodes) ? kCodes[index].summary : "unknown failure";
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
