/*
 * ClapLoader.h - the platform boundary and the TYPED error path of the CLAP host
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

#ifndef LMMS_CLAP_LOADER_H
#define LMMS_CLAP_LOADER_H

#include <QByteArray>
#include <QString>

#include <cstdint>

#include <clap/clap.h>

/*!
 * The CLAP host's module loader: the ONE place the host touches the platform's
 * dynamic loader, and the ONE place a failed load is given a type.
 *
 * WHY THIS FILE EXISTS. ClapHost.cpp used to carry the platform boundary
 * inline (dlopen/dlsym, then a `#ifdef _WIN32` half with
 * LoadLibraryW/GetProcAddress) and to report every failure through a single
 * `QString* error`, which a caller could only read as prose. A scan of a
 * directory full of modules therefore could not tell "no such file" from "not
 * a CLAP module" from "a CLAP 0.x plug-in this host refuses" -- they all
 * arrived as "'<path>' is not a usable CLAP module" -- and a caller that
 * wanted to act on the difference had nothing to match on.
 *
 * The contract here is: every distinct way a load can fail has its own Code,
 * carries a stable machine token, and is proven by
 * tests/src/plugins/ClapLoaderErrorTest.cpp against real modules built to fail
 * in exactly one way each (tests/data/clap-test-plugin). Nothing in this file
 * ever returns a null pointer for the caller to dereference: the ladder stops
 * at the first step that fails, records why, and leaves what it opened for the
 * caller to unwind in one place (Library::reset()).
 *
 * The POSIX half is the loader the host has always used, verbatim: same
 * dlopen() mode flags (RTLD_NOW | RTLD_LOCAL), same dlsym/dlclose/dlerror. The
 * Windows half opens the module through the wide path (LoadLibraryW), so an
 * install directory with non-ASCII characters works, while the UTF-8 path is
 * still what clap_entry.init() receives, as the CLAP spec requires.
 */
namespace lmms::clap::loader
{

//! Every distinct way loading a module or one of its plug-ins can fail.
enum class Code
{
	None,                //!< no failure
	LibraryUnavailable,  //!< the module file could not be opened (missing, not loadable, no permission)
	SymbolMissing,       //!< the module opened but exports no `clap_entry`
	VersionUnsupported,  //!< `clap_entry` reports a clap_version this host cannot host (major < 1)
	EntryInitFailed,     //!< `clap_entry.init()` returned false
	FactoryMissing,      //!< the entry has no clap.plugin-factory extension
	PluginNotFound,      //!< the factory holds no plug-in with the requested id
	PluginCreateFailed,  //!< create_plugin() returned null
	PluginInitFailed,    //!< clap_plugin.init() returned false
	ExtensionMissing,    //!< the plug-in does not implement an extension the host requires
	NoAudioPorts,        //!< the plug-in's audio-ports description is empty or unusable
	Count,               //!< not a failure: the number of codes above (the tests walk them)
};

//! Stable, never-localised, never-reformatted machine name of a code: what a
//! test, a log reader or the control surface matches on. One token per code,
//! distinct, non-empty, and lower-case-hyphenated.
auto token(Code code) -> const char*;

//! One short sentence naming the failure class, without any path or detail.
auto summary(Code code) -> const char*;

/*!
 * A failure and the sentence that goes with it. `detail` is the specific
 * message (it names the module, the plug-in id, the platform's own error text),
 * and it is what the QString out-parameters of the host's public API carry, so
 * the text every existing caller shows is unchanged.
 */
struct Status
{
	Code code = Code::None;
	QString detail;

	auto failed() const -> bool { return code != Code::None; }

	//! The stable machine name of this failure. What a test, a log reader or the
	//! control surface matches on; see token() for the full table.
	auto token() const -> const char* { return lmms::clap::loader::token(code); }

	//! One short sentence naming the failure class, without the detail.
	auto summary() const -> const char* { return lmms::clap::loader::summary(code); }

	//! `detail` when there is one, `summary()` otherwise.
	auto message() const -> QString;
};

/*!
 * A loaded module file. Opaque on purpose: HMODULE on Windows, the dlopen
 * handle on POSIX, and neither type escapes into a caller. Non-copyable,
 * movable, and closed by its destructor, so no path can leak it.
 */
class Module
{
public:
	Module() = default;
	Module(const Module&) = delete;
	auto operator=(const Module&) -> Module& = delete;
	Module(Module&& other) noexcept;
	auto operator=(Module&& other) noexcept -> Module&;
	~Module();

	auto isOpen() const -> bool { return m_handle != nullptr; }

	//! Address of an exported symbol, or null. Null is not an error here: the
	//! caller is the one that knows which symbol it asked for and what a
	//! missing one means (see load()).
	auto symbol(const char* name) const -> void*;

	void close();

private:
	explicit Module(void* handle) : m_handle{handle} {}

	void* m_handle = nullptr;

	friend auto open(const QString& path, Status& status) -> Module;
};

/*!
 * Opens a module file. Failure: Code::LibraryUnavailable, with the platform's
 * own error text (dlerror() / FormatMessageW) in the detail.
 */
auto open(const QString& path, Status& status) -> Module;

/*!
 * A CLAP module as far as the host takes it: the entry symbol resolved, its
 * init() called, the plug-in factory fetched. It owns exactly what it opened,
 * so `reset()` -- deinit() when init() succeeded, then close the module -- is
 * the single place the module lifetime is unwound, whether the load finished
 * or stopped halfway.
 */
struct Library
{
	Module module;
	const clap_plugin_entry_t* entry = nullptr;
	const clap_plugin_factory_t* factory = nullptr;
	bool initialized = false;

	//! The UTF-8 path handed to clap_entry.init(), kept alive for as long as
	//! the module is open (the CLAP entry point is handed a path the plug-in
	//! may hold on to), and cleared by reset().
	QByteArray pathUtf8;

	//! True when both the entry and the factory are usable.
	auto isUsable() const -> bool { return entry != nullptr && factory != nullptr; }

	void reset();
};

/*!
 * THE LOADER LADDER, in one place, with a typed answer at every step:
 *
 *   open the file  -> LibraryUnavailable
 *   find clap_entry -> SymbolMissing
 *   check the version -> VersionUnsupported
 *   entry->init()  -> EntryInitFailed
 *   get the factory -> FactoryMissing
 *
 * Returns false with `status` set, having touched `library` only as far as it
 * got: the caller unwinds with library.reset() in the same place it unwinds a
 * later failure, and never has to know which step failed in order to clean up.
 * `library` is reset on entry, so a second call cannot inherit the first's
 * module.
 */
auto load(const QString& modulePath, Library& library, Status& status) -> bool;

} // namespace lmms::clap::loader

#endif // LMMS_CLAP_LOADER_H
