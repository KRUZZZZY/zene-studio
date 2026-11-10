/*
 * CarlaVst2Discovery.cpp - finding VST2 plug-ins for the Carla-backed VST2 host
 *                          (Q050-481.1).
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

/*
 * The plug-ins are never opened in this process to find out what they are.
 * Carla ships one discovery executable per binary kind (carla-discovery-native,
 * carla-discovery-win64.exe, ...); it loads the binary, prints what it found and
 * exits, so a plug-in that crashes on load costs a failed listing instead of the
 * session. Windows binaries are discovered through Wine, as Carla bridges them.
 * Everything here runs on the GUI/control thread; nothing touches audio.
 */

#include "CarlaVst2Discovery.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>

#include <CarlaNativePlugin.h>

#include "lmmsconfig.h"

#ifndef LMMS_BUILD_WIN32
#include <dlfcn.h>
#endif

#include "ConfigManager.h"

namespace lmms::carlavst2
{

namespace
{

//! carla-discovery's PLUGIN_IS_SYNTH hint bit (CarlaBackend.h).
constexpr qint64 PluginIsSynthHint = 0x004;
constexpr int NativeTimeoutMs = 15000;
constexpr int WineTimeoutMs = 90000;

struct CacheEntry
{
	qint64 size = -1;
	qint64 modified = -1;
	QList<Vst2Record> records;
};

QMutex& cacheMutex()
{
	static QMutex mutex;
	return mutex;
}

QHash<QString, CacheEntry>& cache()
{
	static QHash<QString, CacheEntry> entries;
	return entries;
}

quint32 readLe32(const QByteArray& bytes, int offset)
{
	if (offset < 0 || offset + 4 > bytes.size()) { return 0; }
	const auto* p = reinterpret_cast<const unsigned char*>(bytes.constData() + offset);
	return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
}

constexpr quint16 PeMachineAmd64 = 0x8664;
constexpr quint16 PeMachineI386 = 0x14c;

//! How Carla runs a PE of @a machine on this host: in process on Windows when it is the host's own
//! architecture, through the matching Wine bridge everywhere else.
Binary binaryOfPeMachine(quint16 machine)
{
	const bool is64 = machine == PeMachineAmd64;
	if (!is64 && machine != PeMachineI386) { return Binary::Unknown; }
#ifdef _WIN32
	return is64 == (sizeof(void*) == 8) ? Binary::Native : Binary::Unknown;
#else
	return is64 ? Binary::Win64 : Binary::Win32;
#endif
}

Binary peBinary(QFile& file, const QByteArray& head)
{
	const quint32 peOffset = readLe32(head, 0x3c);
	if (peOffset == 0 || !file.seek(peOffset)) { return Binary::Unknown; }
	const QByteArray pe = file.read(6);
	if (pe.size() < 6 || !pe.startsWith(QByteArray("PE\0\0", 4))) { return Binary::Unknown; }
	return binaryOfPeMachine(quint16(quint8(pe[4]) | (quint16(quint8(pe[5])) << 8)));
}

QString discoveryTool(Binary binary)
{
	const QString folder = runtimeFolder();
	if (folder.isEmpty()) { return QString(); }
	QString name;
	switch (binary)
	{
#ifdef _WIN32
	case Binary::Native: name = QStringLiteral("carla-discovery-native.exe"); break;
#else
	case Binary::Native: name = QStringLiteral("carla-discovery-native"); break;
#endif
	case Binary::Win64: name = QStringLiteral("carla-discovery-win64.exe"); break;
	case Binary::Win32: name = QStringLiteral("carla-discovery-win32.exe"); break;
	case Binary::Unknown: return QString();
	}
	const QString path = QDir(folder).filePath(name);
	return QFileInfo::exists(path) ? path : QString();
}

QString wineExecutable()
{
	const QString configured = QString::fromLocal8Bit(qgetenv("WINE"));
	if (!configured.isEmpty()) { return configured; }
	return QStandardPaths::findExecutable(QStringLiteral("wine"));
}

//! Runs one discovery tool on @a file; its stdout. *ran is false when it could not start or timed out (a result
//! not worth remembering: the next listing tries again).
QString runDiscovery(const QString& file, Binary binary, bool* ran)
{
	*ran = false;
	const QString tool = discoveryTool(binary);
	if (tool.isEmpty()) { return QString(); }
	QString program = tool;
	QStringList arguments{QStringLiteral("vst2"), file};
	QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
	if (binary == Binary::Win64 || binary == Binary::Win32)
	{
		program = wineExecutable();
		if (program.isEmpty()) { return QString(); }  // no Wine: nothing to remember either
		arguments.prepend(tool);
		// A fresh prefix would otherwise stop on Wine's Mono/Gecko installer prompt.
		if (!environment.contains(QStringLiteral("WINEDLLOVERRIDES")))
		{
			environment.insert(QStringLiteral("WINEDLLOVERRIDES"), QStringLiteral("mscoree,mshtml="));
		}
		if (!environment.contains(QStringLiteral("WINEDEBUG"))) { environment.insert(QStringLiteral("WINEDEBUG"), QStringLiteral("-all")); }
	}
	QProcess process;
	process.setProcessEnvironment(environment);
	process.setProcessChannelMode(QProcess::SeparateChannels);
	process.start(program, arguments);
	const int timeout = binary == Binary::Native ? NativeTimeoutMs : WineTimeoutMs;
	if (!process.waitForStarted(timeout))
	{
		qWarning("carlavst2: could not start %s: %s", qPrintable(program), qPrintable(process.errorString()));
		*ran = false;
		return QString();
	}
	if (!process.waitForFinished(timeout))
	{
		process.kill();
		process.waitForFinished(1000);
		qWarning("carlavst2: discovery of %s timed out after %d ms", qPrintable(file), timeout);
		*ran = false;
		return QString();
	}
	*ran = true;
	return QString::fromLocal8Bit(process.readAllStandardOutput());
}

void applyField(Vst2Record* record, const QString& key, const QString& value)
{
	if (key == QLatin1String("name")) { record->name = value; }
	else if (key == QLatin1String("maker")) { record->maker = value; }
	else if (key == QLatin1String("uniqueId")) { record->uniqueId = value.toLongLong(); }
	else if (key == QLatin1String("hints")) { record->isSynth = record->isSynth || (value.toLongLong() & PluginIsSynthHint) != 0; }
	else if (key == QLatin1String("category")) { record->isSynth = record->isSynth || value == QLatin1String("synth"); }
}

QList<Vst2Record> discoverFile(const QString& file)
{
	const QFileInfo info(file);
	const qint64 modified = info.lastModified().toMSecsSinceEpoch();
	{
		const QMutexLocker locker(&cacheMutex());
		const auto found = cache().constFind(file);
		if (found != cache().constEnd() && found->size == info.size() && found->modified == modified)
		{
			return found->records;
		}
	}
	const Binary binary = binaryOf(file);
	QList<Vst2Record> records;
	bool ran = binary == Binary::Unknown;  // not a binary Carla runs: settled, remember it
	if (binary != Binary::Unknown) { records = parseDiscoveryOutput(file, binary, runDiscovery(file, binary, &ran)); }
	if (ran)
	{
		const QMutexLocker locker(&cacheMutex());
		cache().insert(file, CacheEntry{info.size(), modified, records});
	}
	return records;
}

void addDir(QStringList* out, const QString& dir)
{
	const QFileInfo info(dir);
	if (dir.isEmpty() || !info.isDir()) { return; }
	const QString canonical = info.canonicalFilePath();
	for (const QString& seen : *out)
	{
		if (QFileInfo(seen).canonicalFilePath() == canonical) { return; }
	}
	out->append(dir);
}

} // namespace

QString binaryName(Binary binary)
{
	switch (binary)
	{
	case Binary::Native: return QStringLiteral("native");
	case Binary::Win64: return QStringLiteral("win64");
	case Binary::Win32: return QStringLiteral("win32");
	case Binary::Unknown: break;
	}
	return QStringLiteral("unknown");
}

Binary binaryFromName(const QString& name)
{
	if (name == QLatin1String("native")) { return Binary::Native; }
	if (name == QLatin1String("win64")) { return Binary::Win64; }
	if (name == QLatin1String("win32")) { return Binary::Win32; }
	return Binary::Unknown;
}

Binary binaryOf(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) { return Binary::Unknown; }
	const QByteArray head = file.read(64);
	if (head.startsWith("\x7f" "ELF"))
	{
#ifdef _WIN32
		return Binary::Unknown;
#else
		// EI_CLASS: 2 is a 64-bit object, 1 a 32-bit one. Carla's 32-bit POSIX bridge is not part
		// of the runtime this host looks for, so only the host's own word size is offered.
		const bool is64 = head.size() > 4 && head[4] == 2;
		return is64 == (sizeof(void*) == 8) ? Binary::Native : Binary::Unknown;
#endif
	}
	if (head.startsWith("MZ")) { return peBinary(file, head); }
	return Binary::Unknown;
}

