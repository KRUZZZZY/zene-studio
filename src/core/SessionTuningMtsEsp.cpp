/*
 * SessionTuningMtsEsp.cpp - the MTS-ESP publication half of SessionTuning
 *                           (board card #712)
 *
 * The vendored thirdparty/mts-esp/ pair (upstream ODDSound/MTS-ESP, 0BSD -
 * see the README beside the files) is a SHIM: at static initialisation it
 * dlopen's upstream's separately-installed IPC core (libMTS.so) if one is
 * there, and every MTS_* call is an inert no-op if it is not. The wrapper
 * exposes no "am I connected" call, so the presence probe asks the loader
 * directly - that is what makes mts.master_set's refusal honest instead of
 * a silent no-op dressed as a success. Split out of SessionTuning.cpp for
 * the file-length ratchet; all four functions are members of SessionTuning.
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
 * License along with this program (see COPYING); if not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301 USA.
 *
 */

#include "SessionTuning.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "thirdparty/mts-esp/libMTSMaster.h"

namespace lmms
{

bool SessionTuning::mtsEspLibraryPresent()
{
	// Cached: the wrapper loads (or fails to load) the IPC core once, at
	// static initialisation, and that cannot change for the life of the
	// process. The probe asks the LOADER - the wrappers expose no
	// "connected" call, and MTS_CanRegisterMaster() answers true with no
	// library at all (thirdparty/mts-esp/README.md records that reading).
	static const bool present = []() {
#ifdef _WIN32
		// The wrapper loads LIBMTS.dll from the Known Folder; ask for it by
		// base name. UNVERIFIED on a Windows builder - there is none in this
		// wave (docs/KNOWN-LIMITATIONS.md), so a Windows arm is a typed
		// refusal until someone measures it.
		return GetModuleHandleW(L"LIBMTS.dll") != nullptr;
#else
		// The two paths libMTSMaster.cpp's POSIX load_lib() tries, in order.
		// RTLD_NOLOAD: the wrapper already loaded the library if it exists,
		// so this only LOOKS - it never pulls a new dependency in.
		const char* const paths[] = {
#ifdef __APPLE__
			"/Library/Application Support/MTS-ESP/libMTS.dylib",
#endif
			"/usr/local/lib/libMTS.so"};
		for (const char* path : paths)
		{
			if (void* handle = dlopen(path, RTLD_NOW | RTLD_NOLOAD))
			{
				dlclose(handle);
				return true;
			}
		}
		return false;
#endif
	}();
	return present;
}

bool SessionTuning::setMtsMaster(bool enabled, QString* error)
{
	if (enabled && !mtsEspLibraryPresent())
	{
		if (error != nullptr)
		{
			*error = QStringLiteral("the MTS-ESP IPC library is not installed (upstream "
				"ODDSound/MTS-ESP's libMTS.so, Linux: /usr/local/lib/libMTS.so) - the master "
				"calls would be inert no-ops, so arming is refused rather than faked "
				"(thirdparty/mts-esp/README.md; docs/KNOWN-LIMITATIONS.md)");
		}
		return false;
	}
	if (enabled)
	{
		MTS_RegisterMaster();
		m_masterEnabled = true;
		publishToMtsEsp();
		return true;
	}
	MTS_DeregisterMaster();
	m_masterEnabled = false;
	return true;
}

QJsonObject SessionTuning::mtsEspJson() const
{
	const bool present = mtsEspLibraryPresent();
	QJsonObject mts;
	mts.insert(QStringLiteral("library"), present
		? QStringLiteral("present") : QStringLiteral("absent"));
	mts.insert(QStringLiteral("master_enabled"), present && m_masterEnabled);
	mts.insert(QStringLiteral("clients"), present ? MTS_GetNumClients() : 0);
	return mts;
}

void SessionTuning::publishToMtsEsp()
{
	if (!m_masterEnabled || !mtsEspLibraryPresent()) { return; }
	const Table& table = m_tables[m_front.load(std::memory_order_acquire)];
	for (int key = 0; key < TableSize; ++key)
	{
		// An unmapped key has no frequency to publish; MTS-ESP's master table
		// wants 128 live values, so a table with holes is NOT published (the
		// previously published one stands). mts.get_state shows the table as
		// it is either way.
		if (!(table.frequencies[key] > 0.0)) { return; }
	}
	MTS_SetNoteTunings(table.frequencies.data());
	const QByteArray name = m_source.toUtf8();
	MTS_SetScaleName(name.left(127).constData());
}

} // namespace lmms