QString runtimeFolder()
{
	// The weak stand-in (plugins/CarlaBase/DummyCarla.cpp) has no rack: no runtime, nothing to run.
	if (carla_get_native_rack_plugin() == nullptr) { return QString(); }
#ifdef LMMS_BUILD_WIN32
	const char* folder = carla_get_library_folder();
	return folder != nullptr ? QString::fromLocal8Bit(folder) : QString();
#else
	// The folder of the libcarla_native-plugin this process actually bound, read from the loader.
	// Not carla_get_library_folder(): it normalises the path with water's removeEllipsis(), which
	// writes out of bounds when the path contains ".." (measured 2026-11-02: SIGSEGV under glibc's
	// malloc checker with such a path, clean without). CarlaInstrument still calls it once per Rack.
	Dl_info info{};
	if (dladdr(reinterpret_cast<void*>(&carla_get_native_rack_plugin), &info) == 0 || info.dli_fname == nullptr)
	{
		return QString();
	}
	return QFileInfo(QString::fromLocal8Bit(info.dli_fname)).absolutePath();
#endif
}

QStringList searchDirs()
{
	QStringList out;
	const QString pinned = QString::fromLocal8Bit(qgetenv("ZENE_VST2_PATH"));
	if (!pinned.isEmpty())
	{
		for (const QString& dir : pinned.split(QDir::listSeparator(), Qt::SkipEmptyParts)) { addDir(&out, dir); }
		return out;
	}
	for (const QString& dir : QString::fromLocal8Bit(qgetenv("VST_PATH")).split(QDir::listSeparator(), Qt::SkipEmptyParts))
	{
		addDir(&out, dir);
	}
	addDir(&out, ConfigManager::inst()->vstDir());
	addDir(&out, QDir::homePath() + QStringLiteral("/.vst"));
	addDir(&out, QDir::homePath() + QStringLiteral("/.lxvst"));
	addDir(&out, QStringLiteral("/usr/lib/vst"));
	addDir(&out, QStringLiteral("/usr/local/lib/vst"));
	addDir(&out, QStringLiteral("/usr/lib/lxvst"));
	return out;
}

QList<Vst2Record> parseDiscoveryOutput(const QString& file, Binary binary, const QString& output)
{
	QList<Vst2Record> records;
	Vst2Record current;
	bool open = false;
	const QString prefix = QStringLiteral("carla-discovery::");
	for (QString line : output.split(QLatin1Char('\n')))
	{
		line = line.trimmed();
		if (!line.startsWith(prefix)) { continue; }
		const QString rest = line.mid(prefix.size());
		const int separator = rest.indexOf(QStringLiteral("::"));
		if (separator < 0) { continue; }
		const QString key = rest.left(separator);
		const QString value = rest.mid(separator + 2);
		if (key == QLatin1String("init"))
		{
			current = Vst2Record{file, QString(), QString(), 0, false, binary};
			open = true;
		}
		else if (key == QLatin1String("end"))
		{
			if (open && !current.name.isEmpty()) { records.append(current); }
			open = false;
		}
		else if (open) { applyField(&current, key, value); }
	}
	return records;
}

QList<Vst2Record> discoverAll()
{
	QList<Vst2Record> out;
	if (runtimeFolder().isEmpty()) { return out; }
	for (const QString& dir : searchDirs())
	{
		QStringList files;
		QDirIterator it(dir, {QStringLiteral("*.so"), QStringLiteral("*.dll"), QStringLiteral("*.DLL")},
			QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
		while (it.hasNext()) { files.append(QFileInfo(it.next()).absoluteFilePath()); }
		files.sort();
		for (const QString& file : files) { out.append(discoverFile(file)); }
	}
	return out;
}

} // namespace lmms::carlavst2
